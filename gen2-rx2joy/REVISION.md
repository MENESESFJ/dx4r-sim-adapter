# Revisión del firmware v1 y cambios en gen2

Revisión completa del firmware que produjo el `.uf2` funcionando.
El código base está bien construido: captura por PIO, seqlock en vez de
mutex, `bInterval = 1`, AND global de enlace en vez de por canal, y la
consola CDC como instrumento de medición. Nada de eso se tocó.

Lo que sigue son cuatro defectos reales encontrados, más lo que se
agregó para los protocolos nuevos.

---

## Defectos encontrados en el v1

### 1. El periodo de trama se medía mal cuando llegaban varias muestras juntas

En `poll_channels()` el timestamp se tomaba una sola vez, antes de
drenar la FIFO del PIO:

```c
uint32_t now = time_us_32();          /* una vez para todo el drenado */
while (!pio_sm_is_rx_fifo_empty(...)) {
    channel_feed(ch, t, now);         /* todas las muestras, mismo now */
}
```

Con más de una muestra encolada, la segunda y las siguientes daban
`frame_us = now - prev_sample_us = 0`. El número que muestra el OLED se
desplomaba a `0.0MS` de forma intermitente.

En el v1 era solo cosmético, porque el timeout era una constante. En
gen2 el timeout se calcula sobre ese periodo, así que el defecto pasaba
a ser funcional.

**Corregido** en dos lugares: el timestamp se toma por muestra, y
`channels_feed()` descarta cualquier delta fuera de 1 a 60 ms y suaviza
el resto con una media móvil.

### 2. Timeout de enlace fijo en 60 ms

`LINK_TIMEOUT_US 60000` estaba dimensionado para el SR2000 a 5,5 ms, o
sea unas 11 tramas de margen. Razonable ahí.

Con un PWM estándar de 20 ms esos mismos 60 ms son 3 tramas: bastan dos
perdidas seguidas para declarar el enlace caído y mandar los ejes a
cero. Un tirón en medio de una curva.

**Corregido**: el margen se calcula sobre el periodo realmente medido,
cuatro tramas más 2 ms, acotado entre 15 y 150 ms. Con el SR2000 queda
en ~24 ms, o sea más rápido que antes para detectar la pérdida real, y
con un receptor lento se relaja solo.

### 3. La calibración dependía de la frecuencia del receptor

`CENTER_SAMPLES 32` es un número de muestras, no un tiempo. A 5,5 ms
son 176 ms de quietud; a 20 ms son 640 ms; en i-BUS a 7 ms serían 224
ms. El criterio quedaba atado a lo que hiciera el receptor en vez de a
lo que importa, que es cuánto tiempo estuvo quieto el control.

**Corregido**: se exige quietud durante `CAL_STABLE_MS` (300 ms) **y**
un mínimo de 8 muestras. El tiempo hace el criterio predecible; el
mínimo de muestras evita que un receptor muy rápido dé por calibrado un
canal con un puñado de muestras ruidosas.

Además, cuando el control se movía, el v1 reiniciaba la ventana a cero y
perdía la muestra actual. Ahora la ventana se reinicia **desde** esa
muestra, así que soltar el control en otra posición empieza a contar de
inmediato.

### 4. `%.1f` en `print_status()` con newlib-nano

```c
"  (%.1f/%.1f/%.1f us)  ..."
```

En newlib-nano el soporte de coma flotante en `printf` viene desactivado
salvo que se pida explícitamente en el CMake. Sin eso, ese formato
imprime basura o nada.

Llamativo: `display.c` ya evitaba `%f` por este mismo motivo, y lo
documentaba en un comentario. La consola se quedó sin la misma
protección.

**Corregido**: formateo entero con la misma función `fmt_us()` que usa
el display.

---

## Lo que se agregó

### Tres protocolos con un solo camino de datos

Todo se convierte a **ticks** (50 por microsegundo) antes de entrar a la
calibración:

| Modo | Conversión |
|---|---|
| PWM | ya viene en ticks desde el PIO |
| i-BUS | microsegundos × 50 |
| S.BUS | `raw × 125 / 4 + 44000` (mapeo Futaba: 172→988 µs, 992→1500 µs, 1811→2012 µs) |

Unificar la unidad antes de calibrar es lo que permite que calibración,
escalado, display y consola sean idénticos en los tres modos. No hay
tres caminos que mantener en paralelo.

El escalado de S.BUS no necesita ser exacto por marca: basta con que sea
lineal y monótono, porque la calibración se encarga de los extremos
reales.

### Sincronización de trama por silencio

Los dos buses serie se sincronizan detectando el silencio entre tramas
(más de 800 µs sin bytes), no buscando el byte de cabecera dentro del
flujo. Buscar la cabecera puede enganchar en un byte de datos que
coincida, y el desfase se arrastra hasta el próximo reinicio.

La FIFO de la UART va **desactivada** a propósito: una interrupción por
byte da un timestamp exacto, y de ese timestamp depende la detección de
silencio. Con FIFO los bytes llegan en lotes y el instante real de cada
uno se pierde. El costo son unas 8000 interrupciones por segundo,
irrelevante frente al resto.

### Failsafe real en S.BUS

Este es el caso que ningún timeout puede ver: con failsafe activo, el
receptor **sigue emitiendo tramas puntuales** con las posiciones de
seguridad. El periodo medido se ve perfecto y el enlace parece sano.

S.BUS lleva el bit en la trama, así que se lee directamente. También se
usa el bit de *frame lost* para descartar tramas repetidas, que si no
ensuciarían la calibración y el periodo medido.

En PWM e i-BUS no hay señal equivalente y la detección sigue recayendo
en el AND de canales y el timeout. Para PWM esto funciona con el SR2000
porque en failsafe deja de emitir la dirección aunque mantenga el
acelerador.

### Calibración guardable en flash

Una por modo, en el último sector. Se carga sola al arrancar, así que el
adaptador queda operativo sin pasar por el neutro.

Guardar es **siempre explícito**: pulsación larga del botón CAL, o `w`
por consola. Escribir flash exige detener core1 y apagar interrupciones
algunos milisegundos, lo que corta el HID y el OLED. Eso no debe ocurrir
sin que el usuario lo pida.

`display_core1_main()` ahora llama `multicore_lockout_victim_init()`
antes que nada. Sin esa llamada, el intento de guardar cuelga el chip,
porque core1 seguiría ejecutando desde flash mientras el sector se
borra.

### Botón CAL con dos funciones

| Pulsación | Acción |
|---|---|
| Corta | Recalibrar |
| Larga (1,5 s) | Guardar la calibración en flash |

La acción larga se dispara al cumplirse el tiempo, con el botón aún
apretado, no al soltarlo. Así la confirmación aparece en el OLED
mientras el usuario sigue presionando, en vez de obligarlo a adivinar
cuánto es "largo".

---

## Lo que deliberadamente NO cambió

**El formato del reporte HID, el VID/PID y el número de serie.** Windows
indexa la calibración del panel de mandos y VRC Pro el mapeo de ejes por
esa terna. Cambiar cualquiera de los tres obligaría a remapear todo al
pasar del v1 a gen2. Solo cambia el nombre de producto, que es texto y
no afecta el índice.

Si algún día quieres distinguir dos adaptadores conectados al mismo PC,
el camino es derivar la serie del ID único del RP2040 — a costa de
remapear una vez.

**El CDC.** Es tu banco de pruebas: con `d` obtienes el volcado de ticks
crudos, que es exactamente lo que vas a necesitar para caracterizar los
rangos reales de Futaba, FlySky y Sanwa cuando tengas esos receptores.

**La ausencia de filtro digital.** `DEADBAND_TICKS` sigue en 0. Si el
neutro no queda quieto en `joy.cpl`, se sube; antes no.

**250 mA declarados** en el descriptor de configuración.

---

## Pendiente de verificar en hardware

| Qué | Cómo |
|---|---|
| Lectura del jumper en GP14 | Arrancar en las tres posiciones y confirmar el modo en el OLED |
| Polaridad de S.BUS | Si no llegan tramas, probar quitando `gpio_set_inover()` |
| Canales del bus | Confirmar que dirección sea el canal 1 y acelerador el 2 (`BUS_CH_STR` / `BUS_CH_THR` en `rx2joy.h`) |
| Byte de cierre S.BUS | `s` por consola lo muestra; varía entre receptores |
| Guardado en flash | Pulsación larga, desenchufar, reconectar, verificar que no pida calibrar |
| Amplitud de la señal del receptor | Con multímetro, antes de conectar cualquier receptor nuevo |

El `CMakeLists.txt` es una reconstrucción: compáralo con el tuyo del v1,
sobre todo en cómo importa el SDK (`pico_sdk_import.cmake`) y en las
opciones de compilación que hayas ajustado.

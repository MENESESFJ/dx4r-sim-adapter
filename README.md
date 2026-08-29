# DX4R PRO Sim Adapter — Firmware V1

DX4R Pro → SR2000 (5,5 ms) → RP2040 → USB HID → VRC Pro

## Cableado

```
USB VBUS (5V) ──────────────────→ SR2000 Bind/Battery (+)
USB GND ────────────────────────→ SR2000 Bind/Battery (−)

SR2000 Steering ──┬── 1.8k ──┬──→ GPIO2   (CH1)
                  │          │
                  │         3.3k
                  │          │
                  └──────────┴──→ GND

SR2000 Throttle ──┬── 1.8k ──┬──→ GPIO3   (CH2)
                  │          │
                  │         3.3k
                  │          │
                  └──────────┴──→ GND

Display SSD1306 0.96" (opcional)
  SDA ────────────────────────→ GPIO4
  SCL ────────────────────────→ GPIO5
  VCC ────────────────────────→ 3V3
  GND ────────────────────────→ GND

Medición del riel de 5 V (opcional, tomar lo más cerca posible del SR2000)
  Pin RX+ ──┬── 10k ──┬──→ GPIO26 / pin físico 31 (ADC0)
            │         │
            │        10k
            │         │
            └─────────┴──→ GND
```

Tomá la muestra en el conector del receptor, no en el pin VBUS del Pico.
El objetivo es detectar la caída de tensión que provocan los picos de
consumo de RF del SR2000; medida en VBUS, más cerca de la fuente, esa
caída se ve amortiguada y el número miente.

El divisor 1,8k/3,3k lleva 5 V a 3,24 V. El VIH del RP2040 es 2,0 V con
Schmitt trigger, así que hay margen de sobra. No hace falta level shifter.

Poné 100 µF entre VBUS y GND lo más cerca posible del receptor. Los picos
de consumo de RF pueden hacer caer la línea lo suficiente como para que
Windows se queje del puerto.

## Placa: Raspberry Pi Pico oficial

Este firmware está pensado para el **Pico original de Raspberry Pi
Foundation** (RP2040, micro-USB, 40 pines castellated). No es el mismo
board que el "RP2040 Super Mini" que menciona el documento de diseño; el
Pico oficial es más grande (21×51 mm) pero tiene mejor documentación y
soporte probado con TinyUSB — para esta fase de prototipo pesa más la
confiabilidad que el tamaño.

Diferencia práctica: el cable es **micro-USB**, no USB-C.

### Pinout usado (pin físico → función)

| Pin físico | GPIO  | Función                  |
|-----------:|-------|---------------------------|
| 4          | GP2   | CH1 steering (entrada)    |
| 5          | GP3   | CH2 throttle (entrada)    |
| 6          | GP4   | I2C0 SDA → OLED           |
| 7          | GP5   | I2C0 SCL → OLED           |
| 31         | GP26  | ADC0 → sensado de riel    |
| 20         | GP15  | Botón CAL (a GND)         |
| 36         | 3V3(OUT) | alimentación del OLED  |
| 40         | VBUS  | 5V de USB → alimenta al SR2000 |
| 3/8/13/18/23/28/33/38 | GND | tierra común    |

VBUS en el Pico oficial está cableado directo al conector USB, sin
interruptor de firmware de por medio — confirmado por el datasheet, no
hace falta verificarlo con multímetro como con un clon genérico.

## Botón de recalibración

Un pulsador momentáneo entre GP15 y GND. Sin resistencia externa: el
pull-up interno del RP2040 hace todo el trabajo.

```
GP15 (pin físico 20) ──┬── pulsador ──→ GND
                        │
                    (pull-up interno,
                     activado en firmware)
```

Un click centra ambos ejes en su posición actual — dejá los controles en
neutro antes de presionar. Hace exactamente lo mismo que la tecla `c` de
la consola CDC; usá el que tengas a mano.

No agrega latencia perceptible: se sondea una vez por vuelta del bucle
principal, mismo costo que ya tenía el sondeo de la consola serie. El
antirrebote es por tiempo (30 ms) y garantiza una sola recalibración por
click, sin importar cuánto lo sostengas apretado.

## Display SSD1306 (opcional)

Un OLED de 0.96" por I2C en GPIO4/5, dirección 0x3C.

Muestra: estado del enlace y frame rate medido, barras bipolares de
steering y throttle con marca de centro, ancho de pulso en µs y valor HID
de cada eje, tensión del riel del receptor, temperatura del chip, estado
USB y ritmo de reportes.

**El display no agrega latencia porque no corre en el mismo núcleo.**

Un frame completo de 128x64 son 1025 bytes por I2C. A 400 kHz eso son
23 ms, cuatro veces el frame del SR2000. Refrescarlo desde el bucle de
captura destruiría la latencia que todo el proyecto busca proteger.

Por eso el OLED entero vive en **core1**, que hasta ahora estaba ocioso:

| Recurso | Antes | Con display |
|---|---|---|
| Core0 (PIO → HID) | ocupado | sin cambios |
| Core1 | ocioso | OLED + ADC |
| I2C0, ADC | libres | en uso, solo core1 |
| PIO0 SM0/SM1 | captura | sin cambios |
| RAM | — | +1 KB framebuffer |

El único costo para core0 son dos incrementos y dos barreras de memoria,
50 veces por segundo. No es medible.

La comunicación entre núcleos es un **seqlock**, no un mutex, y esa
distinción importa: un mutex puede bloquear al escritor. Core0 no puede
esperar nunca a core1 por un display. Con seqlock core0 publica y sigue;
si core1 lee justo durante la escritura, lo detecta por el contador y
reintenta.

Si no conectás el display, el init falla con NACK, core1 se duerme y el
adaptador funciona idéntico. El OLED es estrictamente opcional.

### Tensión del riel

Necesita un divisor 10k/10k desde VBUS a GPIO26. Sin él la pantalla
muestra `--.--V` en vez de inventar un número.

Es la medición más útil de las tres: te dice si el SR2000 está hundiendo
el riel de USB. Si ves caídas por debajo de 4,7 V bajo carga de RF, el
capacitor de 100 µF es obligatorio y no opcional.

### Consumo

Subí `bMaxPower` a 250 mA en el descriptor. SR2000 (~50 mA) + OLED
(~20 mA) + RP2040 (~30 mA) ya no entran cómodos en los 100 mA que pide
un dispositivo USB por defecto.

## Configuración de la radio

- Frame Rate: **5.5ms**. Solo funciona con el SR2000, y en ese modo solo
  operan steering y throttle. Que es justo lo que necesitamos.
- Travel: empezá en 100%. Después de medir la resolución, probá 150% y
  volvé a medir (ver más abajo).
- Expo, dual rate y subtrim: dejalos como los usás en pista. Toda la
  sensación del transmisor viaja intacta hasta el simulador.

## Regenerar la fuente del display

`font5x7.h` sale de `tools/gen_font.py`, donde los glifos están dibujados
como arte ASCII en vez de hex a mano. El script se auto-verifica: vuelve a
renderizar cada glifo desde los bytes generados y compara con el original.

```bash
cd tools && python3 gen_font.py   # escribe font5x7.h e imprime una muestra
```

## Compilar

```bash
export PICO_SDK_PATH=/ruta/al/pico-sdk
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
mkdir build && cd build
cmake ..
make -j
```

Sale `dx4r_sim_adapter.uf2`. BOOTSEL, arrastrar, listo.

## Calibración

Automática al encender, sin flash.

1. Enchufá el adaptador con el receptor ya enlazado y los controles en
   neutro. A los ~200 ms captura el centro de cada canal.
2. Mové volante y gatillo a los topes una vez. El rango se expande solo.
3. Listo. Se pierde al desconectar; eso se guarda en flash en la Fase 3.

Si movés algo durante el paso 1, la captura de centro se reinicia sola
hasta que todo quede quieto.

**Recalibración manual**: botón físico en GP15, o tecla `c` por consola
CDC. Ambos hacen exactamente lo mismo — dejá los controles en neutro
antes de usarlos.

## Consola de diagnóstico

El dispositivo enumera como HID **y** como puerto serie. Abrilo con
cualquier terminal y mandá:

- `s` — estado: enlace, calibración, rango en ticks y µs, frame rate medido
- `d` — activa/desactiva volcado de ticks crudos
- `c` — recalibrar
- `?` — ayuda

## Medir la resolución real (Prueba B)

```bash
pip install pyserial numpy
python analiza_pulsos.py COM5 --canal 0 --segundos 60
```

Mientras corre, barré el volante **muy** lento de tope a tope. Unos 30
segundos por barrida completa. El script busca el escalón mínimo entre
valores distintos, que es la cuantización del receptor, y de ahí saca los
pasos efectivos.

Predicción: con travel al 100% deberías ver alrededor de 1300–1400 pasos,
que son los 11 bits de DSMR repartidos sobre ±150% de recorrido. Si sale
eso, subí travel a 150% y repetí: tenés que ganar pasos reales.

Si sale algo muy distinto, la predicción estaba mal y conviene rehacer los
números del objetivo de resolución del documento con el dato medido.

## Latencia

- Frame del SR2000 a 5,5 ms: 5,5 ms
- Captura PIO: despreciable
- Poll USB con bInterval=1: hasta 1 ms
- Piso total: ~6–7 ms

El `bInterval` está en `usb_descriptors.c`, último argumento de
`TUD_HID_DESCRIPTOR`. TinyUSB trae 10 por defecto. No lo toques.

## Decisiones que quedaron fuera a propósito

- **Sin filtro.** `DEADBAND_TICKS` está en 0. Medí primero el jitter en
  neutro con `analiza_pulsos.py` y subilo solo si el eje no queda quieto
  en `joy.cpl`.
- **Sin CH3/CH4.** A 5,5 ms no existen. Si querés probarlos, pasá la radio
  a 11 ms y usá el SR410. El descriptor ya tiene 8 botones reservados.
- **Sin guardar calibración en flash.** Fase 3.
- **Sin gráficos en el display.** Nada de historiales ni curvas: cada
  píxel extra son ciclos de core1, y core1 después va a servir para algo
  más útil que dibujar.

## Detección de pérdida de enlace

Del manual de la DX4R Pro: ante pérdida de señal el receptor lleva throttle
a la posición de failsafe y **los demás canales dejan de emitir salida**.
O sea, timeout en CH1 mientras CH2 sigue vivo = enlace caído, no cable
suelto. El firmware centra los dos ejes cuando un canal supera
`LINK_TIMEOUT_US` (60 ms, unos 11 frames).

## VID/PID

Están puestos en 1209:0001, el par de pruebas de pid.codes. Sirve para uso
privado. Si en algún momento hacés más de una unidad para otra gente,
pedí un PID propio.

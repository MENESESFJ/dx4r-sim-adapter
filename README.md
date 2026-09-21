# DX4R PRO Sim Adapter — Firmware + PCB V1

DX4R Pro → SR2000 (5,5 ms) → Raspberry Pi Pico / RP2040 → USB HID → VRC Pro

Adaptador de baja latencia para utilizar una radio **Spektrum DX4R Pro** con
**VRC Pro**, capturando directamente las señales PWM del receptor
**Spektrum SR2000** mediante PIO del RP2040 y presentándolas al PC como un
dispositivo USB HID.

La versión actual utiliza una **Raspberry Pi Pico oficial**, receptor
SR2000 a 5,5 ms, dos canales (steering y throttle/brake), OLED SSD1306
opcional, calibración automática y una PCB de componentes through-hole
pensada para montaje manual.

---

## Arquitectura

```text
Spektrum DX4R Pro
       │ RF / DSMR
       ▼
Spektrum SR2000
       │
       ├── CH1 Steering ── divisor 10k/20k ──→ GP2
       │
       └── CH2 Throttle ── divisor 10k/20k ──→ GP3

USB 5 V ──→ Raspberry Pi Pico
    │
    └──────→ H1 ──→ alimentación SR2000

RP2040
 ├── PIO0 SM0 → CH1
 ├── PIO0 SM1 → CH2
 ├── GP4/GP5 → OLED I2C
 ├── GP26    → ADC riel receptor
 ├── GP15    → botón CAL
 └── TinyUSB → USB HID + CDC
                     │
                     ▼
                  VRC Pro
```

El objetivo principal del diseño es mantener la cadena de captura lo más
directa y determinista posible:

**SR2000 → PIO → escalado → USB HID**

El display, ADC y tareas auxiliares quedan separados del camino crítico.

---

## Cableado

### Alimentación del receptor

El SR2000 se alimenta desde el VBUS de 5 V proporcionado por el USB de la
Raspberry Pi Pico.

En la PCB final esa alimentación sale por **H1**, un conector independiente
de dos pines:

```text
Pico VBUS (5 V) ───────────────→ H1 (+) ──→ SR2000 Bind/Battery (+)
Pico GND ──────────────────────→ H1 (−) ──→ SR2000 Bind/Battery (−)
```

La alimentación del receptor no se obtiene desde el conector de señales
U1. Esto es intencional.

H1 mantiene físicamente separada la alimentación del receptor de las
entradas de señal y facilita tanto el cableado como las mediciones del riel.

---

### CH1 — Steering

```text
SR2000 Steering ── 10k ──┬────→ GPIO2 / GP2
                          │
                         20k
                          │
                         GND
```

### CH2 — Throttle / Brake

```text
SR2000 Throttle ── 10k ───┬────→ GPIO3 / GP3
                           │
                          20k
                           │
                          GND
```

R1/R5 forman el divisor de CH1.

R2/R6 forman el divisor de CH2.

Los resistores son:

- R1 = 10 kΩ
- R2 = 10 kΩ
- R5 = 20 kΩ
- R6 = 20 kΩ

Todos son componentes **through-hole**, coherentes con la decisión de
evitar componentes SMD en esta primera PCB.

---

## Por qué 10k/20k en las entradas del receptor

La relación del divisor es:

```text
VGPIO = VRX × 20k / (10k + 20k)
      = VRX × 0,6667
```

Por lo tanto:

| HIGH del SR2000 | Entrada aproximada al RP2040 |
|---:|---:|
| 5,0 V | 3,33 V |
| 3,3 V | 2,20 V |

La versión inicial del proyecto utilizaba un divisor de **1,8k/3,3k**,
cuya relación era aproximadamente 0,647.

La versión definitiva adopta **10k/20k**.

### Razones

1. **Menor carga sobre la salida del SR2000**

Con HIGH = 5 V:

```text
1,8k + 3,3k = 5,1k
I ≈ 5 / 5100
I ≈ 0,98 mA
```

Con el nuevo divisor:

```text
10k + 20k = 30k
I ≈ 5 / 30000
I ≈ 0,167 mA
```

La nueva red carga aproximadamente **seis veces menos** la salida del
receptor.

2. **Divisor completamente definido por la PCB**

La relación de tensión queda determinada explícitamente por R1/R5 y
R2/R6.

No se utiliza ninguna resistencia interna del receptor como elemento de
diseño del divisor.

El receptor naturalmente tiene una impedancia de salida finita, pero no
forma parte de la relación nominal utilizada para diseñar la adaptación
de nivel.

3. **Mayor nivel lógico disponible**

La relación pasa de aproximadamente:

```text
0,647 → 0,667
```

Esto entrega un HIGH ligeramente mayor al RP2040, útil especialmente si
la etapa lógica del SR2000 resulta ser de 3,3 V.

4. **No introduce una carga temporal relevante**

Las señales son pulsos PWM de aproximadamente 1–2 ms dentro de un frame
de 5,5 ms.

La impedancia 10k/20k sigue siendo suficientemente baja para esta
aplicación y no representa una limitación relevante frente a los tiempos
que estamos midiendo.

---

## Niveles eléctricos del RP2040

Con IOVDD = 3,3 V, el datasheet del RP2040 especifica aproximadamente:

```text
VIH mínimo = 2,0 V
VIL máximo = 0,8 V
```

Por esto:

### Si el SR2000 entrega HIGH ≈ 5 V

```text
5,0 V × 0,6667 ≈ 3,33 V
```

Hay margen amplio sobre VIH.

### Si el SR2000 entrega HIGH ≈ 3,3 V

```text
3,3 V × 0,6667 ≈ 2,20 V
```

Sigue por encima del VIH especificado, pero el margen queda reducido a
aproximadamente:

```text
2,20 V − 2,00 V = 0,20 V
```

Por esta razón **no conviene asumir el nivel de salida del SR2000**.

Hay que medirlo.

Con los niveles esperados, el divisor resistivo evita la necesidad de un
level shifter activo y mantiene el circuito extremadamente simple.

---

## Antes de soldar: verificar el HIGH real del SR2000

El SR2000 se alimenta a aproximadamente 5 V, pero eso no implica
necesariamente que su salida lógica también sea de 5 V.

Podría utilizar internamente lógica de 3,3 V.

La diferencia importa porque el divisor 10k/20k produce:

```text
HIGH SR2000 ≈ 5,0 V → GPIO ≈ 3,33 V
HIGH SR2000 ≈ 3,3 V → GPIO ≈ 2,20 V
```

Ambos valores son compatibles nominalmente con el RP2040, pero el segundo
caso tiene bastante menos margen.

### Cómo medirlo sin osciloscopio

El propio Pico puede utilizarse como instrumento.

1. Utilizá el mismo divisor **10k/20k** de CH1.
2. Conectá temporalmente su salida a GP26 / ADC0 en lugar de GP2.
3. Compilá y flasheá `peak_probe.uf2`.
4. Abrí una terminal serie a 115200 baudios.
5. Mové steering a ambos extremos durante 5–10 segundos.
6. Anotá el valor `PICO=` que se imprime.

El ADC del RP2040 puede muestrear muchísimo más rápido que los pulsos de
~1–2 ms que necesitamos observar.

El programa utiliza un peak-hold en software para capturar el HIGH.

### Interpretación

Aproximadamente:

```text
PICO ≈ 3,3 V → señal original ≈ 5 V
PICO ≈ 2,2 V → señal original ≈ 3,3 V
```

Si el valor dividido se aproxima demasiado a 2,0 V, conviene revisar la
adaptación de nivel antes de cerrar definitivamente el hardware.

No tiene sentido diseñar alrededor de un nivel supuesto si podemos
medirlo.

### Importante sobre la PCB final

GP26 también se utiliza en la placa definitiva para medir el riel de
alimentación mediante el divisor R3/R4.

Por eso la prueba de `peak_probe` está pensada principalmente como prueba
temporal antes del montaje definitivo, o desconectando temporalmente la
red de medición de VBUS.

Terminada la prueba:

```text
CH1 → GP2
CH2 → GP3
GP26 → divisor de medición de VBUS
```

y se vuelve a flashear:

```text
dx4r_sim_adapter.uf2
```

---

## Medición del riel de 5 V

La placa incluye otro divisor completamente independiente:

```text
RX VBUS ── 10k ──┬────→ GPIO26 / ADC0
                  │
                 10k
                  │
                 GND
```

R3 y R4 son ambos de **10 kΩ**.

La relación es:

```text
VADC = VBUS / 2
```

Por ejemplo:

```text
5,0 V → 2,50 V en GP26
```

El firmware conoce el factor ×2 y reconstruye la tensión del riel.

Tomá eléctricamente la muestra del riel que alimenta al receptor, lo más
cerca posible de H1/SR2000.

El objetivo es detectar las caídas producidas por los picos de consumo de
RF del receptor.

Medir exclusivamente cerca de la fuente USB puede ocultar parte de la
caída de tensión que queremos detectar.

---

## Condensador de 100 µF

Se mantiene la recomendación original:

**100 µF entre VBUS y GND lo más cerca posible del receptor.**

Su objetivo es amortiguar transitorios de consumo del SR2000.

La PCB V1 actual no depende de este condensador para funcionar y no lo
utiliza como parte de ninguna red lógica.

Por eso puede considerarse una mejora condicionada a medición.

Si durante la prueba el riel presenta caídas importantes —especialmente
si baja de aproximadamente **4,7 V** durante actividad RF—, instalá el
condensador directamente entre los terminales de alimentación del
receptor o en H1.

Esto mantiene la placa simple y permite decidir con datos en lugar de
agregar componentes sin comprobar su necesidad.

---

## Placa: Raspberry Pi Pico oficial

Este firmware está pensado para el **Pico original de Raspberry Pi
Foundation**:

- RP2040
- micro-USB
- 40 pines castellated
- 21 × 51 mm

No es el mismo board que el "RP2040 Super Mini" que apareció durante las
primeras etapas del proyecto.

El Pico oficial es más grande, pero para este proyecto aporta:

- documentación oficial completa;
- alimentación conocida;
- pinout estable;
- soporte probado con Pico SDK;
- TinyUSB ampliamente utilizado;
- menor incertidumbre frente a placas clon.

Para esta fase pesa más la confiabilidad que ahorrar unos milímetros.

Diferencia práctica:

**el conector USB es micro-USB, no USB-C.**

---

## PCB V1 final

La primera PCB fabricable del proyecto fue rediseñada deliberadamente
para utilizar componentes **through-hole**.

La razón no es eléctrica sino práctica:

- facilita montaje manual;
- facilita reemplazar componentes;
- facilita medir con multímetro;
- facilita modificaciones durante pruebas;
- evita soldadura manual de resistores SMD extremadamente pequeños;
- permite comprar la PCB desnuda y los componentes por separado.

El Gerber actual corresponde a una PCB de dos capas.

Dimensiones aproximadas:

```text
76,20 mm × 43,18 mm
```

El diseño incluye Raspberry Pi Pico, headers, resistores THT, pulsador,
OLED y conexiones del receptor.

---

## Validación del esquema y Gerber

La versión actual fue comparada entre:

```text
Esquema JSON
      ↓
Netlist
      ↓
PCB / Gerber
      ↓
Pinout Raspberry Pi Pico
      ↓
Firmware
```

Las redes funcionales principales coinciden:

| Función | Hardware | Firmware |
|---|---|---|
| Steering | R1/R5 → GP2 | GP2 |
| Throttle | R2/R6 → GP3 | GP3 |
| OLED SDA | GP4 | GP4 |
| OLED SCL | GP5 | GP5 |
| Sensado VBUS | R3/R4 → GP26 | GP26 / ADC0 |
| CAL | SW1 → GP15 → GND | GP15 |
| OLED alimentación | 3V3 OUT | 3V3 |
| Receptor alimentación | H1 → VBUS/GND | independiente |
| Tierra | GND común | GND común |

Los pines de alimentación que no se utilizan en U1 no indican una pista
faltante: la alimentación del SR2000 se realiza deliberadamente mediante
H1.

---

## Pinout usado

| Pin físico | GPIO | Función |
|-----------:|------|---------|
| 4 | GP2 | CH1 steering |
| 5 | GP3 | CH2 throttle/brake |
| 6 | GP4 | I2C0 SDA → OLED |
| 7 | GP5 | I2C0 SCL → OLED |
| 20 | GP15 | Botón CAL → GND |
| 31 | GP26 | ADC0 → sensado del riel |
| 36 | 3V3(OUT) | alimentación OLED |
| 40 | VBUS | 5 V USB → H1 → SR2000 |
| 3/8/13/18/23/28/33/38 | GND | tierra común |

VBUS en el Pico oficial está conectado al sistema de alimentación USB del
board y no depende de que el firmware habilite una salida.

---

## Botón de recalibración

Se utiliza un pulsador momentáneo entre GP15 y GND.

En la PCB se utiliza un pulsador THT **TS665CJ**.

No necesita resistencia externa porque se utiliza el pull-up interno del
RP2040.

```text
GP15 (pin físico 20) ── pulsador ──→ GND
                         │
                    pull-up interno
```

El botón está activo en LOW.

Un click centra ambos ejes utilizando su posición actual.

Por eso:

**dejá volante y gatillo en neutro antes de presionarlo.**

Hace exactamente lo mismo que la tecla `c` de la consola CDC.

No agrega latencia perceptible.

Se sondea una vez por vuelta del bucle principal y utiliza antirrebote por
tiempo de 30 ms.

Esto garantiza una sola recalibración por pulsación independientemente de
cuánto tiempo se mantenga apretado.

---

## Display SSD1306

El OLED es opcional.

Configuración:

```text
SSD1306 0,96"
128 × 64
I2C
Dirección: 0x3C
Frecuencia: 400 kHz
```

Cableado:

```text
OLED SDA ───────────────→ GP4
OLED SCL ───────────────→ GP5
OLED VCC ───────────────→ 3V3
OLED GND ───────────────→ GND
```

Muestra:

- estado del enlace;
- frame rate medido;
- steering;
- throttle;
- marca de centro;
- ancho de pulso en µs;
- valor HID de cada eje;
- tensión del riel del receptor;
- temperatura del RP2040;
- estado USB;
- ritmo de reportes.

### El display no agrega latencia al camino crítico

Un frame completo de 128×64 son aproximadamente 1025 bytes por I2C.

A 400 kHz eso representa alrededor de 23 ms, mucho más que el frame de
5,5 ms del SR2000.

Actualizar el OLED desde el bucle de captura sería una mala arquitectura:
la interfaz gráfica terminaría condicionando la latencia del control.

Por eso el OLED entero vive en **core1**.

| Recurso | Sin display | Con display |
|---|---|---|
| Core0 | PIO → HID | sin cambios |
| Core1 | ocioso | OLED + ADC |
| I2C0 | libre | OLED |
| ADC | libre | sensado VBUS |
| PIO0 SM0/SM1 | captura | captura |
| RAM | — | +~1 KB framebuffer |

El único costo para core0 son dos incrementos y barreras de memoria al
publicar el estado compartido.

La comunicación entre núcleos utiliza un **seqlock**, no un mutex.

La diferencia es importante:

un mutex podría hacer esperar al escritor.

Core0 no debe esperar nunca a core1 por una pantalla.

Con seqlock:

```text
Core0 publica → continúa
Core1 lee
si detecta escritura concurrente → vuelve a leer
```

La prioridad sigue siendo siempre la captura.

Si el OLED no está conectado, su inicialización falla con NACK y core1
deja de actualizarlo.

El adaptador continúa funcionando normalmente.

El OLED es estrictamente opcional.

---

## Tensión del riel

El firmware necesita el divisor 10k/10k hacia GP26 para conocer la tensión
del receptor.

Sin ese divisor, el valor mostrado no debe interpretarse como una medida
válida.

Esta es probablemente una de las variables de diagnóstico más útiles del
proyecto porque permite detectar problemas que de otra forma pueden
parecer fallas de USB, firmware o radio.

Si el SR2000 provoca caídas del riel durante actividad RF, el display
permite observarlas sin equipamiento adicional.

Si aparecen caídas bajo aproximadamente 4,7 V, el condensador de 100 µF
pasa de ser una mejora preventiva a una recomendación práctica.

---

## Consumo USB

El descriptor USB utiliza:

```text
bMaxPower = 250 mA
```

Orden de magnitud esperado:

```text
SR2000 ≈ 50 mA
OLED   ≈ 20 mA
RP2040 ≈ 30 mA
```

El conjunto ya no queda cómodamente representado como un dispositivo USB
de solamente 100 mA.

Por esto el descriptor anuncia una capacidad mayor.

---

## Configuración de la radio

### Frame Rate

Usar:

**5.5 ms**

Este modo funciona con el SR2000 y proporciona steering y throttle.

Es exactamente lo que necesita VRC Pro.

### Travel

Comenzar con:

**100 %**

Después de medir la resolución efectiva:

**probar 150 % y repetir la medición.**

Esto permite comprobar experimentalmente si el aumento de travel entrega
realmente más pasos utilizables.

### Expo, dual rate y subtrim

Dejalos configurados como los usarías conduciendo.

La filosofía del adaptador es no intentar recrear en software la sensación
de la radio.

La señal resultante del transmisor debe viajar intacta hasta el simulador.

---

## Regenerar la fuente del display

`font5x7.h` se genera desde:

```text
tools/gen_font.py
```

Los glifos están dibujados como arte ASCII en lugar de editar bytes
hexadecimales a mano.

El script se auto-verifica: vuelve a renderizar cada carácter generado y
lo compara con el original.

```bash
cd tools
python3 gen_font.py
```

Esto escribe:

```text
font5x7.h
```

y muestra una prueba del resultado.

---

## Compilar

```bash
export PICO_SDK_PATH=/ruta/al/pico-sdk
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .

mkdir build
cd build
cmake ..
make -j
```

Se generan dos archivos `.uf2`:

```text
dx4r_sim_adapter.uf2
peak_probe.uf2
```

### `dx4r_sim_adapter.uf2`

Firmware normal del adaptador.

### `peak_probe.uf2`

Firmware auxiliar para medir el HIGH real entregado por el SR2000.

Se utiliza durante validación eléctrica y no durante operación normal.

Para instalar:

1. Mantener BOOTSEL.
2. Conectar el Pico.
3. Copiar el `.uf2`.
4. Reiniciar.

---

## Nota sobre `peak_probe.c`

Las primeras versiones de `tools/peak_probe/peak_probe.c` fueron escritas
cuando el hardware utilizaba el divisor:

```text
1,8k / 3,3k
```

La PCB definitiva utiliza:

```text
10k / 20k
```

El algoritmo de `peak_probe` no depende directamente de esos valores:
lee el ADC y entrega el voltaje observado en GP26.

Sin embargo, los comentarios y valores esperados del código deben
mantenerse sincronizados con esta documentación.

Para la versión final deben considerarse:

```text
~3,33 V → HIGH original cercano a 5 V
~2,20 V → HIGH original cercano a 3,3 V
```

---

## Calibración

La calibración es automática al encender.

Actualmente no se guarda en flash.

### Secuencia

1. Enchufá el adaptador con el SR2000 ya enlazado.
2. Dejá steering y throttle en neutro.
3. Después de aproximadamente 200 ms de estabilidad se captura el centro.
4. Mové volante y gatillo a ambos extremos.
5. Los rangos mínimo y máximo se expanden automáticamente.

Si movés alguno de los controles durante la captura inicial del centro,
la secuencia se reinicia hasta encontrar suficientes muestras estables.

### Recalibración manual

Puede realizarse mediante:

```text
botón GP15
```

o desde la consola CDC:

```text
c
```

Ambas rutas llaman al mismo procedimiento interno.

No son dos sistemas de calibración distintos.

---

## Consola de diagnóstico

El dispositivo enumera simultáneamente como:

- USB HID
- puerto serie CDC

Desde cualquier terminal pueden enviarse los siguientes comandos:

```text
s
```

Estado:

- enlace;
- calibración;
- rango;
- ticks;
- µs;
- frame rate.

```text
d
```

Activa o desactiva el volcado de ticks crudos.

```text
c
```

Recalibra.

```text
?
```

Muestra ayuda.

---

## Medir la resolución real — Prueba B

```bash
pip install pyserial numpy
python analiza_pulsos.py COM5 --canal 0 --segundos 60
```

Mientras corre:

mové steering **muy lentamente** de un extremo al otro.

Idealmente unos 30 segundos por barrido completo.

El script busca el escalón mínimo entre valores distintos.

Ese escalón representa la cuantización observable del receptor y permite
calcular los pasos efectivos.

### Hipótesis inicial

Con travel al 100 % esperamos aproximadamente:

```text
1300–1400 pasos
```

basados en los 11 bits del enlace DSMR distribuidos sobre aproximadamente
±150 % de recorrido.

Después:

1. medir a 100 %;
2. configurar travel 150 %;
3. repetir exactamente la misma prueba.

Si aumenta el número real de pasos, el cambio aporta resolución útil.

Si el resultado es muy diferente de la estimación inicial, se descarta la
predicción y se recalcula el objetivo a partir de datos medidos.

**Los datos mandan sobre la hipótesis.**

---

## Latencia

Componentes principales:

```text
Frame SR2000 @ 5,5 ms       ≈ 5,5 ms
Captura PIO                 ≈ despreciable frente al frame
USB HID bInterval = 1 ms    ≤ ~1 ms de espera
```

Piso aproximado:

```text
~6–7 ms
```

El `bInterval` se encuentra en:

```text
usb_descriptors.c
```

como último argumento de:

```text
TUD_HID_DESCRIPTOR
```

TinyUSB utiliza valores mayores por defecto en varias configuraciones.

Para este proyecto:

**no cambiar bInterval=1 sin una razón medida.**

---

## Decisiones de firmware

### Captura por PIO

No se utiliza:

```text
micros()
attachInterrupt()
```

La captura se realiza por PIO.

El PIO funciona a 100 MHz y el loop de medida utiliza dos ciclos por
iteración:

```text
20 ns por tick
50 ticks / µs
```

La resolución interna de medida es muchísimo mayor que la resolución
efectiva del enlace DSMR.

Eso evita que el microcontrolador se transforme en el factor limitante.

---

### HID de 16 bits

El HID utiliza ejes signed de 16 bits.

Esto no significa que el sistema tenga 16 bits reales de resolución.

La resolución efectiva sigue determinada por:

```text
DX4R → DSMR → SR2000
```

El objetivo es simplemente que el escalado USB no agregue cuantización
adicional.

---

### Sin filtro

`DEADBAND_TICKS` permanece inicialmente en:

```text
0
```

No se agrega filtrado preventivamente.

Primero hay que medir el jitter real en neutro utilizando:

```text
analiza_pulsos.py
```

y `joy.cpl`.

Solamente si el eje no queda estable se agrega la mínima zona muerta
necesaria.

Agregar filtros antes de medir puede mejorar visualmente una señal a costa
de introducir latencia o esconder información útil.

---

### Sin CH3/CH4

A 5,5 ms se utilizan únicamente steering y throttle con el SR2000.

Si en el futuro se quiere experimentar con más canales:

```text
DX4R → 11 ms → SR410
```

El descriptor HID ya tiene botones reservados.

Eso queda fuera del objetivo de V1.

---

### Sin guardar calibración en flash

La calibración se pierde al desconectar.

Persistencia en flash queda para una fase posterior.

Esto evita introducir complejidad antes de validar:

- estabilidad;
- resolución;
- comportamiento del receptor;
- experiencia dentro de VRC Pro.

---

### Sin gráficos históricos en el display

Nada de:

- curvas;
- tendencias;
- historiales;
- animaciones complejas.

Cada función gráfica adicional consume tiempo de core1.

Core1 se reservó para diagnóstico, no para convertir el adaptador en una
interfaz gráfica.

---

## Detección de pérdida de enlace

Según el comportamiento esperado del sistema DX4R/SR2000, ante pérdida de
enlace el throttle utiliza failsafe mientras otros canales pueden dejar de
actualizarse.

Por eso el firmware no considera válido mantener indefinidamente el
último valor recibido.

Se utiliza:

```text
LINK_TIMEOUT_US = 60000
```

A 5,5 ms equivale a aproximadamente:

```text
11 frames
```

Si un canal supera el timeout, el firmware considera que el enlace dejó de
ser confiable y lleva los ejes a condición segura/centrada según la lógica
del programa.

La pantalla utiliza el mismo criterio lógico que el HID.

No debe existir una situación en la que la interfaz diga **LINK** mientras
el HID ya considera perdido el enlace.

---

## Filosofía del proyecto

El diseño sigue algunos principios simples:

1. **Medir antes de compensar.**
2. **No filtrar una señal que todavía no hemos caracterizado.**
3. **No agregar componentes que no solucionen un problema observado.**
4. **Mantener el camino crítico fuera del display y del diagnóstico.**
5. **Usar PIO para aquello donde el tiempo importa.**
6. **Usar core1 para aquello donde el tiempo no debe interferir.**
7. **Separar alimentación, señales y diagnóstico de manera clara.**
8. **Mantener el hardware reparable y medible.**
9. **Preferir datos reales del SR2000 sobre supuestos de diseño.**
10. **Mantener esquema, PCB, firmware y documentación sincronizados.**

---

## BOM eléctrica principal — PCB V1

| Ref. | Valor / componente | Función |
|---|---|---|
| R1 | 10 kΩ | serie CH1 |
| R2 | 10 kΩ | serie CH2 |
| R3 | 10 kΩ | divisor ADC VBUS |
| R4 | 10 kΩ | divisor ADC VBUS |
| R5 | 20 kΩ | CH1 → GND |
| R6 | 20 kΩ | CH2 → GND |
| SW1 | TS665CJ THT | recalibración |
| H1 | header 2 pines | VBUS + GND hacia receptor |
| U1 | header 2×3 | señales/conexiones receptor |
| OLED | SSD1306 0,96" | display opcional |
| MCU | Raspberry Pi Pico RP2040 | captura + HID |

Todos los resistores del diseño definitivo son THT.

No se requieren resistores SMD.

---

## Pruebas recomendadas antes de considerar V1 cerrada

### Prueba A — nivel HIGH del SR2000

Medir con `peak_probe`.

Objetivo:

determinar si el SR2000 entrega lógica cercana a 5 V o a 3,3 V.

### Prueba B — resolución

Medir con `analiza_pulsos.py`.

Objetivo:

obtener número real de pasos y comparar travel 100 % vs 150 %.

### Prueba C — jitter de neutro

Mantener steering y throttle inmóviles.

Objetivo:

decidir con datos si `DEADBAND_TICKS` debe seguir en cero.

### Prueba D — riel de alimentación

Observar VBUS en GP26 durante operación RF.

Objetivo:

decidir si el condensador de 100 µF es necesario.

### Prueba E — pérdida de enlace

Apagar o alejar el transmisor de forma controlada.

Objetivo:

confirmar que OLED y HID reaccionen de forma coherente.

### Prueba F — VRC Pro

Verificar:

- enumeración HID;
- steering;
- throttle/brake;
- centro;
- extremos;
- respuesta;
- estabilidad;
- sensación respecto del transmisor real.

---

## Estado de V1

Hardware base:

```text
Spektrum DX4R Pro
Spektrum SR2000
Raspberry Pi Pico oficial / RP2040
PCB 2 capas
Componentes THT
OLED SSD1306 opcional
```

Entradas:

```text
CH1 → 10k/20k → GP2
CH2 → 10k/20k → GP3
```

Diagnóstico:

```text
VBUS → 10k/10k → GP26
OLED → GP4/GP5
CDC USB
```

Control:

```text
CAL → GP15
```

USB:

```text
HID + CDC
bInterval = 1 ms
bMaxPower = 250 mA
```

Captura:

```text
PIO0 SM0
PIO0 SM1
```

Objetivo final:

**obtener una interfaz DX4R Pro → VRC Pro de baja latencia, reproducible,
medible y reparable, sin agregar complejidad que no esté justificada por
datos.**

---

## VID/PID

Actualmente:

```text
VID:PID = 1209:0001
```

Es adecuado para las pruebas privadas actuales.

Si el proyecto deja de ser exclusivamente personal o se distribuyen
unidades a terceros, corresponde obtener/asignar un PID propio y actualizar
el descriptor USB.

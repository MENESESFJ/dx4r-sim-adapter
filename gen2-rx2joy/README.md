# RX2JOY — gen2

Adaptador USB multimarca para simuladores de RC de superficie.
Convierte la salida de casi cualquier receptor de superficie en un
joystick USB HID, sin drivers ni software intermedio.

```
Radio  →  Receptor  →  RX2JOY  →  USB HID  →  VRC Pro
         (PWM / S.BUS / i-BUS)
```

---

## Revisiones

| Revisión | Carpeta | Entrada | Radios | Estado |
|---|---|---|---|---|
| rev1 "Yusleidy" | [`hardware/rev1-yusleidy/`](hardware/rev1-yusleidy/) | PWM, 2 canales | Spektrum | PCB diseñada |
| rev2 "UART" | [`hardware/rev2-uart/`](hardware/rev2-uart/) | PWM + bus serial | Multimarca | En desarrollo |

La rev2 es la rev1 más cuatro componentes: un conector de bus serial
(J4), un jumper selector de protocolo (JP1) y dos resistencias (R5, R6).
Todo lo demás es idéntico, así que el firmware de la rev2 corre en la
rev1 quedándose en modo PWM.

---

## Por qué funciona con cualquier marca

Cada fabricante usa su propio protocolo de radio: DSMR en Spektrum,
AFHDS 3 en FlySky, T-FHSS en Futaba, FH5 en Sanwa. Son propietarios,
cerrados y, salvo los de Spektrum, sin implementaciones abiertas
maduras.

El truco es que **ese enlace queda encerrado dentro del receptor**.
RX2JOY nunca lo ve. Lo que lee es la salida del receptor, que sí está
estandarizada en la industria. Por eso cambiar de marca de radio se
reduce a cambiar de receptor, y el adaptador no necesita saber nada del
protocolo de radio.

---

## Modos de entrada

| Modo | Cableado | Señal | Reposo | Canales |
|---|---|---|---|---|
| **PWM** | Un cable por canal | Pulsos 1000–2000 µs cada ~20 ms | — | 2 |
| **S.BUS** | Un solo cable | UART 100000 8E2, **invertido** | Bajo | hasta 16 |
| **i-BUS** | Un solo cable | UART 115200 8N1 | Alto | hasta 14 |

### Selección de modo

Un jumper en JP1, sin reflashear ni recompilar:

| Posición del jumper | Modo |
|---|---|
| Pines 1–2 (hacia 3V3) | PWM |
| Pines 2–3 (hacia GND) | S.BUS |
| Sin jumper | i-BUS |

El firmware distingue los tres estados leyendo GP14 dos veces, una con
pull-up interno y otra con pull-down. Si ambas lecturas coinciden, el
pin está atado a un riel; si difieren, está flotando.

### Sobre S.BUS invertido

S.BUS es UART invertido, lo que normalmente exige un transistor
inversor. En el RP2040 no hace falta: se invierte en el propio pin con
`gpio_set_inover()`. Un componente menos en la placa.

---

## ⚠ Niveles eléctricos — limitación conocida de la rev2

**El RP2040 no tolera 5 V en sus GPIO.** Las resistencias en serie de la
placa limitan corriente; **no convierten el nivel**. Son dos cosas
distintas y conviene no confundirlas.

Con una señal de 5 V, el pin no recibe 5 V: los diodos de protección
internos del RP2040 lo clampean en torno a 3,6–3,9 V y la resistencia en
serie fija la corriente que circula por ellos.

| Entrada | R | Corriente por el clamp | Veredicto |
|---|---|---|---|
| PWM 5 V | R3/R4 = 10 kΩ | ~0,11 mA | Aceptable; práctica habitual |
| Bus serial 5 V | R5 = 1 kΩ | ~1,35 mA | **Funciona, pero no es diseño correcto** |

El problema del bus serial no es que se queme: es que se apoya en unos
diodos pensados para transitorios de ESD, no para conducir de forma
continua, y esa corriente se inyecta al riel de 3V3.

**Por qué no se arregla cambiando el divisor.** No existe una relación
fija que sirva para los dos casos. Para que 5 V den 3,0 V hace falta
6,8k/10k, y con esa misma relación un receptor de 3,3 V entrega 1,96 V,
por debajo del umbral de entrada alta del RP2040 (2,15 V). O sirve para
uno o sirve para el otro.

**Arreglos previstos para la rev3:**

| Solución | Costo | Comentario |
|---|---|---|
| Diodo Schottky (BAT54) del nodo de señal a 3V3, con R5 = 1 kΩ | 1 componente | El clamp lo hace el diodo, no el chip. Funciona con 3,3 V y con 5 V |
| Buffer 74LVC1G17 alimentado a 3V3 | 1 componente + desacople | Entradas tolerantes a 5 V por diseño, Schmitt trigger que además limpia flancos. Un 74LVC3G17 cubre los tres canales |

**Mientras tanto: medir siempre la amplitud de salida del receptor con
multímetro antes de conectarlo.** La mayoría entrega 3,3 V, pero no
todos.

---

## Pinout

| GPIO | Función | Va a |
|---|---|---|
| GP1 | Bus serial (UART0 RX) | J4 vía R5 |
| GP2 | PWM dirección | J1 vía R3 |
| GP3 | PWM acelerador | J2 vía R4 |
| GP4 | I2C0 SDA | OLED |
| GP5 | I2C0 SCL | OLED |
| GP14 | Selector de protocolo | JP1 |
| GP15 | Botón CAL | SW1 |
| GP26 | ADC sensado de riel | Divisor R1/R2 |

Idénticos al proyecto original salvo GP1 y GP14, que son los dos pines
nuevos de la rev2.

---

## Conectores

| Ref | Función | Pines |
|---|---|---|
| J1 | PWM dirección | SIG / NC / GND |
| J2 | PWM acelerador | SIG / NC / GND |
| J4 | Bus serial S.BUS / i-BUS | SIG / NC / GND |
| P1 | Alimentación del receptor | 5V / GND |
| J3 | OLED SSD1306 | GND / VCC / SCL / SDA |

**El pin central de J1, J2 y J4 no está conectado.** El receptor se
alimenta desde P1, con un cable aparte. Así se puede alimentar el
receptor desde otra fuente si hace falta, y la placa no le impone una
tensión que quizá no tolere.

---

## Resistencias

| Ref | Valor | Función |
|---|---|---|
| R3, R4 | 10 kΩ | Limitan corriente hacia los diodos de protección del GPIO |
| R5 | **1 kΩ** | Igual función, pero baja a propósito |
| R6 | 10 kΩ | Pull-down del bus serial |
| R1, R2 | 10 kΩ | Divisor para sensar la tensión de riel por ADC |

R5 no puede ser de 10 kΩ porque forma un **divisor** con R6. Con
10k/10k, un nivel alto de 3,3 V llegaría al GPIO como 1,65 V, por debajo
del umbral de entrada, y el bus no se leería. Con 1 kΩ quedan 3,0 V,
holgadamente válidos.

R6 cumple además una función útil: define el estado de la línea cuando
no hay receptor conectado, de modo que el firmware puede distinguir "sin
receptor" de "receptor presente pero sin datos".

---

## Calibración

Cada marca escala distinto, y los valores difieren incluso dentro de una
misma marca según el modo (Futaba normal contra SR/UR, Sanwa normal
contra SSR/SUR). El firmware **no asume rangos fijos**.

Valores de partida, solo como semilla:

| Modo | Mínimo | Centro | Máximo |
|---|---|---|---|
| PWM | 1000 µs | 1500 µs | 2000 µs |
| S.BUS | 172 | 992 | 1811 |
| i-BUS | 1000 | 1500 | 2000 |

**Cómo calibrar:** dejar los controles en neutro hasta que desaparezca
el aviso `CAL?` del OLED (300 ms de quietud), y luego mover volante y
gatillo a sus topes una vez. El rango solo se expande.

**Botón CAL (GP15):**

| Pulsación | Acción |
|---|---|
| Corta | Recalibrar |
| Larga (1,5 s) | Guardar la calibración en flash |

Se guarda un juego de valores por modo, así que cambiar el jumper no
obliga a recalibrar. La calibración guardada se carga sola al arrancar.

El escalado al rango HID se hace **por tramos** respecto al centro
calibrado, no por interpolación lineal única: como el centro casi nunca
queda a mitad de camino por el trim de la radio, una interpolación única
dejaría el punto muerto corrido hacia un lado.

---

## Salida USB

| Parámetro | Valor |
|---|---|
| Clase | HID Joystick + CDC (consola) |
| Ejes | 2, de 16 bits **con signo** (−32768 a 32767) |
| Botones | 8, reservados |
| Polling | 1 ms (1000 Hz) |
| Corriente declarada | 250 mA |
| VID / PID | 0x1209 / 0x0001 (pid.codes) |
| Número de serie | `"000001"`, fijo |

El polling se deja en el mínimo aunque el receptor entregue datos cada
14 o 20 ms: así el PC recoge cada trama apenas llega, en vez de esperar
al siguiente ciclo de sondeo. Esa espera sería latencia gratuita.

**El número de serie es fijo a propósito.** Windows indexa la
calibración del panel de mandos y VRC Pro el mapeo de ejes por la terna
VID/PID/serie. Derivarlo del ID único del RP2040 permitiría distinguir
dos adaptadores en el mismo PC, pero obligaría a remapear todo una vez.
Si algún día hay dos unidades conviviendo, ese es el cambio a hacer.

**El VID/PID es de pid.codes, reservado para pruebas y uso privado.** No
distribuir hardware con este par.

---

## Consola CDC

El adaptador expone además un puerto serie virtual. Es el banco de
pruebas para caracterizar receptores nuevos:

| Tecla | Acción |
|---|---|
| `d` | Volcado continuo de `canal,ticks,frame_us` |
| `c` | Recalibrar |
| `s` | Estado: modo, enlace, failsafe, rangos, tramas buenas y malas |
| `w` | Guardar calibración en flash |
| `e` | Borrar la calibración guardada |
| `?` | Ayuda |

`tools/analiza_pulsos.py` consume la salida de `d` para medir la
resolución real del enlace.

---

## Montaje

1. Soldar primero los componentes bajos: resistencias y condensadores.
2. Headers hembra para la RP2040-Zero, para poder reemplazarla sin
   desoldar.
3. Conectores de servo alineados en el borde, respetando el orden
   serigrafiado.
4. JP1 accesible desde fuera de la carcasa: se cambia cada vez que se
   prueba un receptor distinto.
5. Verificar con multímetro que el pin central de J1, J2 y J4 no tenga
   continuidad con ningún riel antes de conectar un receptor.

**Antes de conectar un receptor nuevo, medir la amplitud de su señal de
salida.** Ver la sección de niveles eléctricos más arriba.

---

## Firmware

```
firmware/
├── CMakeLists.txt
└── src/
    ├── main.c              Bucle principal, botón CAL, consola, HID
    ├── rx2joy.h            Pines, constantes y API de los módulos
    ├── inputs.c            Selector de modo, PWM por PIO, S.BUS, i-BUS
    ├── channels.c          Calibración, escalado y detección de enlace
    ├── calstore.c          Persistencia de calibración en flash
    ├── usb_descriptors.c   Descriptores TinyUSB (HID + CDC)
    ├── tusb_config.h       Configuración de TinyUSB
    ├── pwm_capture.pio     Captura de PWM por PIO (heredado de gen1)
    ├── display.c / .h      OLED, corriendo en core1
    ├── ssd1306.c / .h      Driver del OLED
    ├── font5x7.h           Fuente, generada por tools/gen_font.py
    └── shared_state.c / .h Seqlock core0 → core1
```

Requisitos: Pico SDK con TinyUSB. El `CMakeLists.txt` necesita
`pico_sdk_import.cmake` a su lado.

**Unidad interna común: ticks, a 50 por microsegundo.** Los tres
protocolos se convierten a esa unidad antes de entrar a la calibración,
de modo que calibración, escalado, display y consola son idénticos en
los tres modos.

**Restricción que condiciona todo el diseño: `tud_task()` no puede
bloquearse.** Por eso la lectura de PWM va por PIO y la del bus serial
por interrupción de UART, nunca esperando dentro del bucle principal. Y
por eso el OLED vive entero en core1, comunicado por seqlock: un frame
completo del SSD1306 son 23 ms de I2C, cuatro veces el periodo de trama
del SR2000.

Ver [`REVISION.md`](REVISION.md) para los defectos corregidos respecto
del firmware de gen1 y las decisiones de diseño.

---

## Compatibilidad de receptores

| Marca | Receptor | Modo | Estado |
|---|---|---|---|
| Spektrum | SR2000 | PWM | Probado |
| FlySky | serie FGr4 | i-BUS | Por probar |
| Futaba | con puerto S.BUS | S.BUS | Por probar |
| Sanwa | con puerto SSR | PWM | Por probar |

Notas por marca:

- **Futaba y Sanwa**: los modos de alta velocidad (SR/UR, SSR/SUR) usan
  pulsos más frecuentes y en algunos casos más angostos. Si aparecen
  lecturas erráticas, dejar el canal en modo Normal; los pocos
  milisegundos extra no se notan en simulador.
- **El puerto S.BUS no siempre viene activado de fábrica.** En varios
  receptores de superficie hay que habilitarlo desde la radio, y a veces
  comparte pin con un canal PWM. Revisar el manual del receptor.
- **Failsafe**: con S.BUS se detecta por el bit de la trama. Es el único
  método fiable, porque el receptor sigue emitiendo tramas puntuales con
  las posiciones de seguridad y ningún timeout las vería. En PWM e i-BUS
  no hay señal equivalente: la detección recae en el timeout y en exigir
  que **todos** los canales sigan llegando.
- **Canales del bus**: se asume dirección en el canal 1 y acelerador en
  el 2. Configurable en `rx2joy.h` (`BUS_CH_STR`, `BUS_CH_THR`).

---

## Estado

- [x] Esquemático rev2 verificado
- [x] Layout rev2
- [x] Firmware multimarca (PWM, S.BUS, i-BUS)
- [x] Calibración persistente en flash
- [ ] PCB rev2 fabricada
- [ ] Pruebas con receptores de otras marcas
- [ ] rev3 con adaptación de nivel en la entrada serial

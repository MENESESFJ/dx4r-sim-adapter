# RX2JOY

**Adaptador USB casero que convierte una radio de superficie RC en
joystick para simuladores de PC.**

`RX → RP2040 → USB HID → VRC Pro`

Pilotar con tu propia radio de pista en lugar de un gamepad. El
adaptador lee las señales del receptor y las presenta al PC como un
joystick USB estándar: sin drivers, sin software intermedio y sin
permisos de administrador. Se enchufa y funciona en cualquier PC.

---

## Generaciones

| Carpeta | Placa | Radios | Entrada | Estado |
|---|---|---|---|---|
| [`gen1-dx4r/`](gen1-dx4r/) | Raspberry Pi Pico | Spektrum DX4R Pro | PWM (SR2000) | Funcionando, congelada |
| [`gen2-rx2joy/`](gen2-rx2joy/) rev1 "Yusleidy" | RP2040-Zero | Spektrum | PWM | PCB diseñada |
| [`gen2-rx2joy/`](gen2-rx2joy/) rev2 "UART" | RP2040-Zero | Multimarca | PWM / S.BUS / i-BUS | En desarrollo |

La **gen1** está congelada como referencia histórica: fue el prototipo
que validó el concepto y produjo el primer `.uf2` funcional. No recibe
más cambios.

La **gen2** es la línea activa. La rev2 agrega una entrada de bus serial
y un jumper selector, lo que la hace compatible con receptores de
cualquier marca de superficie sin tocar el firmware ni el hardware.

---

## Cómo funciona

La radio transmite a su receptor con su propio protocolo propietario
(DSMR en Spektrum, AFHDS 3 en FlySky, T-FHSS en Futaba). Ese enlace
queda encerrado dentro del receptor y el adaptador nunca lo ve. Lo que
el adaptador lee es la salida del receptor, que sí está estandarizada.
Por eso cambiar de marca de radio se reduce a cambiar de receptor.

Tres formas de entrada soportadas en la rev2:

| Modo | Cableado | Señal | Canales |
|---|---|---|---|
| **PWM** | Un cable por canal | Pulsos de 1000–2000 µs | 2 |
| **S.BUS** | Un solo cable | UART 100000 8E2 invertido | hasta 16 |
| **i-BUS** | Un solo cable | UART 115200 8N1 | hasta 14 |

La selección se hace con un jumper en la placa, sin reflashear:

```
jumper hacia 3V3  →  PWM
jumper hacia GND  →  S.BUS
sin jumper        →  i-BUS
```

---

## Características

- **Baja latencia.** Lectura de PWM por PIO del RP2040 con 20 ns de
  resolución, sin bloquear el bucle USB, y polling HID a 1 ms.
- **16 bits con signo por eje.** El escalado no agrega cuantización
  propia por encima de la que ya impone el enlace de radio.
- **Calibración automática** con botón físico, que guarda mínimo, centro
  y máximo reales de cada canal, y puede persistirse en flash. Necesaria
  porque cada marca escala distinto.
- **Detección de failsafe** por el bit de la trama en S.BUS, no solo por
  ausencia de señal: un receptor en failsafe sigue emitiendo tramas
  puntuales y ningún timeout las vería.
- **OLED SSD1306** opcional con estado de enlace, modo activo, posición
  de ejes, periodo de trama y voltaje del riel.
- **Consola CDC** para instrumentación: volcado de pulsos crudos para
  caracterizar receptores nuevos.
- **Sin drivers.** Enumera como joystick HID nativo.

---

## Estructura del repositorio

```
├── gen1-dx4r/              Prototipo Pico + SR2000 (congelado)
│   ├── firmware/
│   ├── hardware/           Gerbers y esquemático
│   └── tools/              Utilidades de análisis de pulsos
└── gen2-rx2joy/            Línea activa
    ├── README.md
    ├── REVISION.md         Defectos corregidos y decisiones de diseño
    ├── hardware/
    │   ├── rev1-yusleidy/  PWM, Spektrum
    │   └── rev2-uart/      Multimarca
    └── firmware/
```

---

## Hardware

Núcleo: **RP2040-Zero** (Waveshare, USB-C), montada sobre headers para
poder reemplazarla.

Receptores probados y previstos:

| Marca | Receptor | Modo | Estado |
|---|---|---|---|
| Spektrum | SR2000 | PWM | Probado |
| FlySky | serie FGr4 | i-BUS | Por probar |
| Futaba | con puerto S.BUS | S.BUS | Por probar |

El receptor se alimenta desde un conector dedicado en la placa, no desde
los conectores de señal.

**Advertencia:** el RP2040 no tolera 5 V en sus GPIO. Medir la amplitud
de salida del receptor antes de conectarlo. Ver la sección de niveles
eléctricos en el README de gen2.

---

## Estado actual

- [x] Prototipo funcionando en protoboard
- [x] PCB gen1 fabricada
- [x] Esquemático rev2 UART verificado
- [x] Layout rev2 UART
- [x] Firmware multimarca (PWM, S.BUS, i-BUS)
- [ ] PCB rev2 fabricada y probada
- [ ] Pruebas con receptores de otras marcas
- [ ] rev3 con adaptación de nivel en la entrada serial

---

## Licencia

Por definir. Habitual en proyectos de este tipo: MIT para el firmware,
CERN-OHL-P para el hardware.

---

## Agradecimientos

El proyecto [VRC-Pico](https://github.com/DynaMight1124/VRC-Pico) sirvió
como referencia inicial del concepto.

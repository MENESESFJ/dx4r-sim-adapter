# DX4R PRO Sim Adapter

## Propósito

Adaptador USB casero que convierte la radio de control remoto **Spektrum DX4R Pro** en joystick inalámbrico para simuladores de carrera RC en PC, principalmente **VRC Pro**. Uso: pilotar con tu propia radio de pista en lugar de gamepad.

## Arquitectura

```
DX4R Pro (radio)
    ↓
SR2000 (receptor Spektrum, 5.5ms, 2 canales)
    ↓
RP2040 (Raspberry Pi Pico)
    ├─ Core0: captura PIO → HID USB
    └─ Core1: OLED + ADC (voltaje del riel)
    ↓
USB HID (joystick)
    ↓
VRC Pro (Windows)
```

## Características clave

- **Latencia ultra-baja**: 5,5 ms del receptor + captura por PIO (20 ns) + poll USB 1 ms = ~6–7 ms total. Competitivo con controles comerciales.
- **Resolución**: ~11 bits efectivos del enlace DSMR (1300–1400 pasos en el rango usado).
- **Calibración automática**: al encender detecta el centro; el rango se expande al mover los controles.
- **Recalibración física**: botón CAL en GPIO15 (a GND), antirrebote por firmware, sin resistencias.
- **Display opcional**: OLED SSD1306 0,96" en core1 (no roba latencia). Muestra estado de enlace, barras de ejes, ancho de pulso, voltaje del receptor, temperatura del chip y ritmo de reportes.
- **Instrumentación**: puerto CDC para volcado de ticks crudos; script Python mide la resolución real del enlace sin osciloscopio.

## Hardware

| Componente | Cantidad | Nota |
|---|---:|---|
| Raspberry Pi Pico (oficial) | 1 | micro-USB |
| Resistencia 1,8 kΩ | 2 | divisor de nivel CH1/CH2 |
| Resistencia 3,3 kΩ | 2 | divisor de nivel CH1/CH2 |
| Resistencia 10 kΩ | 2 | divisor de riel de 5V |
| Capacitor 100 µF | 1 | contra picos de RF |
| OLED SSD1306 0,96" I2C | 1 | opcional |
| Pulsador momentáneo | 1 | botón CAL |
| Receptor Spektrum SR2000 | 1 | vienen con el combo DX4R Pro |

**Costo total**: ~USD 5–8 en componentes (sin contar la radio ni receptor).

## Software

- **Firmware**: C SDK de Pico, TinyUSB (HID + CDC), PIO para captura.
- **Fuente personalizada**: 5×7 pixel, generada desde arte ASCII con verificación de ida y vuelta.
- **Seqlock**: comunicación entre núcleos sin mutexes que bloqueen core0.
- **Antirrebote**: debounce por tiempo (30 ms) en botón CAL.

## Fases completadas

**Fase 0 → V1**: arquitectura, PIO, HID, display, botón CAL.

## Fases pendientes

- **Fase 2**: validación con VRC Pro en pista, ajustes de travel/EPA en la radio.
- **Fase 3**: guardar calibración en flash, persistencia entre sesiones.
- **Fase 4**: PCB definitiva, caja impresa 3D, conectores soldados.

## Documentación

- `README.md` — cableado, compilación, calibración, consola CDC.
- `analiza_pulsos.py` — medición de resolución sin osciloscopio.
- `tools/gen_font.py` — generador de fuente verificable desde arte ASCII.

## Estado actual

Firmware V1 compilable, testeado en simulación. Listo para prototipar en protoboard. Display integrado en core1 sin costo de latencia. Botón CAL operativo.

---

**Contacto/repo**: [tu GitHub]

**Licencia**: MIT (librería PIO), TinyUSB (BSD), resto: dominio público para uso privado.

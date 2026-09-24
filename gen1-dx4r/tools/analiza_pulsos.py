#!/usr/bin/env python3
"""
analiza_pulsos.py — mide la resolucion efectiva real del enlace DSMR.

Esta es la Prueba B sin osciloscopio. El firmware vuelca ticks crudos de
20 ns por el puerto CDC; aca los juntamos y sacamos el escalon minimo
entre valores distintos, que es la cuantizacion del receptor.

Uso:
    1. Conecta el adaptador y averigua el puerto (COM5, /dev/ttyACM0, ...)
    2. python analiza_pulsos.py COM5 --canal 0 --segundos 60
    3. Mientras corre, barre el volante MUY lento de tope a tope.
       Lento en serio: 30 segundos por barrida completa.

Requiere: pip install pyserial numpy
"""

import argparse
import sys
import time
from collections import Counter

import numpy as np
import serial

TICKS_PER_US = 50.0


def capturar(puerto, canal, segundos):
    ser = serial.Serial(puerto, 115200, timeout=0.5)
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(b"d")          # activar volcado
    ser.flush()

    ticks, frames = [], []
    t0 = time.time()
    print(f"Capturando {segundos}s del canal {canal}. Barre el control MUY lento...")

    while time.time() - t0 < segundos:
        linea = ser.readline().decode("ascii", errors="ignore").strip()
        if not linea or linea.startswith("#"):
            continue
        partes = linea.split(",")
        if len(partes) != 3:
            continue
        try:
            ch, t, frame_us = int(partes[0]), int(partes[1]), int(partes[2])
        except ValueError:
            continue
        if ch != canal:
            continue
        ticks.append(t)
        if 0 < frame_us < 100000:
            frames.append(frame_us)

    ser.write(b"d")          # apagar volcado
    ser.close()
    return np.array(ticks), np.array(frames)


def analizar(ticks, frames):
    if ticks.size < 500:
        print(f"Solo {ticks.size} muestras. Muy poco, revisa la conexion.")
        return

    us = ticks / TICKS_PER_US

    print(f"\n--- Muestras: {ticks.size} ---")
    print(f"Rango medido : {us.min():.2f} .. {us.max():.2f} us "
          f"(span {us.max() - us.min():.2f} us)")

    if frames.size:
        print(f"Frame rate   : mediana {np.median(frames)/1000:.2f} ms, "
              f"p99 {np.percentile(frames, 99)/1000:.2f} ms, "
              f"jitter p99-p1 {(np.percentile(frames,99)-np.percentile(frames,1))/1000:.2f} ms")

    # El escalon de cuantizacion: diferencia minima no nula entre valores
    # distintos que aparecen de forma repetida. Se toma la moda de las
    # diferencias de primer orden entre valores unicos ordenados.
    unicos = np.unique(ticks)
    if unicos.size < 3:
        print("Casi no hay valores distintos. Movete mas.")
        return

    difs = np.diff(unicos)
    difs = difs[difs > 0]
    moda, cuenta = Counter(difs.tolist()).most_common(1)[0]
    escalon_us = moda / TICKS_PER_US

    print(f"\nValores unicos    : {unicos.size}")
    print(f"Escalon dominante : {moda} ticks = {escalon_us:.3f} us "
          f"({cuenta} ocurrencias)")

    span_us = us.max() - us.min()
    if escalon_us > 0:
        pasos = span_us / escalon_us
        bits = np.log2(pasos) if pasos > 1 else 0
        print(f"\nPasos efectivos en el recorrido usado: ~{pasos:.0f}  "
              f"({bits:.1f} bits)")
        print("\nSi esto sale cerca de 1300-1400 pasos con travel al 100%,")
        print("estas viendo los 11 bits de DSMR repartidos sobre +-150%.")
        print("Sube travel a 150% en la radio y repite: deberias ganar pasos.")

    # Ruido en reposo: los valores mas repetidos
    top = Counter(ticks.tolist()).most_common(5)
    print("\nValores mas frecuentes (util para ver jitter en neutro):")
    for t, n in top:
        print(f"  {t/TICKS_PER_US:8.3f} us  x{n}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("puerto", help="COM5 o /dev/ttyACM0")
    p.add_argument("--canal", type=int, default=0, help="0=steering, 1=throttle")
    p.add_argument("--segundos", type=int, default=60)
    a = p.parse_args()

    try:
        ticks, frames = capturar(a.puerto, a.canal, a.segundos)
    except serial.SerialException as e:
        print(f"Error de puerto: {e}", file=sys.stderr)
        return 1

    analizar(ticks, frames)
    return 0


if __name__ == "__main__":
    sys.exit(main())

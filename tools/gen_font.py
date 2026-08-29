#!/usr/bin/env python3
"""
gen_font.py — genera font5x7.h desde glifos dibujados como arte ASCII.

Se hace asi, y no escribiendo hex a mano, porque los glifos quedan
legibles y verificables. El script se auto-verifica: al final vuelve a
renderizar cada glifo desde los bytes generados y compara con el original.

Cada glifo son 5 columnas x 7 filas. El SSD1306 direcciona por paginas
verticales de 8 px, asi que cada columna se guarda como un byte con el
bit 0 arriba.
"""

GLYPHS = {
' ': """
.....
.....
.....
.....
.....
.....
.....
""",
'!': """
..#..
..#..
..#..
..#..
..#..
.....
..#..
""",
'%': """
##..#
##.#.
...#.
..#..
.#...
#.##.
#.##.
""",
'+': """
.....
..#..
..#..
#####
..#..
..#..
.....
""",
'-': """
.....
.....
.....
#####
.....
.....
.....
""",
'.': """
.....
.....
.....
.....
.....
.##..
.##..
""",
'/': """
....#
....#
...#.
..#..
.#...
#....
#....
""",
'0': """
.###.
#...#
#..##
#.#.#
##..#
#...#
.###.
""",
'1': """
..#..
.##..
..#..
..#..
..#..
..#..
.###.
""",
'2': """
.###.
#...#
....#
...#.
..#..
.#...
#####
""",
'3': """
#####
...#.
..#..
...#.
....#
#...#
.###.
""",
'4': """
...#.
..##.
.#.#.
#..#.
#####
...#.
...#.
""",
'5': """
#####
#....
####.
....#
....#
#...#
.###.
""",
'6': """
..##.
.#...
#....
####.
#...#
#...#
.###.
""",
'7': """
#####
....#
...#.
..#..
.#...
.#...
.#...
""",
'8': """
.###.
#...#
#...#
.###.
#...#
#...#
.###.
""",
'9': """
.###.
#...#
#...#
.####
....#
...#.
.##..
""",
':': """
.....
.##..
.##..
.....
.##..
.##..
.....
""",
'=': """
.....
.....
#####
.....
#####
.....
.....
""",
'?': """
.###.
#...#
....#
...#.
..#..
.....
..#..
""",
'A': """
.###.
#...#
#...#
#####
#...#
#...#
#...#
""",
'B': """
####.
#...#
#...#
####.
#...#
#...#
####.
""",
'C': """
.###.
#...#
#....
#....
#....
#...#
.###.
""",
'D': """
###..
#..#.
#...#
#...#
#...#
#..#.
###..
""",
'E': """
#####
#....
#....
####.
#....
#....
#####
""",
'F': """
#####
#....
#....
####.
#....
#....
#....
""",
'G': """
.###.
#...#
#....
#.###
#...#
#...#
.####
""",
'H': """
#...#
#...#
#...#
#####
#...#
#...#
#...#
""",
'I': """
.###.
..#..
..#..
..#..
..#..
..#..
.###.
""",
'J': """
..###
...#.
...#.
...#.
...#.
#..#.
.##..
""",
'K': """
#...#
#..#.
#.#..
##...
#.#..
#..#.
#...#
""",
'L': """
#....
#....
#....
#....
#....
#....
#####
""",
'M': """
#...#
##.##
#.#.#
#.#.#
#...#
#...#
#...#
""",
'N': """
#...#
#...#
##..#
#.#.#
#..##
#...#
#...#
""",
'O': """
.###.
#...#
#...#
#...#
#...#
#...#
.###.
""",
'P': """
####.
#...#
#...#
####.
#....
#....
#....
""",
'Q': """
.###.
#...#
#...#
#...#
#.#.#
#..#.
.##.#
""",
'R': """
####.
#...#
#...#
####.
#.#..
#..#.
#...#
""",
'S': """
.####
#....
#....
.###.
....#
....#
####.
""",
'T': """
#####
..#..
..#..
..#..
..#..
..#..
..#..
""",
'U': """
#...#
#...#
#...#
#...#
#...#
#...#
.###.
""",
'V': """
#...#
#...#
#...#
#...#
#...#
.#.#.
..#..
""",
'W': """
#...#
#...#
#...#
#.#.#
#.#.#
##.##
#...#
""",
'X': """
#...#
#...#
.#.#.
..#..
.#.#.
#...#
#...#
""",
'Y': """
#...#
#...#
.#.#.
..#..
..#..
..#..
..#..
""",
'Z': """
#####
....#
...#.
..#..
.#...
#....
#####
""",
'[': """
.###.
.#...
.#...
.#...
.#...
.#...
.###.
""",
']': """
.###.
...#.
...#.
...#.
...#.
...#.
.###.
""",
'|': """
..#..
..#..
..#..
..#..
..#..
..#..
..#..
""",
}

FIRST = 0x20   # espacio
LAST  = 0x7C   # |  (0x5E-0x7B quedan en blanco; minusculas se mapean a mayusculas al dibujar)


def parse(art):
    rows = [r for r in art.split("\n") if r.strip("") != "" and len(r) == 5]
    assert len(rows) == 7, f"glifo con {len(rows)} filas, esperaba 7"
    return rows


def to_columns(rows):
    """Convierte 7 filas de 5 chars en 5 bytes (bit 0 = fila superior)."""
    cols = []
    for x in range(5):
        b = 0
        for y in range(7):
            if rows[y][x] == '#':
                b |= (1 << y)
        cols.append(b)
    return cols


def from_columns(cols):
    """Inversa, para verificar."""
    rows = []
    for y in range(7):
        rows.append("".join('#' if (cols[x] >> y) & 1 else '.' for x in range(5)))
    return rows


def main():
    table = []
    errores = 0

    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        art = GLYPHS.get(ch)
        if art is None:
            cols = [0, 0, 0, 0, 0]
            table.append((ch, cols))
            continue

        rows = parse(art)
        cols = to_columns(rows)

        # verificacion ida y vuelta
        if from_columns(cols) != rows:
            print(f"ERROR de round-trip en '{ch}'")
            errores += 1

        table.append((ch, cols))

    if errores:
        raise SystemExit(f"{errores} glifos con error")

    lines = []
    lines.append("/* font5x7.h — generado por tools/gen_font.py. No editar a mano. */")
    lines.append("#ifndef FONT5X7_H")
    lines.append("#define FONT5X7_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define FONT_FIRST_CHAR 0x{FIRST:02X}")
    lines.append(f"#define FONT_LAST_CHAR  0x{LAST:02X}")
    lines.append("#define FONT_WIDTH      5")
    lines.append("#define FONT_HEIGHT     7")
    lines.append("")
    lines.append("/* 5 columnas por glifo, bit 0 = fila superior. */")
    lines.append("static const uint8_t font5x7[][FONT_WIDTH] = {")
    for ch, cols in table:
        hexs = ", ".join(f"0x{c:02X}" for c in cols)
        etiqueta = "espacio" if ch == ' ' else ch
        lines.append(f"    {{ {hexs} }},   /* {etiqueta} */")
    lines.append("};")
    lines.append("")
    lines.append("#endif")

    with open("font5x7.h", "w") as f:
        f.write("\n".join(lines) + "\n")

    definidos = sum(1 for ch, _ in table if ch in GLYPHS)
    print(f"OK: {len(table)} entradas, {definidos} glifos dibujados, "
          f"round-trip verificado.")

    # muestra visual para inspeccion humana
    print("\nMuestra:")
    for palabra in ["ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789+-.:", "[]|%?!/="]:
        filas = ["" for _ in range(7)]
        for ch in palabra:
            cols = table[ord(ch) - FIRST][1]
            g = from_columns(cols)
            for y in range(7):
                filas[y] += g[y] + " "
        print()
        for r in filas:
            print("  " + r.replace('#', '\u2588').replace('.', ' '))


if __name__ == "__main__":
    main()

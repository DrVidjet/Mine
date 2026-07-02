#!/usr/bin/env python3
"""
Сборщик атласа текстур для одного блока.

Использование:
    python3 make_atlas.py <выход.png> <верх.png> <бок.png> <низ.png>

Пример:
    python3 make_atlas.py textures/atlas_grass.png textures/grasstop.png textures/grass.png textures/dirt.png
    python3 make_atlas.py textures/atlas_dirt.png  textures/dirt.png     textures/dirt.png  textures/dirt.png

Результат — PNG из 4 тайлов в ряд:
    [верх][бок][низ][пусто]

UV в коде:
    top    = { 0.00f, 0.0f, 0.25f, 1.0f }
    side   = { 0.25f, 0.0f, 0.50f, 1.0f }
    bottom = { 0.50f, 0.0f, 0.75f, 1.0f }
"""

import sys
import os
from PIL import Image


def make_atlas(out_path, top_path, side_path, bottom_path):
    paths = {"верх": top_path, "бок": side_path, "низ": bottom_path}
    for label, path in paths.items():
        if not os.path.exists(path):
            print(f"Ошибка: файл не найден ({label}) — {path}")
            sys.exit(1)

    top    = Image.open(top_path).convert("RGBA")
    side   = Image.open(side_path).convert("RGBA")
    bottom = Image.open(bottom_path).convert("RGBA")

    if not (top.size == side.size == bottom.size):
        print(f"Ошибка: текстуры разного размера")
        print(f"  верх: {top.size}  бок: {side.size}  низ: {bottom.size}")
        sys.exit(1)

    W, H = top.size

    # 4 тайла в ряд, 4-й пустой — зарезервирован под будущее
    atlas = Image.new("RGBA", (W * 4, H), (0, 0, 0, 0))
    atlas.paste(top,    (W * 0, 0))
    atlas.paste(side,   (W * 1, 0))
    atlas.paste(bottom, (W * 2, 0))

    os.makedirs(os.path.dirname(out_path) if os.path.dirname(out_path) else ".", exist_ok=True)
    atlas.save(out_path)

    print(f"Атлас сохранён: {out_path}  ({W*4}x{H} px, тайл {W}x{H})")
    print(f"  тайл 0 (u 0.00–0.25) — верх  ← {top_path}")
    print(f"  тайл 1 (u 0.25–0.50) — бок   ← {side_path}")
    print(f"  тайл 2 (u 0.50–0.75) — низ   ← {bottom_path}")
    print(f"  тайл 3 (u 0.75–1.00) — пусто (зарезервирован)")


def print_usage():
    print("Использование:")
    print("  python3 make_atlas.py <выход.png> <верх.png> <бок.png> <низ.png>")
    print()
    print("Примеры:")
    print("  python3 make_atlas.py textures/atlas_grass.png textures/grasstop.png textures/grass.png textures/dirt.png")
    print("  python3 make_atlas.py textures/atlas_dirt.png  textures/dirt.png     textures/dirt.png  textures/dirt.png")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print(f"Ошибка: ожидается 4 аргумента, получено {len(sys.argv) - 1}")
        print()
        print_usage()
        sys.exit(1)

    _, out_path, top_path, side_path, bottom_path = sys.argv
    make_atlas(out_path, top_path, side_path, bottom_path)

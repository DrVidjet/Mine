#!/usr/bin/env python3
"""
Процедурная генерация текстур дерева (кора, кольца среза, листва) —
16x16 пиксель-арт в той же стилистике, что и существующие текстуры
(grass/dirt/stone) — базовый цвет + шумовой спекл.

Использование:
    python3 make_tree_textures.py

Результат: textures/log_side.png, textures/log_top.png, textures/leaves.png
Детерминировано (фиксированные seed), чтобы повторный запуск давал тот же результат.
"""
import random
from PIL import Image

SIZE = 16


def make_bark():
    random.seed(11)
    base_dark  = (61, 43, 27)
    base_mid   = (78, 56, 36)
    base_light = (94, 68, 44)
    groove     = (46, 32, 20)

    im = Image.new("RGB", (SIZE, SIZE))
    px = im.load()
    # Вертикальные полосы коры с неровными бороздками через каждые ~3 пикселя
    for x in range(SIZE):
        groove_col = (x % 3 == 0)
        for y in range(SIZE):
            jitter = random.random()
            if groove_col and jitter < 0.6:
                c = groove
            elif jitter < 0.15:
                c = base_light
            elif jitter < 0.55:
                c = base_mid
            else:
                c = base_dark
            px[x, y] = c
    return im


def make_log_top():
    random.seed(22)
    ring_colors = [(196, 157, 102), (176, 138, 88), (156, 118, 74), (134, 98, 60)]
    bark = (61, 43, 27)
    cx, cy = (SIZE - 1) / 2, (SIZE - 1) / 2

    im = Image.new("RGB", (SIZE, SIZE))
    px = im.load()
    for y in range(SIZE):
        for x in range(SIZE):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d > 7.3:
                c = bark  # кора по краю среза
            else:
                ring = int(d * 1.6) % len(ring_colors)
                if random.random() < 0.12:  # лёгкий шум, чтобы кольца не были идеально ровными
                    ring = (ring + 1) % len(ring_colors)
                c = ring_colors[ring]
            px[x, y] = c
    return im


def make_leaves():
    random.seed(33)
    variants = [(46, 92, 38), (56, 107, 46), (38, 79, 32), (66, 120, 54), (30, 66, 26)]
    weights  = [30, 28, 20, 14, 8]

    im = Image.new("RGB", (SIZE, SIZE))
    px = im.load()
    for y in range(SIZE):
        for x in range(SIZE):
            px[x, y] = random.choices(variants, weights=weights)[0]
    return im


if __name__ == "__main__":
    make_bark().save("textures/log_side.png")
    make_log_top().save("textures/log_top.png")
    make_leaves().save("textures/leaves.png")
    print("Текстуры сохранены: textures/log_side.png, textures/log_top.png, textures/leaves.png")

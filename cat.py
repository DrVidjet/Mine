from PIL import Image
import sys
import os

# Пути к текстурам — измени если у тебя другие имена файлов
TOP    = "textures/grasstop.png"
SIDE   = "textures/grass.png"
BOTTOM = "textures/dirt.png"

# Проверяем что все файлы существуют
for path in [TOP, SIDE, BOTTOM]:
    if not os.path.exists(path):
        print(f"Ошибка: файл не найден — {path}")
        sys.exit(1)

top    = Image.open(TOP).convert("RGBA")
side   = Image.open(SIDE).convert("RGBA")
bottom = Image.open(BOTTOM).convert("RGBA")

# Все текстуры должны быть одного размера
if not (top.size == side.size == bottom.size):
    print(f"Ошибка: текстуры разного размера — top:{top.size} side:{side.size} bottom:{bottom.size}")
    sys.exit(1)

W, H = top.size  # размер одного тайла (например 16x16)

# Атлас: 4 тайла в ряд (4-й пустой, зарезервирован под будущее)
# [grasstop][grass_side][dirt][пусто]
atlas = Image.new("RGBA", (W * 4, H), (0, 0, 0, 0))

atlas.paste(top,    (W * 0, 0))  # тайл 0 — верх
atlas.paste(side,   (W * 1, 0))  # тайл 1 — бок
atlas.paste(bottom, (W * 2, 0))  # тайл 2 — низ
# тайл 3 — пустой, под будущие текстуры

out = "textures/atlas.png"
atlas.save(out)
print(f"Атлас сохранён: {out}  ({W*4}x{H} px, тайл {W}x{H})")
print(f"  тайл 0 (x=0..{W-1})   — верх    ({TOP})")
print(f"  тайл 1 (x={W}..{W*2-1}) — бок     ({SIDE})")
print(f"  тайл 2 (x={W*2}..{W*3-1}) — низ     ({BOTTOM})")
print(f"  тайл 3 (x={W*3}..{W*4-1}) — пустой  (зарезервирован)")

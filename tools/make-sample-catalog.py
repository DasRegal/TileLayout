#!/usr/bin/env python3
"""Встроенные каталоги: рисунки плитки, сгенерированные здесь же (без чужих
фотографий), и catalog.json к каждому.

    python3 tools/make-sample-catalog.py [папка]   # по умолчанию data/catalogs

samples — образцы разных материалов и ходовые форматы и виды плитки в магазинах
          РФ (номинальные размеры; калибр у производителей отличается на несколько
          мм — например, 40,2×40,2 или 29,8×59,8, — его можно поправить в программе).

Нужны Pillow и numpy. Рисунки одинаковы при каждом запуске (фиксированные зёрна).
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

LONG_SIDE = 512  # px по длинной стороне плитки


def noise(width, height, scale, seed, octaves=4):
    """Сглаженный шум 0…1 (сумма октав)."""
    rng = np.random.default_rng(seed)
    result = np.zeros((height, width))
    amplitude, total = 1.0, 0.0
    for octave in range(octaves):
        cells = max(2, int(scale * 2 ** octave))
        grid = rng.random((cells + 1, cells + 1))
        layer = Image.fromarray((grid * 255).astype(np.uint8)).resize((width, height), Image.BICUBIC)
        result += amplitude * np.asarray(layer, dtype=float) / 255
        total += amplitude
        amplitude /= 2
    return result / total


def to_image(rgb):
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))


def tint(base, values, strength):
    """Цвет base, меняющийся с values (0…1) на ±strength."""
    shift = (values - 0.5)[..., None] * strength
    return np.asarray(base, dtype=float)[None, None, :] + shift


def marble(width, height, seed, base, vein):
    field = noise(width, height, 3, seed, 5)
    x = np.linspace(0, 6, width)[None, :]
    y = np.linspace(0, 3, height)[:, None]
    veins = np.abs(np.sin((x + y * 0.6 + field * 4) * np.pi))
    veins = np.clip(1 - veins * 5, 0, 1) ** 2
    rgb = tint(base, noise(width, height, 2, seed + 1), 18)
    rgb = rgb * (1 - veins[..., None]) + np.asarray(vein, dtype=float) * veins[..., None]
    return to_image(rgb).filter(ImageFilter.GaussianBlur(0.6))


def concrete(width, height, seed, base):
    rgb = tint(base, noise(width, height, 6, seed, 5), 40)
    speckles = np.random.default_rng(seed).random((height, width)) > 0.995
    rgb[speckles] *= 0.7
    return to_image(rgb)


def wood(width, height, seed, base):
    rng = np.random.default_rng(seed)
    y = np.linspace(0, 1, height)[:, None]
    wobble = noise(width, height, 2, seed, 3) * 0.08
    rings = np.sin((y + wobble) * 2 * np.pi * rng.uniform(9, 13)) * 0.5 + 0.5
    fibers = noise(width, height, 40, seed + 1, 2)
    values = 0.6 * rings + 0.4 * fibers
    rgb = tint(base, values, 55)
    for knot in range(2):  # сучки
        cx, cy = rng.integers(0, width), rng.integers(0, height)
        yy, xx = np.ogrid[:height, :width]
        distance = ((xx - cx) / (height * 0.2)) ** 2 + ((yy - cy) / (height * 0.08)) ** 2
        rgb *= (1 - 0.3 * np.clip(1 - distance, 0, 1))[..., None]
    return to_image(rgb)


def terracotta(width, height, seed, base):
    rgb = tint(base, noise(width, height, 4, seed, 4), 35)
    return to_image(rgb).filter(ImageFilter.GaussianBlur(0.4))


def glossy(width, height, seed, base):
    """«Кабанчик»: ровный цвет, фаска по краю и блик."""
    rgb = tint(base, noise(width, height, 3, seed, 2), 8)
    image = to_image(rgb)
    draw = ImageDraw.Draw(image)
    bevel = max(3, height // 18)
    for i in range(bevel):
        shade = int(30 * (1 - i / bevel))
        draw.rectangle([i, i, width - 1 - i, height - 1 - i],
                       outline=tuple(max(0, c - shade) for c in base))
    glare = Image.new("L", (width, height), 0)
    ImageDraw.Draw(glare).ellipse([width * 0.1, height * 0.12, width * 0.6, height * 0.45], fill=60)
    glare = glare.filter(ImageFilter.GaussianBlur(height / 8))
    return Image.composite(Image.new("RGB", (width, height), (255, 255, 255)), image, glare)


def slate(width, height, seed, base):
    field = noise(width, height, 4, seed, 5)
    layers = np.sin(np.linspace(0, 40, height)[:, None] + field * 6) * 0.5 + 0.5
    rgb = tint(base, 0.6 * field + 0.4 * layers, 35)
    return to_image(rgb)


def travertine(width, height, seed, base):
    rng = np.random.default_rng(seed)
    y = np.linspace(0, 1, height)[:, None]
    bands = np.sin((y + noise(width, height, 2, seed, 3) * 0.1) * 50) * 0.5 + 0.5
    rgb = tint(base, 0.5 * bands + 0.5 * noise(width, height, 5, seed + 1), 30)
    image = to_image(rgb)
    draw = ImageDraw.Draw(image)
    for hole in range(rng.integers(25, 40)):  # поры
        x, y0 = rng.integers(0, width), rng.integers(0, height)
        w, h = rng.integers(4, 18), rng.integers(2, 5)
        draw.ellipse([x, y0, x + w, y0 + h], fill=tuple(int(c * 0.78) for c in base))
    return image


def cement_pattern(width, height, seed, base, ink):
    """Цементная плитка с узором: видно, что рисунок поворачивается с плиткой."""
    image = to_image(tint(base, noise(width, height, 4, seed, 3), 14))
    draw = ImageDraw.Draw(image)
    w, h = width, height
    draw.ellipse([w * 0.3, h * 0.3, w * 0.7, h * 0.7], outline=ink, width=max(2, w // 40))
    for cx, cy in [(0, 0), (w, 0), (0, h), (w, h)]:
        draw.pieslice([cx - w * 0.35, cy - h * 0.35, cx + w * 0.35, cy + h * 0.35], 0, 360, fill=ink)
    draw.polygon([(w * 0.5, h * 0.12), (w * 0.6, h * 0.3), (w * 0.4, h * 0.3)], fill=ink)  # «верх» узора
    return image


def chevron(width, height, seed, base, planks):
    """Керамогранит с печатной «ёлочкой» (шевроном): два ряда досок под 45° вдоль
    длинной стороны сходятся углом посередине; карвинг — тёмные утопленные стыки.
    planks — досок в ряду: шаг укладывается в длину целиком, и рисунок без разрыва
    переходит на соседние плитки (при смещении рядов 0)."""
    rng = np.random.default_rng(seed)
    rows = 2
    row_height = height / rows
    plank = width / planks  # шаг досок вдоль ряда
    yy, xx = np.mgrid[:height, :width].astype(float)
    row = np.minimum((yy // row_height).astype(int), rows - 1)
    local = yy - row * row_height
    sign = np.where(row % 2 == 0, 1.0, -1.0)
    along = xx + sign * np.where(sign > 0, local, local - row_height)  # стыки рядов сходятся углом
    index = np.floor(along / plank).astype(int) + row * 1000
    tones = rng.uniform(-22, 22, size=index.max() + 1)
    grain = noise(width, height, 30, seed + 1, 2)
    fibers = np.sin((along / plank * 16 + grain * 0.8) * np.pi) * 0.5 + 0.5
    rgb = tint(base, 0.55 * fibers + 0.45 * grain, 34) + tones[index][..., None]
    seam = np.minimum(along % plank, plank - along % plank) < 1.2
    seam |= np.minimum(local, row_height - local) < 1.2
    rgb[seam] *= 0.72
    return to_image(rgb).filter(ImageFilter.GaussianBlur(0.5))


def plain(width, height, seed, base, spread=10):
    """Однотонная матовая плитка."""
    return to_image(tint(base, noise(width, height, 5, seed, 3), spread))


def salt_pepper(width, height, seed, base):
    """Технический керамогранит «соль-перец»: мелкие тёмные и светлые крапины."""
    rgb = tint(base, noise(width, height, 4, seed, 3), 14)
    dots = np.random.default_rng(seed).random((height, width))
    rgb[dots > 0.97] *= 0.55
    rgb[dots < 0.02] = np.minimum(rgb[dots < 0.02] * 1.35, 255)
    return to_image(rgb).filter(ImageFilter.GaussianBlur(0.5))


def mosaic(width, height, seed, base, chip_mm, sheet_mm, spread=40):
    """Мозаика на сетке: лист из квадратных чипов с тёмными швами."""
    rng = np.random.default_rng(seed)
    image = Image.new("RGB", (width, height), tuple(int(c * 0.55) for c in base))
    draw = ImageDraw.Draw(image)
    count = round(sheet_mm / chip_mm)
    step, gap = width / count, max(1, width // 160)
    for row in range(count):
        for column in range(count):
            shade = rng.uniform(-spread, spread)
            color = tuple(int(np.clip(c + shade, 0, 255)) for c in base)
            draw.rectangle([column * step + gap, row * step + gap, (column + 1) * step - gap, (row + 1) * step - gap],
                           fill=color)
    return image


SAMPLES = [
    ("marble-white", "Мрамор белый", 600, 300, lambda w, h: marble(w, h, 11, (232, 230, 225), (150, 150, 155))),
    ("marble-grey", "Мрамор серый", 600, 600, lambda w, h: marble(w, h, 12, (170, 172, 176), (90, 92, 98))),
    ("concrete", "Бетон", 600, 600, lambda w, h: concrete(w, h, 13, (150, 148, 144))),
    ("anthracite", "Керамогранит антрацит", 600, 600, lambda w, h: concrete(w, h, 14, (62, 64, 68))),
    ("oak-light", "Дуб светлый", 1200, 200, lambda w, h: wood(w, h, 15, (196, 160, 112))),
    ("oak-dark", "Дуб тёмный", 1200, 200, lambda w, h: wood(w, h, 16, (112, 78, 52))),
    ("terracotta", "Терракота", 300, 300, lambda w, h: terracotta(w, h, 17, (178, 98, 64))),
    ("metro-white", "Кабанчик белый", 200, 100, lambda w, h: glossy(w, h, 18, (238, 238, 234))),
    ("metro-green", "Кабанчик зелёный", 200, 100, lambda w, h: glossy(w, h, 19, (92, 140, 120))),
    ("slate", "Сланец", 600, 300, lambda w, h: slate(w, h, 20, (78, 82, 86))),
    ("travertine", "Травертин", 600, 300, lambda w, h: travertine(w, h, 21, (214, 196, 166))),
    ("cement-pattern", "Цементная с узором", 200, 200,
     lambda w, h: cement_pattern(w, h, 22, (226, 220, 206), (44, 76, 112))),
]

WHITE, CREAM, BEIGE, GREY, DARK = (238, 238, 234), (232, 224, 206), (210, 192, 162), (168, 168, 166), (60, 62, 66)

# Названия — как на ценниках: вид, формат в сантиметрах, рисунок и цвет.
RU_MARKET = [
    # Керамогранит для пола и стен
    ("kg-60x60-concrete-grey", "Керамогранит 60×60 бетон серый", 600, 600, lambda w, h: concrete(w, h, 101, (150, 148, 144))),
    ("kg-60x60-concrete-light", "Керамогранит 60×60 бетон светлый", 600, 600, lambda w, h: concrete(w, h, 102, (196, 192, 186))),
    ("kg-60x60-marble-white", "Керамогранит 60×60 мрамор белый", 600, 600, lambda w, h: marble(w, h, 103, (234, 232, 228), (160, 158, 160))),
    ("kg-60x60-stone-beige", "Керамогранит 60×60 камень бежевый", 600, 600, lambda w, h: travertine(w, h, 104, BEIGE)),
    ("kg-60x60-anthracite", "Керамогранит 60×60 антрацит", 600, 600, lambda w, h: concrete(w, h, 105, DARK)),
    ("kg-60x120-marble-white", "Керамогранит 60×120 мрамор белый", 1200, 600, lambda w, h: marble(w, h, 106, (236, 234, 230), (140, 140, 146))),
    ("kg-60x120-marble-grey", "Керамогранит 60×120 мрамор серый", 1200, 600, lambda w, h: marble(w, h, 107, (160, 162, 166), (80, 82, 88))),
    ("kg-60x120-concrete-grey", "Керамогранит 60×120 бетон серый", 1200, 600, lambda w, h: concrete(w, h, 108, (140, 138, 134))),
    ("kg-60x120-travertine", "Керамогранит 60×120 травертин", 1200, 600, lambda w, h: travertine(w, h, 109, (218, 202, 172))),
    ("kg-80x80-marble-beige", "Керамогранит 80×80 мрамор бежевый", 800, 800, lambda w, h: marble(w, h, 110, (226, 214, 192), (170, 140, 110))),
    ("kg-30x60-slate", "Керамогранит 30×60 сланец", 600, 300, lambda w, h: slate(w, h, 111, (84, 86, 88))),
    ("kg-30x30-salt-pepper", "Керамогранит 30×30 технический соль-перец", 300, 300, lambda w, h: salt_pepper(w, h, 112, GREY)),
    ("kg-30x30-beige", "Керамогранит 30×30 бежевый", 300, 300, lambda w, h: salt_pepper(w, h, 113, BEIGE)),
    # «Паркет» — керамогранит под дерево
    ("wood-20x120-oak-light", "Керамогранит 20×120 дуб светлый", 1200, 200, lambda w, h: wood(w, h, 120, (200, 166, 120))),
    ("wood-20x120-oak-natural", "Керамогранит 20×120 дуб натуральный", 1200, 200, lambda w, h: wood(w, h, 121, (160, 118, 78))),
    ("wood-20x120-oak-grey", "Керамогранит 20×120 дуб серый", 1200, 200, lambda w, h: wood(w, h, 122, (150, 144, 136))),
    ("wood-20x60-walnut", "Керамогранит 20×60 орех", 600, 200, lambda w, h: wood(w, h, 123, (110, 76, 52))),
    ("wood-15x60-oak-white", "Керамогранит 15×60 дуб беленый", 600, 150, lambda w, h: wood(w, h, 124, (214, 204, 188))),
    ("wood-20x80-ash", "Керамогранит 20×80 ясень", 800, 200, lambda w, h: wood(w, h, 125, (188, 160, 124))),
    ("chevron-light", "Керамогранит 60×120 «ёлочка» Chevron Light", 1200, 600,
     lambda w, h: chevron(w, h, 126, (196, 158, 116), 8)),
    # Напольная керамическая плитка
    ("floor-33x33-grey", "Плитка напольная 33×33 серая", 330, 330, lambda w, h: salt_pepper(w, h, 130, (150, 150, 150))),
    ("floor-40x40-beige", "Плитка напольная 40×40 бежевая", 400, 400, lambda w, h: travertine(w, h, 131, (206, 186, 152))),
    ("floor-42x42-brown", "Плитка напольная 42×42 коричневая", 420, 420, lambda w, h: terracotta(w, h, 132, (130, 96, 72))),
    ("floor-45x45-grey", "Плитка напольная 45×45 серый камень", 450, 450, lambda w, h: slate(w, h, 133, (128, 128, 126))),
    ("floor-30x30-terracotta", "Клинкер 30×30 терракота", 300, 300, lambda w, h: terracotta(w, h, 134, (176, 94, 60))),
    # Настенная плитка
    ("wall-20x30-white", "Плитка настенная 20×30 белая глянцевая", 300, 200, lambda w, h: glossy(w, h, 140, WHITE)),
    ("wall-20x30-beige", "Плитка настенная 20×30 бежевая", 300, 200, lambda w, h: plain(w, h, 141, CREAM)),
    ("wall-25x40-white", "Плитка настенная 25×40 белая глянцевая", 400, 250, lambda w, h: glossy(w, h, 142, WHITE)),
    ("wall-25x40-marble", "Плитка настенная 25×40 мрамор бежевый", 400, 250, lambda w, h: marble(w, h, 143, CREAM, (176, 150, 120))),
    ("wall-20x60-white", "Плитка настенная 20×60 белая глянцевая", 600, 200, lambda w, h: glossy(w, h, 144, WHITE)),
    ("wall-30x60-marble-white", "Плитка настенная 30×60 мрамор белый", 600, 300, lambda w, h: marble(w, h, 145, (236, 236, 232), (150, 150, 156))),
    ("wall-30x60-concrete", "Плитка настенная 30×60 бетон серый", 600, 300, lambda w, h: concrete(w, h, 146, (176, 174, 170))),
    ("wall-25x75-marble", "Плитка настенная 25×75 мрамор белый", 750, 250, lambda w, h: marble(w, h, 147, (238, 236, 232), (168, 164, 160))),
    ("wall-30x90-marble-grey", "Плитка настенная 30×90 мрамор серый", 900, 300, lambda w, h: marble(w, h, 148, (196, 196, 198), (100, 102, 108))),
    ("wall-15x15-white", "Плитка настенная 15×15 белая матовая", 150, 150, lambda w, h: plain(w, h, 149, WHITE, 6)),
    ("wall-20x20-pattern", "Плитка 20×20 пэчворк", 200, 200, lambda w, h: cement_pattern(w, h, 150, (228, 222, 208), (40, 70, 110))),
    # «Кабанчик»
    ("metro-7.5x15-white", "Кабанчик 7,5×15 белый", 150, 75, lambda w, h: glossy(w, h, 160, WHITE)),
    ("metro-10x20-white", "Кабанчик 10×20 белый", 200, 100, lambda w, h: glossy(w, h, 161, WHITE)),
    ("metro-10x20-black", "Кабанчик 10×20 чёрный", 200, 100, lambda w, h: glossy(w, h, 162, (40, 40, 42))),
    ("metro-10x30-white", "Кабанчик 10×30 белый", 300, 100, lambda w, h: glossy(w, h, 163, WHITE)),
    ("metro-7.5x30-green", "Кабанчик 7,5×30 зелёный", 300, 75, lambda w, h: glossy(w, h, 164, (86, 130, 112))),
    # Мозаика — размер листа на сетке
    ("mosaic-30x30-white", "Мозаика 30×30 белая (чип 2,5 см)", 300, 300, lambda w, h: mosaic(w, h, 170, WHITE, 25, 300, 8)),
    ("mosaic-30x30-blue", "Мозаика 30×30 голубая для бассейна (чип 2,5 см)", 300, 300,
     lambda w, h: mosaic(w, h, 171, (90, 160, 200), 25, 300)),
    ("mosaic-30x30-grey", "Мозаика 30×30 серый микс (чип 5 см)", 300, 300, lambda w, h: mosaic(w, h, 172, (150, 150, 150), 50, 300, 30)),
]

CATALOGS = [
    ("samples", "Образцы TileLayout", SAMPLES + RU_MARKET),
]


def paint_tile(folder, stem, width_mm, height_mm, paint):
    """Рисунок плитки в folder; имя файла — stem и размер в мм: так плитка узнаётся
    и без catalog.json, если перенести картинку в другую папку. Имя латиницей —
    без забот с кодировкой имён в zip для Windows. Возвращает имя файла и картинку."""
    scale = LONG_SIDE / max(width_mm, height_mm)
    image = paint(round(width_mm * scale), round(height_mm * scale))
    file = f"{stem} {width_mm}x{height_mm}.jpg"
    image.save(os.path.join(folder, file), quality=86, optimize=True)
    return file, image


def clear_images(folder):
    """Старые рисунки генератора (переименованные или удалённые плитки)."""
    for file in os.listdir(folder):
        if file.lower().endswith(".jpg"):
            os.remove(os.path.join(folder, file))


def make_catalog(folder, title, entries):
    os.makedirs(folder, exist_ok=True)
    clear_images(folder)
    tiles = []
    for tile_id, name, width_mm, height_mm, paint in entries:
        file, image = paint_tile(folder, tile_id, width_mm, height_mm, paint)
        mean = np.asarray(image, dtype=float).reshape(-1, 3).mean(axis=0)
        tiles.append({"id": tile_id, "name": name, "width": width_mm, "height": height_mm, "image": file,
                      "color": "#%02x%02x%02x" % tuple(int(round(c)) for c in mean)})
    with open(os.path.join(folder, "catalog.json"), "w", encoding="utf-8") as out:
        json.dump({"name": title, "tiles": tiles}, out, ensure_ascii=False, indent=1)
        out.write("\n")
    print(f"{len(tiles)} плиток в {folder}")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join("data", "catalogs")
    for folder, title, entries in CATALOGS:
        make_catalog(os.path.join(root, folder), title, entries)


if __name__ == "__main__":
    main()

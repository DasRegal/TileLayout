/*
 * Панно — картинка из нескольких плиток (columns × rows), продаётся комплектом и кладётся
 * целиком, без подрезок: основная плитка его обходит со швом (её шов — между ними). Картинка
 * (texture) растянута на всё панно, каждая плитка — своя часть картинки.
 *
 * Панно — на стене или на полу комнаты:
 *   на стене wall (сторона tl_space_wall_outline) — at: левый нижний угол, x от левого угла
 *   стены (как видно из комнаты), h от пола;
 *   на полу (wall = TL_PANNO_FLOOR) — at: центр панно — сдвиг от центра комнаты в осях её
 *   раскладки (как у ковра).
 * Клей, затирка, грунт — как у плитки вокруг; укладка — по её цене; панно — своей строкой сметы.
 */
#ifndef TL_PANNO_H
#define TL_PANNO_H

#include <stddef.h>

#include "tl_geometry.h"
#include "tl_tile_settings.h"

#define TL_PANNO_FLOOR ((size_t)-1)
#define TL_PANNO_MAX_TILES 64 /* плиток в панно по каждой стороне */

typedef struct TlPanno {
    size_t wall;
    TlVec2 at;
    int columns, rows;
    double tile_width, tile_height;  /* плитка панно, мм */
    double joint_width;
    char color[TL_COLOR_LENGTH];
    char texture[TL_TEXTURE_LENGTH]; /* картинка на всё панно; пусто — цвет */
    double price;                    /* ₽ за комплект */
} TlPanno;

TL_ARRAY_DEFINE(TlPannoList, TlPanno, tl_panno_list)

/* Панно 2 × 2 плитки 300 × 600 на стене wall (TL_PANNO_FLOOR — на полу). */
TlPanno tl_panno_default(size_t wall);
/* Размер панно со швами между плитками, мм. */
double tl_panno_width(const TlPanno *panno);
double tl_panno_height(const TlPanno *panno);
/* «Панно 600×1200 мм (2×2 плитки 300×600)». */
void tl_panno_name(const TlPanno *panno, char *text, size_t size);
/* Плитка панно для раскладки и материалов: клей, затирка и прочее — как у плитки вокруг (around). */
TlTileSettings tl_panno_tile(const TlPanno *panno, const TlTileSettings *around);
/* Числа в допустимых пределах (плиток 1…TL_PANNO_MAX_TILES, размеры > 0). */
void tl_panno_clamp(TlPanno *panno);

#endif

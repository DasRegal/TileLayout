/*
 * Облицовка стен комнаты плиткой.
 *
 * Стены комнаты — стороны её контура без пола под дверьми (tl_space_wall_outline):
 * у перегородки между двумя комнатами каждая сторона — стена своей комнаты.
 * Высота облицовки — до height (0 — до потолка, высоты помещения). По высоте стена
 * делится на полосы снизу вверх:
 *   низ   (lower)  — другой плиткой до своей высоты (width — высота полосы);
 *   фриз  (frieze) — один ряд узкой плитки над низом или, без низа, на высоте dy от пола; высота
 *                    фриза — высота его плитки;
 *   основная плитка (main) — всё остальное до высоты облицовки.
 * У каждой полосы своя плитка, рисунок, цвет, цены (как у зон пола, tl_zone_tile);
 * клей, затирка, СВП — как у пола комнаты. Ряды кладутся от низа полосы — у пола
 * целый ряд; по стене — от левого угла (смещение — в настройках плитки полосы).
 */
#ifndef TL_WALL_H
#define TL_WALL_H

#include "tl_zone.h"

#define TL_ROOM_HEIGHT 2700.0 /* высота помещения по умолчанию, мм */

typedef enum TlWallBand { TL_WALL_LOWER, TL_WALL_FRIEZE, TL_WALL_MAIN, TL_WALL_BAND_COUNT } TlWallBand;

typedef struct TlWallFinish {
    bool enabled;
    double height;           /* до какой высоты облицовка, мм; 0 — до потолка */
    TlZone bands[TL_WALL_BAND_COUNT]; /* у main — только плитка; у lower — enabled, width, fit; у frieze —
                                         enabled, dy (высота — у плитки) */
} TlWallFinish;

TlWallFinish tl_wall_finish_factory(void);
const char *tl_wall_band_key(TlWallBand band);   /* "lower", "frieze", "main" — в файле проекта */
const char *tl_wall_band_name(TlWallBand band);  /* «низ», «фриз», «стены» */
bool tl_wall_finishes_equal(const TlWallFinish *a, const TlWallFinish *b);

/* Полоса над полосой — через шов: он у верха нижней полосы, сверх её плитки (по целой плитке высота
   низа — ряды со швом над каждым: 4 ряда 300 со швом 2 — 1208; фриз 100 — 102). */
/* Участки полосы по высоте: spans[i] = {низ, верх}, мм от пола; ответ — сколько (0 — полосы нет).
   У основной плитки их два, если фриз висит без низа: под фризом и над ним. */
size_t tl_wall_band_spans(const TlWallFinish *finish, TlWallBand band, double room_height, double spans[2][2]);

#endif

/*
 * Зоны пола комнаты — участки со своей плиткой:
 *   бордюр — полоса шириной width вдоль стен комнаты; у каждой стены своя
 *            раскладка вдоль неё (первый ряд — от стены), углы — «на ус»;
 *   ковёр (панно) — прямоугольник width × height в осях раскладки комнаты,
 *            центр — в центре комнаты со сдвигом (dx, dy).
 * Основной пол комнаты — без зон; между зоной и основным полом — шов.
 *
 * У зоны свои размер, шов, рисунок, цвет, поворот, смещение, запас и цены
 * плитки и укладки; клей, СВП, затирка, грунт — как у комнаты (tl_zone_tile).
 * Зоны — свои у каждой комнаты, даже при общем поле с другой.
 */
#ifndef TL_ZONE_H
#define TL_ZONE_H

#include "tl_tile_settings.h"

typedef enum TlZoneKind { TL_ZONE_BORDER, TL_ZONE_CARPET, TL_ZONE_COUNT } TlZoneKind;

typedef struct TlZone {
    bool enabled;
    double width;          /* бордюр — ширина полосы; ковёр — размер по X осей раскладки комнаты, мм */
    double height;         /* ковёр — размер по Y */
    double dx, dy;         /* ковёр — сдвиг центра от центра комнаты */
    bool fit;              /* размер — по целой плитке: n плиток со швами (ближайшее n) */
    TlTileSettings tile;   /* свои поля — tl_zone_field_is_own */
} TlZone;

/* Заводские зоны: выключены; бордюр 300 мм плиткой 300×300, ковёр 1200×1200 шахматкой. */
TlZone tl_zone_factory(TlZoneKind kind);
const char *tl_zone_key(TlZoneKind kind);   /* "border", "carpet" — в файле проекта */
const char *tl_zone_name(TlZoneKind kind);  /* «Бордюр», «Ковёр» */

/* Размер под n целых плиток со швами: n·(плитка + шов) − шов, ближайший к size, n ≥ 1. */
double tl_zone_fit_size(double size, double tile, double gap);
/* Зона, как её кладут: с fit размеры подогнаны под плитку (у прямой и шахматки; бордюр — по
   ширине плитки поперёк полосы, ковёр — по сторонам плитки с учётом поворота на 90°). */
TlZone tl_zone_effective(const TlZone *zone, TlZoneKind kind);

/* Поле настроек плитки — своё у зоны (иначе — как у комнаты). */
bool tl_zone_field_is_own(const char *key);
/* Настройки плитки зоны: комнаты, а свои поля — зоны. */
TlTileSettings tl_zone_tile(const TlZone *zone, const TlTileSettings *room);
bool tl_zones_equal(const TlZone *a, const TlZone *b);

#endif

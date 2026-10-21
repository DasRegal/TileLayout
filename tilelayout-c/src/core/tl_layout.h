/*
 * Раскладка плитки в комнате.
 *
 * Сетка строится в «осях раскладки»: начало — левый верхний угол габарита
 * комнаты (для общего пола — комнаты-источника), ось X повёрнута на угол
 * раскладки. Плитка (column, row) лежит в
 *     x = offset_x + column·(w + шов) + сдвиг ряда,  y = offset_y + row·(h + шов).
 * Каждая плитка, задевающая пол, — целая, подрезка или узкая подрезка.
 */
#ifndef TL_LAYOUT_H
#define TL_LAYOUT_H

#include "tl_space.h"

#define TL_LAYOUT_MAX_TILES 150000

typedef enum TlTileKind { TL_TILE_WHOLE, TL_TILE_CUT, TL_TILE_NARROW } TlTileKind;

typedef enum TlLayoutCorner {
    TL_CORNER_TOP_LEFT,
    TL_CORNER_TOP_RIGHT,
    TL_CORNER_BOTTOM_LEFT,
    TL_CORNER_BOTTOM_RIGHT
} TlLayoutCorner;

typedef struct TlCell {
    int column, row;
} TlCell;

typedef struct TlTile {
    TlRect rect;           /* в осях раскладки */
    TlCell cell;
    TlTileKind kind;
    int number;            /* номер на плане (см. tl_report_assign_numbers) */
    TlVec2 center;         /* центр плитки или куска, в осях раскладки */
    double piece_width;    /* габарит куска по осям плитки */
    double piece_height;
    bool figured;          /* кусок не прямоугольный */
    TlPolygonList shapes;  /* контуры куска от угла плитки: первый — внешний, далее дыры */
} TlTile;

TL_ARRAY_DEFINE(TlTileList, TlTile, tl_tile_list)

typedef struct TlLayout {
    TlTileList tiles;
    int whole_count, cut_count, narrow_count;
    TlAffine local_to_world;
    TlRect local_bounds;
    double floor_area;           /* мм² пола под плитку */
    TlPolygon clip_points;       /* точки СВП, в осях раскладки */
    TlPolygon floor_outline;     /* пол под плитку (с отступом от стен), в осях раскладки */
    TlPolygonList floor_holes;
    bool too_many;               /* плитка слишком мелкая для такой комнаты */
} TlLayout;

typedef struct TlLayoutOptions {
    bool fast;                   /* без контуров кусков и СВП — для перебора смещений */
    bool use_offset;             /* смещение из offset_x/offset_y вместо настроек */
    double offset_x, offset_y;
} TlLayoutOptions;

struct TlLayoutCache;

TlLayout *tl_layout_compute(TlSpace *space, const TlLayoutOptions *options);
void tl_layout_free(TlLayout *layout);
/* Раскладка комнаты из кэша (пересчитывается, если комната изменилась). */
TlLayout *tl_space_layout(TlSpace *space);
void tl_layout_cache_free(struct TlLayoutCache *cache);

int tl_layout_total(const TlLayout *layout);
TlTile *tl_layout_tile_at(TlLayout *layout, TlCell cell);

double tl_layout_glue_kg(TlSpace *space, const TlLayout *layout);
int tl_layout_glue_bags(TlSpace *space, const TlLayout *layout);
int tl_layout_clip_packs(TlSpace *space, const TlLayout *layout);

/* ---- смещение сетки ------------------------------------------------------- */

/* К одному шагу сетки, не меняя рисунок (при сдвиге рядов компенсирует X). */
void tl_layout_normalize_offsets(const TlTileSettings *tile, double *offset_x, double *offset_y);
/* Крайний угол комнаты в осях раскладки. */
TlVec2 tl_layout_local_corner(TlSpace *space, TlLayoutCorner corner);
/* Целая плитка в выбранном углу (с учётом отступа от стены). */
void tl_layout_corner_offsets(TlSpace *space, TlLayoutCorner corner, double *offset_x, double *offset_y);
/* Центр комнаты — в центре плитки или на пересечении швов. */
void tl_layout_center_offsets(TlSpace *space, bool on_joint, double *offset_x, double *offset_y);
/* Смещение с минимумом узких подрезок, затем плиток. false — перебор невозможен. */
bool tl_layout_optimize_narrow(TlSpace *space, double *offset_x, double *offset_y);

#endif

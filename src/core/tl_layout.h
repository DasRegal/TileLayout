/*
 * Раскладка плитки в комнате.
 *
 * Сетка строится в «осях раскладки»: начало — левый верхний угол габарита
 * комнаты (для общего пола — комнаты-источника), ось X повёрнута на угол
 * раскладки. Плитки кладутся по рисунку укладки (tl_pattern.h), его начало —
 * в (offset_x, offset_y); у прямой плитка (column, row) лежит в
 *     x = offset_x + column·(w + шов) + сдвиг ряда,  y = offset_y + row·(h + шов).
 * Каждая плитка, задевающая пол, — целая, подрезка или узкая подрезка.
 * Деформационный шов вырезает из пола полосу: плитку, через которую он
 * проходит, режет на куски по обе стороны от шва. Труба (tl_pipe.h) сверлит
 * плитку: у плитки — отверстия (drills).
 *
 * Раскладка не знает, что она на полу: поверхность (TlSurface) — контур с
 * дырами, оси и настройки плитки. Пол комнаты, зона пола (бордюр вдоль стены,
 * ковёр — tl_zone.h) и в будущем развёртка стены раскладываются одинаково.
 */
#ifndef TL_LAYOUT_H
#define TL_LAYOUT_H

#include "tl_pattern.h"
#include "tl_space.h"

#define TL_LAYOUT_MAX_TILES 150000

typedef enum TlTileKind { TL_TILE_WHOLE, TL_TILE_CUT, TL_TILE_NARROW } TlTileKind;

typedef enum TlLayoutCorner {
    TL_CORNER_TOP_LEFT,
    TL_CORNER_TOP_RIGHT,
    TL_CORNER_BOTTOM_LEFT,
    TL_CORNER_BOTTOM_RIGHT
} TlLayoutCorner;

typedef struct TlTile {
    TlRect rect;           /* в осях раскладки */
    TlCell cell;
    int spec;              /* вид плитки — layout->pattern.specs[spec] */
    bool turned;           /* лежит поперёк своего вида (ёлочка, модульная) */
    TlTileKind kind;
    int number;            /* номер на плане (см. tl_report_assign_numbers) */
    TlVec2 center;         /* центр плитки или куска, в осях раскладки */
    double piece_width;    /* габарит куска по осям раскладки */
    double piece_height;
    bool figured;          /* кусок не прямоугольный */
    TlPolygonList shapes;  /* контуры куска от угла плитки: первый — внешний, далее дыры */
    TlCircleList drills;   /* отверстия под трубы: центр — от угла плитки, в осях раскладки */
    TlPolygon outline;     /* плитка-многоугольник (шеврон, вставка) от угла габарита; пусто — прямоугольник */
} TlTile;

TL_ARRAY_DEFINE(TlTileList, TlTile, tl_tile_list)

typedef struct TlLayout {
    TlTileList tiles;
    int whole_count, cut_count, narrow_count;
    TlPatternUnit pattern;       /* рисунок: виды плитки — pattern.specs */
    TlAffine local_to_world;
    TlRect local_bounds;
    double floor_area;           /* мм² пола под плитку */
    TlPolygon clip_points;       /* точки СВП, в осях раскладки */
    TlPolygon floor_outline;     /* пол под плитку (с отступом от стен), в осях раскладки */
    TlPolygonList floor_holes;
    TlPolygonList joint_strips;  /* полосы деформационных швов, в осях раскладки */
    bool too_many;               /* плитка слишком мелкая для такой комнаты */
} TlLayout;

typedef struct TlLayoutOptions {
    bool fast;                   /* без контуров кусков и СВП — для перебора смещений */
    bool use_offset;             /* смещение из offset_x/offset_y вместо настроек */
    double offset_x, offset_y;
} TlLayoutOptions;

/* Поверхность под плитку (всё — в мире). */
typedef struct TlSurface {
    const TlPolygon *outline;
    const double *insets;               /* отступ плитки от каждой стороны контура, мм; NULL — от стен
                                           wall_gap, у стыков в проёмах (threshold_strips) — 0 */
    const TlPolygonList *holes;         /* перегородки — плитка отступает от них на wall_gap */
    const TlPolygonList *cutouts;       /* вырезанные зоны — как есть (шов к ним уже учтён) */
    const TlPolygonList *joint_strips;  /* деформационные швы */
    const TlPolygonList *threshold_strips;
    const TlCircleList *pipes;
    TlVec2 origin;                      /* оси раскладки: начало */
    double angle;                       /* направление оси X, ° (поворот плитки — сверх него) */
    const TlTileSettings *tile;
} TlSurface;

struct TlLayoutCache;

TlLayout *tl_layout_compute_surface(const TlSurface *surface, const TlLayoutOptions *options);
/* Основной пол комнаты — без зон. */
TlLayout *tl_layout_compute(TlSpace *space, const TlLayoutOptions *options);
void tl_layout_free(TlLayout *layout);
/* Основная раскладка комнаты из кэша (пересчитывается, если комната изменилась). */
TlLayout *tl_space_layout(TlSpace *space);

/* ---- зоны пола (tl_space_zones.c) -------------------------------------------------
 * Раскладки комнаты по номерам: 0 — основной пол, дальше — зоны: бордюр (своя
 * раскладка у каждой стены), ковёр. */

size_t tl_space_layout_count(TlSpace *space);
TlLayout *tl_space_layout_at(TlSpace *space, size_t index);
/* Настройки плитки раскладки index (у зоны — tl_zone_tile). */
const TlTileSettings *tl_space_layout_tile(TlSpace *space, size_t index);
/* Чья раскладка: зона или TL_ZONE_COUNT — основной пол. */
TlZoneKind tl_space_layout_zone(TlSpace *space, size_t index);
/* Плиток на укладку в комнате — основной пол и зоны. */
int tl_space_tile_total(TlSpace *space);
/* Что зоны вырезают из основного пола (со швом основной плитки). */
TlPolygonList tl_space_zone_cutouts(TlSpace *space);
void tl_zone_cache_free(struct TlZoneCache *cache);
void tl_layout_cache_free(struct TlLayoutCache *cache);

int tl_layout_total(const TlLayout *layout);
TlTile *tl_layout_tile_at(TlLayout *layout, TlCell cell);
const TlTileSpec *tl_layout_spec(const TlLayout *layout, const TlTile *tile);
/* Габарит куска по осям вида плитки (у повёрнутой плитки — piece_height и piece_width). */
void tl_tile_piece_size(const TlTile *tile, double *width, double *height);
/* Плитка как лежит — по осям вида (laid_width × laid_height). */
void tl_tile_laid_size(const TlLayout *layout, const TlTile *tile, double *width, double *height);

/* ---- материалы ---------------------------------------------------------------- */

/* Материалы — по настройкам плитки раскладки (tl_space_layout_tile). */
double tl_layout_glue_kg(const TlTileSettings *tile, const TlLayout *layout);
int tl_layout_glue_bags(const TlTileSettings *tile, const TlLayout *layout);
int tl_layout_clip_packs(const TlTileSettings *tile, const TlLayout *layout);
/* Длина швов, мм: у каждой плитки (куска) — половина периметра, у соседей шов общий. */
double tl_layout_joint_length(const TlLayout *layout);
/* Затирка: длина швов × ширина шва × толщина плитки × плотность. */
double tl_layout_grout_kg(const TlTileSettings *tile, const TlLayout *layout);
/* Грунт: площадь под плитку × расход. */
double tl_layout_primer_l(const TlTileSettings *tile, const TlLayout *layout);

/* ---- смещение сетки ------------------------------------------------------- */

/* Из мира — в оси раскладки комнаты. */
TlAffine tl_layout_world_to_local(TlSpace *space);
/* К одному модулю рисунка, не меняя рисунок (при сдвиге рядов компенсирует X). */
void tl_layout_normalize_offsets(const TlTileSettings *tile, double *offset_x, double *offset_y);
/* Крайний угол комнаты в осях раскладки. */
TlVec2 tl_layout_local_corner(TlSpace *space, TlLayoutCorner corner);
/* Целая плитка в выбранном углу (с учётом отступа от стены). */
void tl_layout_corner_offsets(TlSpace *space, TlLayoutCorner corner, double *offset_x, double *offset_y);
/* Центр комнаты — в центре плитки или на пересечении швов. */
void tl_layout_center_offsets(TlSpace *space, bool on_joint, double *offset_x, double *offset_y);
/* Смещение с минимумом узких подрезок, затем плиток. false — перебор невозможен. */
bool tl_layout_optimize_narrow(TlSpace *space, double *offset_x, double *offset_y);

/* ---- раскладка от линии (tl_layout_align.c) -------------------------------------
 * Линия from—to в мире: порог двери, ось ниши или унитаза (линейка). Линия должна
 * идти вдоль оси раскладки (допуск — несколько градусов); иначе — false. */

typedef enum TlLineAlign {
    TL_LINE_JOINT,      /* по линии — шов */
    TL_LINE_TILE_EDGE,  /* от линии — целая плитка (в сторону side) */
    TL_LINE_TILE_CENTER /* по линии — середина плитки */
} TlLineAlign;

/* Смещение по одной оси раскладки так, чтобы линия легла по mode. side — точка
   по ту сторону, куда идёт целая плитка (для TL_LINE_TILE_EDGE). Вторая ось не меняется. */
bool tl_layout_line_offsets(TlSpace *space, TlVec2 from, TlVec2 to, TlLineAlign mode, TlVec2 side,
                            double *offset_x, double *offset_y);
/* От двери: от порога в комнату — целый ряд, проём — посередине плитки или шва
   (что даёт у косяков куски шире). threshold_from/to — край пола комнаты в проёме
   (tl_project_space_door_edge). */
bool tl_layout_door_offsets(TlSpace *space, TlVec2 threshold_from, TlVec2 threshold_to, double *offset_x,
                            double *offset_y);

#endif

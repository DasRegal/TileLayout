/*
 * Комната — замкнутая область пола. Геометрия (outline, holes, house)
 * вычисляется проектом из стен и перегородок; комната хранит название,
 * настройки плитки и источник общего пола.
 */
#ifndef TL_SPACE_H
#define TL_SPACE_H

#include "tl_fixture.h"
#include "tl_outline.h"
#include "tl_panno.h"
#include "tl_pipe.h"
#include "tl_regions.h"
#include "tl_tile_settings.h"
#include "tl_wall.h"
#include "tl_zone.h"

struct TlLayoutCache;
struct TlZoneCache;
struct TlWallCache;

/* Проём на стене комнаты: на стене wall (сторона tl_space_wall_outline) от from до to мм от её
   начала, по высоте — от bottom до top мм от пола. */
typedef struct TlWallOpening {
    size_t opening;            /* номер проёма в проекте; у предмета — SIZE_MAX */
    size_t fixture;            /* номер предмета, закрывающего стену (без плитки за ним); иначе SIZE_MAX */
    size_t wall;
    double from, to;
    double bottom, top;
    double reveal;             /* откосы плиткой: глубина, мм; 0 — нет */
} TlWallOpening;

TL_ARRAY_DEFINE(TlWallOpeningList, TlWallOpening, tl_wall_opening_list)

/* Предметы на стене wall (сторона tl_space_wall_outline). Точка на стене — (x, h): x — от левого
   угла стены, как видно из комнаты, h — от пола, мм. */
typedef struct TlWallPipe {   /* отверстие под трубу, розетку, смеситель */
    size_t wall;
    TlVec2 at;
    double diameter;
} TlWallPipe;

typedef struct TlWallRuler {  /* линейка на стене */
    size_t wall;
    TlVec2 start, end;
} TlWallRuler;

typedef struct TlWallShift {  /* смещение сетки плитки стены (у всех её полос), мм */
    size_t wall;
    TlVec2 offset;
} TlWallShift;

/* Участок пола: часть комнаты между линиями раздела и деформационными швами со своим покрытием —
   своей плиткой (tiled, плитка — zone.tile, как у зон: клей и прочее — от комнаты) или без плитки.
   Участок узнаётся по точке anchor внутри: после правки линий проект снова находит его часть пола
   (outline, holes) и переносит anchor в её середину. Части без участка — с плиткой комнаты. */
typedef struct TlFloorArea {
    TlVec2 anchor;
    char name[64];
    bool tiled;
    TlZone zone;
    TlRegion region;  /* вычисляет проект; пусто — участка больше нет */
} TlFloorArea;

TL_ARRAY_DEFINE(TlFloorAreaList, TlFloorArea, tl_floor_area_list)

TL_ARRAY_DEFINE(TlWallPipeList, TlWallPipe, tl_wall_pipe_list)
TL_ARRAY_DEFINE(TlWallRulerList, TlWallRuler, tl_wall_ruler_list)
TL_ARRAY_DEFINE(TlWallShiftList, TlWallShift, tl_wall_shift_list)

typedef struct TlSpace {
    int id;
    char *name;
    TlTileSettings tile;
    int source_id;                 /* id комнаты с общей раскладкой, 0 — своя */

    TlPolygon outline;             /* контур пола, с полом дверных проёмов */
    TlPolygon room_outline;        /* контур без проёмов: от него строится сетка плитки */
    TlPolygonList holes;           /* отдельно стоящие перегородки */
    TlPolygonList joint_strips;    /* полосы деформационных швов, задевающих пол (мир) */
    TlPolygonList threshold_strips; /* стыки в проёмах у пола комнаты (мир) */
    TlCircleList pipes;            /* отверстия под трубы, задевающие пол (мир) */
    TlOutline *house;              /* дом, в котором комната */
    struct TlSpace *layout_source; /* чья раскладка используется (сама или источник) */

    TlZone zones[TL_ZONE_COUNT];   /* бордюр и ковёр — свои у комнаты */
    double height;                 /* высота помещения, мм */
    TlWallFinish walls;            /* облицовка стен */
    TlWallOpeningList wall_openings; /* проёмы на стенах — вычисляет проект */
    TlWallPipeList wall_pipes;     /* отверстия на стенах */
    TlWallRulerList wall_rulers;   /* линейки на стенах */
    TlWallShiftList wall_shifts;   /* смещения плитки по стенам (нет записи — 0, 0) */
    TlPannoList pannos;            /* панно на стенах и на полу */
    TlRegionList regions;          /* части пола между линиями (вычисляет проект) */
    TlFloorAreaList areas;         /* участки со своим покрытием */
    TlPolygonList fixture_floor;   /* без плитки: под предметами, решётки трапов (вычисляет проект) */
    TlFixtureList stairs;          /* лестницы в комнате — их плитка на развёртке (вычисляет проект) */
    TlPolygonList slope_lines;     /* разрезы конверта трапов: отрезки от трапа к углам (вычисляет проект) */
    TlPolygonList cut_strips;      /* что режет плитку: полосы швов и разрезы конверта (вычисляет проект) */

    struct TlLayoutCache *layout_cache; /* ведёт модуль раскладки */
    struct TlZoneCache *zone_cache;     /* раскладки зон — tl_space_zones.c */
    struct TlWallCache *wall_cache;     /* раскладки стен — tl_space_walls.c */
} TlSpace;

TL_ARRAY_DEFINE(TlSpaceList, TlSpace *, tl_space_list)

TlSpace *tl_space_new(int id, const char *name, TlTileSettings tile);
void tl_space_free(TlSpace *space);
void tl_space_set_name(TlSpace *space, const char *name);

/* Пол комнаты вместе с полом под дверьми — то, что кладут плиткой. */
double tl_space_area(const TlSpace *space);
double tl_space_perimeter(const TlSpace *space);
/* Площадь и периметр самой комнаты — без пола дверных проёмов (как на плане). */
double tl_space_room_area(const TlSpace *space);
double tl_space_room_perimeter(const TlSpace *space);
bool tl_space_contains(const TlSpace *space, TlVec2 point);
TlVec2 tl_space_label_point(const TlSpace *space);

/* Контур комнаты без пола дверных проёмов (пока не вычислен — весь контур). */
const TlPolygon *tl_space_room_outline(const TlSpace *space);
/* Настройки и контур, по которым строится сетка (у общего пола — источника). */
TlTileSettings *tl_space_effective_tile(TlSpace *space);
const TlPolygon *tl_space_frame(const TlSpace *space);
bool tl_space_has_shared_floor(const TlSpace *space);
/* Часть пола под точкой (номер в regions) и участок на этой части; SIZE_MAX — нет. */
size_t tl_space_region_at(const TlSpace *space, TlVec2 point);
size_t tl_space_area_of_region(const TlSpace *space, size_t region);
/* Участок на части region: своя плитка (как у комнаты) или без плитки → номер. */
size_t tl_space_add_area(TlSpace *space, size_t region, bool tiled);
void tl_space_remove_area(TlSpace *space, size_t area);
/* Найти части пола участков заново (после правки комнат). */
void tl_space_match_areas(TlSpace *space);
/* Стены комнаты: контур без пола под дверьми, без лишних точек; сторона i — стена i. */
TlPolygon tl_space_wall_outline(const TlSpace *space);

#endif

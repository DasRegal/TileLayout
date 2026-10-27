/*
 * Комната — замкнутая область пола. Геометрия (outline, holes, house)
 * вычисляется проектом из стен и перегородок; комната хранит название,
 * настройки плитки и источник общего пола.
 */
#ifndef TL_SPACE_H
#define TL_SPACE_H

#include "tl_outline.h"
#include "tl_pipe.h"
#include "tl_tile_settings.h"

struct TlLayoutCache;

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

    struct TlLayoutCache *layout_cache; /* ведёт модуль раскладки */
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

#endif

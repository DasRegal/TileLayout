/*
 * Контур дома/квартиры: замкнутая цепочка наружных стен. Хранится
 * внутренняя сторона стен (чистовой пол) и толщина каждой стены наружу.
 * Стена i идёт от угла i к углу i+1.
 */
#ifndef TL_OUTLINE_H
#define TL_OUTLINE_H

#include "tl_geometry.h"

typedef struct TlOutline {
    TlPolygon inner;           /* внутренний контур */
    TlDoubleArray thickness;   /* толщина каждой стены */
    TlBoolArray locked;        /* стена заблокирована от перемещения */
} TlOutline;

/* Чем сдвигать стену при изменении длины соседней (например, вместе со
 * смежными стенами соседнего дома). */
typedef void (*TlWallShifter)(TlOutline *outline, size_t wall, TlVec2 offset, void *context);

TlOutline *tl_outline_new(const TlVec2 *corners, size_t corner_count, double thickness);
TlOutline *tl_outline_clone(const TlOutline *outline);
void tl_outline_free(TlOutline *outline);

size_t tl_outline_wall_count(const TlOutline *outline);
TlVec2 tl_outline_wall_start(const TlOutline *outline, size_t wall);
TlVec2 tl_outline_wall_end(const TlOutline *outline, size_t wall);
double tl_outline_area(const TlOutline *outline);
bool tl_outline_contains(const TlOutline *outline, TlVec2 point);
bool tl_outline_contains_or_touches(const TlOutline *outline, TlVec2 point);

TlPolygon tl_outline_outer(const TlOutline *outline);   /* наружный контур */
TlPolygon tl_outline_axis(const TlOutline *outline);    /* по осям стен */
double tl_outline_inner_length(const TlOutline *outline, size_t wall);
double tl_outline_outer_length(const TlOutline *outline, size_t wall);
double tl_outline_axis_length(const TlOutline *outline, size_t wall);
double tl_outline_wall_angle(const TlOutline *outline, size_t wall); /* 0…360°, против часовой */
void tl_outline_wall_frame(const TlOutline *outline, size_t wall, TlVec2 *direction,
                           TlVec2 *outward_normal);
/* Тело стены: внутренняя сторона и наружная (outer можно не передавать). */
TlPolygon tl_outline_wall_quad(const TlOutline *outline, size_t wall, const TlPolygon *outer);

void tl_outline_translate(TlOutline *outline, TlVec2 offset);
/* Параллельный сдвиг стены; концы скользят по соседним стенам. */
void tl_outline_shift_wall(TlOutline *outline, size_t wall, TlVec2 offset);
/* Новая длина стены по внутренней стороне. keep_angles — сдвинуть следующую
 * стену параллельно (через shifter), иначе передвинуть только конечный угол. */
void tl_outline_set_inner_length(TlOutline *outline, size_t wall, double length, bool keep_angles,
                                 TlWallShifter shifter, void *context);

void tl_outline_insert_vertex(TlOutline *outline, size_t wall, TlVec2 point);
bool tl_outline_remove_vertex(TlOutline *outline, size_t vertex);

bool tl_outline_vertex_locked(const TlOutline *outline, size_t vertex);
bool tl_outline_any_locked(const TlOutline *outline);

#endif

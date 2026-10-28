/*
 * Плоская геометрия: векторы, прямоугольники, многоугольники, аффинные
 * преобразования. Все размеры в миллиметрах, ось Y направлена вниз (как на
 * экране) — поэтому «положительная» площадь соответствует обходу по часовой
 * стрелке на экране.
 */
#ifndef TL_GEOMETRY_H
#define TL_GEOMETRY_H

#include <stdbool.h>
#include <stddef.h>

#include "tl_array.h"

#define TL_PI 3.14159265358979323846

typedef struct TlVec2 {
    double x, y;
} TlVec2;

/* Прямоугольник по границам: left ≤ right, top ≤ bottom. */
typedef struct TlRect {
    double left, top, right, bottom;
} TlRect;

/* x' = a·x + c·y + tx;  y' = b·x + d·y + ty  (как в cairo_matrix_t). */
typedef struct TlAffine {
    double a, b, c, d, tx, ty;
} TlAffine;

TL_ARRAY_DEFINE(TlPolygon, TlVec2, tl_polygon)
TL_ARRAY_DEFINE(TlPolygonList, TlPolygon, tl_polygon_list)
TL_ARRAY_DEFINE(TlDoubleArray, double, tl_double_array)
TL_ARRAY_DEFINE(TlBoolArray, bool, tl_bool_array)
TL_ARRAY_DEFINE(TlIntArray, int, tl_int_array)

/* ---- векторы ---------------------------------------------------------- */

static inline TlVec2 tl_vec2(double x, double y) { return (TlVec2){x, y}; }
static inline TlVec2 tl_vec2_add(TlVec2 p, TlVec2 q) { return tl_vec2(p.x + q.x, p.y + q.y); }
static inline TlVec2 tl_vec2_sub(TlVec2 p, TlVec2 q) { return tl_vec2(p.x - q.x, p.y - q.y); }
static inline TlVec2 tl_vec2_scale(TlVec2 p, double k) { return tl_vec2(p.x * k, p.y * k); }
static inline double tl_vec2_dot(TlVec2 p, TlVec2 q) { return p.x * q.x + p.y * q.y; }
static inline double tl_vec2_cross(TlVec2 p, TlVec2 q) { return p.x * q.y - p.y * q.x; }
static inline TlVec2 tl_vec2_lerp(TlVec2 p, TlVec2 q, double t)
{
    return tl_vec2(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t);
}
static inline TlVec2 tl_vec2_midpoint(TlVec2 p, TlVec2 q) { return tl_vec2_lerp(p, q, 0.5); }
/* Перпендикуляр (поворот на +90° в экранных координатах). */
static inline TlVec2 tl_vec2_perpendicular(TlVec2 p) { return tl_vec2(-p.y, p.x); }

double tl_vec2_length(TlVec2 p);
double tl_distance(TlVec2 p, TlVec2 q);
TlVec2 tl_vec2_normalized(TlVec2 p);
bool tl_vec2_is_zero(TlVec2 p);

/* Пересечение прямых p + t·d и q + s·e. false, если они параллельны. */
bool tl_line_intersection(TlVec2 p, TlVec2 d, TlVec2 q, TlVec2 e, TlVec2 *hit);

/* Расстояние от точки до отрезка; t — параметр ближайшей точки (0…1). */
double tl_point_segment_distance(TlVec2 point, TlVec2 a, TlVec2 b, double *t);

/* ---- прямоугольники --------------------------------------------------- */

static inline double tl_rect_width(TlRect r) { return r.right - r.left; }
static inline double tl_rect_height(TlRect r) { return r.bottom - r.top; }
static inline TlVec2 tl_rect_center(TlRect r)
{
    return tl_vec2((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}
bool tl_rect_intersects(TlRect r, TlRect s);
bool tl_rect_contains_point(TlRect r, TlVec2 p);
TlRect tl_rect_united(TlRect r, TlRect s);

/* ---- многоугольники --------------------------------------------------- */

double tl_polygon_signed_area(const TlPolygon *polygon);
double tl_polygon_area(const TlPolygon *polygon);
double tl_polygon_perimeter(const TlPolygon *polygon);
TlRect tl_polygon_bounds(const TlPolygon *polygon);
bool tl_polygon_contains(const TlPolygon *polygon, TlVec2 point);
bool tl_polygon_on_boundary(const TlPolygon *polygon, TlVec2 point, double tolerance);
TlVec2 tl_polygon_edge_start(const TlPolygon *polygon, size_t edge);
TlVec2 tl_polygon_edge_end(const TlPolygon *polygon, size_t edge);
double tl_polygon_edge_length(const TlPolygon *polygon, size_t edge);

/* Направление стороны и внешняя нормаль (с учётом направления обхода). */
void tl_polygon_edge_frame(const TlPolygon *polygon, size_t edge, TlVec2 *direction,
                           TlVec2 *outward_normal);

/* Каждая сторона сдвигается наружу на distances[i]; углы — по пересечению. */
TlPolygon tl_polygon_offset(const TlPolygon *polygon, const double *distances);
TlPolygon tl_polygon_offset_uniform(const TlPolygon *polygon, double distance);

/* Убирает совпадающие точки и точки на прямой (на месте). */
void tl_polygon_clean(TlPolygon *polygon, double tolerance);

/* Точка для подписи внутри многоугольника (в том числе невыпуклого). */
TlVec2 tl_polygon_label_point(const TlPolygon *polygon);

TlPolygon tl_polygon_from_rect(TlRect rect);
TlPolygon tl_polygon_from_points(const TlVec2 *points, size_t count);
void tl_polygon_translate(TlPolygon *polygon, TlVec2 offset);

/* Отсечение многоугольника прямоугольником (Сазерленд — Ходжмен). */
TlPolygon tl_polygon_clip_rect(const TlPolygon *polygon, TlRect rect);

TlPolygon tl_convex_hull(const TlVec2 *points, size_t count);

/* Освобождает все многоугольники списка и сам список. */
void tl_polygon_list_free_all(TlPolygonList *list);
TlPolygonList tl_polygon_list_deep_copy(const TlPolygonList *list);

/* ---- аффинные преобразования ------------------------------------------ */

TlAffine tl_affine_identity(void);
/* Поворот на angle_degrees (по часовой на экране) и перенос в origin. */
TlAffine tl_affine_rotation_translation(TlVec2 origin, double angle_degrees);
TlAffine tl_affine_inverted(const TlAffine *m);
/* Сначала inner, затем outer. */
TlAffine tl_affine_multiply(const TlAffine *outer, const TlAffine *inner);
TlVec2 tl_affine_apply(const TlAffine *m, TlVec2 p);
TlPolygon tl_affine_apply_polygon(const TlAffine *m, const TlPolygon *polygon);

#endif

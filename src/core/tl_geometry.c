#include "tl_geometry.h"

#include <math.h>
#include <stdlib.h>

#define PARALLEL_EPSILON 1e-9
#define MITER_LIMIT_FACTOR 10.0

/* ---- векторы ---------------------------------------------------------- */

double tl_vec2_length(TlVec2 p) { return hypot(p.x, p.y); }

double tl_distance(TlVec2 p, TlVec2 q) { return hypot(q.x - p.x, q.y - p.y); }

bool tl_vec2_is_zero(TlVec2 p) { return p.x == 0.0 && p.y == 0.0; }

TlVec2 tl_vec2_normalized(TlVec2 p)
{
    double length = tl_vec2_length(p);
    return length < 1e-9 ? tl_vec2(0, 0) : tl_vec2(p.x / length, p.y / length);
}

bool tl_line_intersection(TlVec2 p, TlVec2 d, TlVec2 q, TlVec2 e, TlVec2 *hit)
{
    double denominator = tl_vec2_cross(d, e);
    if (fabs(denominator) < PARALLEL_EPSILON)
        return false;
    double t = tl_vec2_cross(tl_vec2_sub(q, p), e) / denominator;
    *hit = tl_vec2_add(p, tl_vec2_scale(d, t));
    return true;
}

double tl_point_segment_distance(TlVec2 point, TlVec2 a, TlVec2 b, double *t)
{
    TlVec2 ab = tl_vec2_sub(b, a);
    double length_squared = tl_vec2_dot(ab, ab);
    double parameter = 0.0;
    if (length_squared > 1e-12) {
        parameter = tl_vec2_dot(tl_vec2_sub(point, a), ab) / length_squared;
        parameter = parameter < 0 ? 0 : parameter > 1 ? 1 : parameter;
    }
    if (t)
        *t = parameter;
    return tl_distance(point, tl_vec2_lerp(a, b, parameter));
}

/* ---- прямоугольники --------------------------------------------------- */

bool tl_rect_intersects(TlRect r, TlRect s)
{
    return r.left <= s.right && s.left <= r.right && r.top <= s.bottom && s.top <= r.bottom;
}

bool tl_rect_contains_point(TlRect r, TlVec2 p)
{
    return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
}

TlRect tl_rect_united(TlRect r, TlRect s)
{
    return (TlRect){fmin(r.left, s.left), fmin(r.top, s.top), fmax(r.right, s.right),
                    fmax(r.bottom, s.bottom)};
}

/* ---- многоугольники --------------------------------------------------- */

TlVec2 tl_polygon_edge_start(const TlPolygon *polygon, size_t edge) { return polygon->items[edge]; }

TlVec2 tl_polygon_edge_end(const TlPolygon *polygon, size_t edge)
{
    return polygon->items[(edge + 1) % polygon->count];
}

double tl_polygon_edge_length(const TlPolygon *polygon, size_t edge)
{
    return tl_distance(tl_polygon_edge_start(polygon, edge), tl_polygon_edge_end(polygon, edge));
}

double tl_polygon_signed_area(const TlPolygon *polygon)
{
    double twice_area = 0.0;
    for (size_t i = 0; i < polygon->count; i++)
        twice_area += tl_vec2_cross(tl_polygon_edge_start(polygon, i), tl_polygon_edge_end(polygon, i));
    return twice_area / 2.0;
}

double tl_polygon_area(const TlPolygon *polygon) { return fabs(tl_polygon_signed_area(polygon)); }

double tl_polygon_perimeter(const TlPolygon *polygon)
{
    double sum = 0.0;
    for (size_t i = 0; i < polygon->count; i++)
        sum += tl_polygon_edge_length(polygon, i);
    return sum;
}

TlRect tl_polygon_bounds(const TlPolygon *polygon)
{
    TlRect bounds = {0, 0, 0, 0};
    for (size_t i = 0; i < polygon->count; i++) {
        TlVec2 p = polygon->items[i];
        if (i == 0) {
            bounds = (TlRect){p.x, p.y, p.x, p.y};
            continue;
        }
        bounds.left = fmin(bounds.left, p.x);
        bounds.right = fmax(bounds.right, p.x);
        bounds.top = fmin(bounds.top, p.y);
        bounds.bottom = fmax(bounds.bottom, p.y);
    }
    return bounds;
}

bool tl_polygon_contains(const TlPolygon *polygon, TlVec2 point)
{
    bool inside = false;
    for (size_t i = 0, j = polygon->count - 1; i < polygon->count; j = i++) {
        TlVec2 pi = polygon->items[i], pj = polygon->items[j];
        if ((pi.y > point.y) != (pj.y > point.y)) {
            double crossing_x = pi.x + (point.y - pi.y) * (pj.x - pi.x) / (pj.y - pi.y);
            if (point.x < crossing_x)
                inside = !inside;
        }
    }
    return polygon->count >= 3 && inside;
}

bool tl_polygon_on_boundary(const TlPolygon *polygon, TlVec2 point, double tolerance)
{
    for (size_t i = 0; i < polygon->count; i++)
        if (tl_point_segment_distance(point, tl_polygon_edge_start(polygon, i),
                                      tl_polygon_edge_end(polygon, i), NULL) < tolerance)
            return true;
    return false;
}

void tl_polygon_edge_frame(const TlPolygon *polygon, size_t edge, TlVec2 *direction,
                           TlVec2 *outward_normal)
{
    double orientation = tl_polygon_signed_area(polygon) > 0 ? 1.0 : -1.0;
    TlVec2 d = tl_vec2_normalized(
        tl_vec2_sub(tl_polygon_edge_end(polygon, edge), tl_polygon_edge_start(polygon, edge)));
    if (direction)
        *direction = d;
    if (outward_normal)
        *outward_normal = tl_vec2(d.y * orientation, -d.x * orientation);
}

TlPolygon tl_polygon_offset(const TlPolygon *polygon, const double *distances)
{
    size_t n = polygon->count;
    TlPolygon result = {0};
    if (n < 3)
        return tl_polygon_copy(polygon);
    double max_distance = 0.0;
    for (size_t i = 0; i < n; i++)
        max_distance = fmax(max_distance, fabs(distances[i]));
    double limit = MITER_LIMIT_FACTOR * max_distance + 1.0;

    for (size_t j = 0; j < n; j++) {
        size_t previous = (j + n - 1) % n;
        TlVec2 previous_dir, previous_normal, dir, normal;
        tl_polygon_edge_frame(polygon, previous, &previous_dir, &previous_normal);
        tl_polygon_edge_frame(polygon, j, &dir, &normal);
        TlVec2 previous_point = tl_vec2_add(polygon->items[previous],
                                            tl_vec2_scale(previous_normal, distances[previous]));
        TlVec2 point = tl_vec2_add(polygon->items[j], tl_vec2_scale(normal, distances[j]));
        TlVec2 corner;
        if (!tl_line_intersection(previous_point, previous_dir, point, dir, &corner) ||
            tl_distance(corner, polygon->items[j]) > limit)
            corner = point; /* соседние стороны параллельны или угол слишком острый */
        tl_polygon_push(&result, corner);
    }
    return result;
}

TlPolygon tl_polygon_offset_uniform(const TlPolygon *polygon, double distance)
{
    double *distances = tl_malloc((polygon->count ? polygon->count : 1) * sizeof(double));
    for (size_t i = 0; i < polygon->count; i++)
        distances[i] = distance;
    TlPolygon result = tl_polygon_offset(polygon, distances);
    free(distances);
    return result;
}

void tl_polygon_clean(TlPolygon *polygon, double tolerance)
{
    bool changed = true;
    while (changed && polygon->count > 3) {
        changed = false;
        for (size_t k = 0; k < polygon->count; k++) {
            size_t n = polygon->count;
            TlVec2 a = polygon->items[(k + n - 1) % n];
            TlVec2 b = polygon->items[k];
            TlVec2 c = polygon->items[(k + 1) % n];
            double chord = fmax(tl_distance(a, c), 1e-9);
            bool duplicate = tl_distance(a, b) < tolerance;
            bool collinear = fabs(tl_vec2_cross(tl_vec2_sub(b, a), tl_vec2_sub(c, a))) / chord < tolerance;
            if (duplicate || collinear) {
                tl_polygon_remove_at(polygon, k);
                changed = true;
                break;
            }
        }
    }
}

static TlVec2 area_centroid(const TlPolygon *polygon, double signed_area)
{
    double cx = 0.0, cy = 0.0;
    for (size_t i = 0; i < polygon->count; i++) {
        TlVec2 p = tl_polygon_edge_start(polygon, i), q = tl_polygon_edge_end(polygon, i);
        double f = tl_vec2_cross(p, q);
        cx += (p.x + q.x) * f;
        cy += (p.y + q.y) * f;
    }
    return tl_vec2(cx / (6 * signed_area), cy / (6 * signed_area));
}

/* Середина самого длинного горизонтального сечения — всегда внутри. */
static TlVec2 widest_section_midpoint(const TlPolygon *polygon, TlVec2 fallback)
{
    TlRect bounds = tl_polygon_bounds(polygon);
    double best_width = -1.0;
    TlVec2 best = fallback;
    double *crossings = tl_malloc((polygon->count + 1) * sizeof(double));
    for (int step = 1; step < 20; step++) {
        double y = bounds.top + tl_rect_height(bounds) * step / 20.0;
        size_t found = 0;
        for (size_t i = 0; i < polygon->count; i++) {
            TlVec2 p = tl_polygon_edge_start(polygon, i), q = tl_polygon_edge_end(polygon, i);
            if ((p.y > y) != (q.y > y))
                crossings[found++] = p.x + (y - p.y) * (q.x - p.x) / (q.y - p.y);
        }
        for (size_t a = 1; a < found; a++) /* сортировка вставками: точек мало */
            for (size_t b = a; b > 0 && crossings[b - 1] > crossings[b]; b--) {
                double swap = crossings[b];
                crossings[b] = crossings[b - 1];
                crossings[b - 1] = swap;
            }
        for (size_t m = 0; m + 1 < found; m += 2)
            if (crossings[m + 1] - crossings[m] > best_width) {
                best_width = crossings[m + 1] - crossings[m];
                best = tl_vec2((crossings[m] + crossings[m + 1]) / 2, y);
            }
    }
    free(crossings);
    return best;
}

TlVec2 tl_polygon_label_point(const TlPolygon *polygon)
{
    double signed_area = tl_polygon_signed_area(polygon);
    if (fabs(signed_area) < 1e-6) {
        TlVec2 sum = {0, 0};
        for (size_t i = 0; i < polygon->count; i++)
            sum = tl_vec2_add(sum, polygon->items[i]);
        return polygon->count ? tl_vec2_scale(sum, 1.0 / polygon->count) : sum;
    }
    TlVec2 centroid = area_centroid(polygon, signed_area);
    if (tl_polygon_contains(polygon, centroid))
        return centroid;
    return widest_section_midpoint(polygon, centroid);
}

TlPolygon tl_polygon_from_rect(TlRect rect)
{
    const TlVec2 corners[] = {{rect.left, rect.top}, {rect.right, rect.top},
                              {rect.right, rect.bottom}, {rect.left, rect.bottom}};
    return tl_polygon_from_points(corners, 4);
}

TlPolygon tl_polygon_from_points(const TlVec2 *points, size_t count)
{
    TlPolygon polygon = {0};
    tl_polygon_reserve(&polygon, count);
    for (size_t i = 0; i < count; i++)
        tl_polygon_push(&polygon, points[i]);
    return polygon;
}

void tl_polygon_translate(TlPolygon *polygon, TlVec2 offset)
{
    for (size_t i = 0; i < polygon->count; i++)
        polygon->items[i] = tl_vec2_add(polygon->items[i], offset);
}

/* ---- отсечение прямоугольником ---------------------------------------- */

typedef enum { CLIP_LEFT, CLIP_RIGHT, CLIP_TOP, CLIP_BOTTOM } ClipSide;

static bool inside_side(TlVec2 p, TlRect r, ClipSide side)
{
    switch (side) {
    case CLIP_LEFT: return p.x >= r.left;
    case CLIP_RIGHT: return p.x <= r.right;
    case CLIP_TOP: return p.y >= r.top;
    default: return p.y <= r.bottom;
    }
}

static TlVec2 cross_side(TlVec2 p, TlVec2 q, TlRect r, ClipSide side)
{
    double t;
    switch (side) {
    case CLIP_LEFT: t = (r.left - p.x) / (q.x - p.x); break;
    case CLIP_RIGHT: t = (r.right - p.x) / (q.x - p.x); break;
    case CLIP_TOP: t = (r.top - p.y) / (q.y - p.y); break;
    default: t = (r.bottom - p.y) / (q.y - p.y); break;
    }
    return tl_vec2_lerp(p, q, t);
}

TlPolygon tl_polygon_clip_rect(const TlPolygon *polygon, TlRect rect)
{
    TlPolygon current = tl_polygon_copy(polygon);
    for (ClipSide side = CLIP_LEFT; side <= CLIP_BOTTOM && current.count; side++) {
        TlPolygon clipped = {0};
        for (size_t i = 0; i < current.count; i++) {
            TlVec2 p = current.items[i];
            TlVec2 q = current.items[(i + 1) % current.count];
            bool p_inside = inside_side(p, rect, side), q_inside = inside_side(q, rect, side);
            if (p_inside)
                tl_polygon_push(&clipped, p);
            if (p_inside != q_inside)
                tl_polygon_push(&clipped, cross_side(p, q, rect, side));
        }
        tl_polygon_free(&current);
        current = clipped;
    }
    return current;
}

/* ---- выпуклая оболочка (монотонная цепочка Эндрю) --------------------- */

static int compare_points(const void *left, const void *right)
{
    const TlVec2 *p = left, *q = right;
    if (p->x != q->x)
        return p->x < q->x ? -1 : 1;
    return p->y < q->y ? -1 : p->y > q->y;
}

static double turn(TlVec2 o, TlVec2 a, TlVec2 b)
{
    return tl_vec2_cross(tl_vec2_sub(a, o), tl_vec2_sub(b, o));
}

TlPolygon tl_convex_hull(const TlVec2 *points, size_t count)
{
    TlPolygon hull = {0};
    if (count < 3)
        return tl_polygon_from_points(points, count);
    TlVec2 *sorted = tl_malloc(count * sizeof(TlVec2));
    memcpy(sorted, points, count * sizeof(TlVec2));
    qsort(sorted, count, sizeof(TlVec2), compare_points);
    tl_polygon_reserve(&hull, 2 * count);
    for (int pass = 0; pass < 2; pass++) {
        size_t chain_start = hull.count;
        for (size_t k = 0; k < count; k++) {
            TlVec2 p = pass == 0 ? sorted[k] : sorted[count - 1 - k];
            while (hull.count >= chain_start + 2 &&
                   turn(hull.items[hull.count - 2], hull.items[hull.count - 1], p) <= 0)
                hull.count--;
            tl_polygon_push(&hull, p);
        }
        hull.count--; /* последняя точка цепочки — первая точка следующей */
    }
    free(sorted);
    return hull;
}

void tl_polygon_list_free_all(TlPolygonList *list)
{
    for (size_t i = 0; i < list->count; i++)
        tl_polygon_free(&list->items[i]);
    tl_polygon_list_free(list);
}

TlPolygonList tl_polygon_list_deep_copy(const TlPolygonList *list)
{
    TlPolygonList copy = {0};
    for (size_t i = 0; i < list->count; i++)
        tl_polygon_list_push(&copy, tl_polygon_copy(&list->items[i]));
    return copy;
}

/* ---- аффинные преобразования ------------------------------------------ */

TlAffine tl_affine_identity(void) { return (TlAffine){1, 0, 0, 1, 0, 0}; }

TlAffine tl_affine_rotation_translation(TlVec2 origin, double angle_degrees)
{
    double radians = angle_degrees * TL_PI / 180.0;
    double c = cos(radians), s = sin(radians);
    return (TlAffine){c, s, -s, c, origin.x, origin.y};
}

TlAffine tl_affine_inverted(const TlAffine *m)
{
    double determinant = m->a * m->d - m->b * m->c;
    if (fabs(determinant) < 1e-15)
        return tl_affine_identity();
    double a = m->d / determinant, b = -m->b / determinant;
    double c = -m->c / determinant, d = m->a / determinant;
    return (TlAffine){a, b, c, d, -(a * m->tx + c * m->ty), -(b * m->tx + d * m->ty)};
}

TlAffine tl_affine_multiply(const TlAffine *outer, const TlAffine *inner)
{
    const TlAffine *o = outer, *i = inner;
    return (TlAffine){o->a * i->a + o->c * i->b,       o->b * i->a + o->d * i->b,
                      o->a * i->c + o->c * i->d,       o->b * i->c + o->d * i->d,
                      o->a * i->tx + o->c * i->ty + o->tx, o->b * i->tx + o->d * i->ty + o->ty};
}

TlVec2 tl_affine_apply(const TlAffine *m, TlVec2 p)
{
    return tl_vec2(m->a * p.x + m->c * p.y + m->tx, m->b * p.x + m->d * p.y + m->ty);
}

TlPolygon tl_affine_apply_polygon(const TlAffine *m, const TlPolygon *polygon)
{
    TlPolygon result = {0};
    tl_polygon_reserve(&result, polygon->count);
    for (size_t i = 0; i < polygon->count; i++)
        tl_polygon_push(&result, tl_affine_apply(m, polygon->items[i]));
    return result;
}

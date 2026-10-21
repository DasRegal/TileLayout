#include "tl_outline.h"

#include <math.h>

#define BOUNDARY_TOLERANCE 1.0

TlOutline *tl_outline_new(const TlVec2 *corners, size_t corner_count, double thickness)
{
    TlOutline *outline = TL_NEW(TlOutline);
    for (size_t i = 0; i < corner_count; i++) {
        tl_polygon_push(&outline->inner, corners[i]);
        tl_double_array_push(&outline->thickness, thickness);
        tl_bool_array_push(&outline->locked, false);
    }
    return outline;
}

TlOutline *tl_outline_clone(const TlOutline *outline)
{
    TlOutline *copy = TL_NEW(TlOutline);
    copy->inner = tl_polygon_copy(&outline->inner);
    copy->thickness = tl_double_array_copy(&outline->thickness);
    copy->locked = tl_bool_array_copy(&outline->locked);
    return copy;
}

void tl_outline_free(TlOutline *outline)
{
    if (!outline)
        return;
    tl_polygon_free(&outline->inner);
    tl_double_array_free(&outline->thickness);
    tl_bool_array_free(&outline->locked);
    free(outline);
}

size_t tl_outline_wall_count(const TlOutline *outline) { return outline->inner.count; }

TlVec2 tl_outline_wall_start(const TlOutline *outline, size_t wall)
{
    return tl_polygon_edge_start(&outline->inner, wall);
}

TlVec2 tl_outline_wall_end(const TlOutline *outline, size_t wall)
{
    return tl_polygon_edge_end(&outline->inner, wall);
}

double tl_outline_area(const TlOutline *outline) { return tl_polygon_area(&outline->inner); }

bool tl_outline_contains(const TlOutline *outline, TlVec2 point)
{
    return tl_polygon_contains(&outline->inner, point);
}

bool tl_outline_contains_or_touches(const TlOutline *outline, TlVec2 point)
{
    return tl_polygon_contains(&outline->inner, point) ||
           tl_polygon_on_boundary(&outline->inner, point, BOUNDARY_TOLERANCE);
}

TlPolygon tl_outline_outer(const TlOutline *outline)
{
    return tl_polygon_offset(&outline->inner, outline->thickness.items);
}

TlPolygon tl_outline_axis(const TlOutline *outline)
{
    size_t n = outline->thickness.count;
    double *halves = tl_malloc((n ? n : 1) * sizeof(double));
    for (size_t i = 0; i < n; i++)
        halves[i] = outline->thickness.items[i] / 2;
    TlPolygon axis = tl_polygon_offset(&outline->inner, halves);
    free(halves);
    return axis;
}

double tl_outline_inner_length(const TlOutline *outline, size_t wall)
{
    return tl_polygon_edge_length(&outline->inner, wall);
}

static double length_on(TlPolygon contour, size_t wall)
{
    double length = tl_polygon_edge_length(&contour, wall);
    tl_polygon_free(&contour);
    return length;
}

double tl_outline_outer_length(const TlOutline *outline, size_t wall)
{
    return length_on(tl_outline_outer(outline), wall);
}

double tl_outline_axis_length(const TlOutline *outline, size_t wall)
{
    return length_on(tl_outline_axis(outline), wall);
}

double tl_outline_wall_angle(const TlOutline *outline, size_t wall)
{
    TlVec2 d = tl_vec2_sub(tl_outline_wall_end(outline, wall), tl_outline_wall_start(outline, wall));
    double degrees = atan2(-d.y, d.x) * 180.0 / TL_PI;
    return fmod(degrees + 360.0, 360.0);
}

void tl_outline_wall_frame(const TlOutline *outline, size_t wall, TlVec2 *direction,
                           TlVec2 *outward_normal)
{
    tl_polygon_edge_frame(&outline->inner, wall, direction, outward_normal);
}

TlPolygon tl_outline_wall_quad(const TlOutline *outline, size_t wall, const TlPolygon *outer)
{
    TlPolygon own_outer = {0};
    if (!outer) {
        own_outer = tl_outline_outer(outline);
        outer = &own_outer;
    }
    size_t next = (wall + 1) % outline->inner.count;
    const TlVec2 corners[] = {outline->inner.items[wall], outline->inner.items[next],
                              outer->items[next], outer->items[wall]};
    tl_polygon_free(&own_outer);
    return tl_polygon_from_points(corners, 4);
}

void tl_outline_translate(TlOutline *outline, TlVec2 offset)
{
    tl_polygon_translate(&outline->inner, offset);
}

void tl_outline_shift_wall(TlOutline *outline, size_t wall, TlVec2 offset)
{
    size_t n = outline->inner.count;
    size_t previous = (wall + n - 1) % n, next = (wall + 1) % n;
    TlVec2 direction, previous_direction, next_direction;
    tl_polygon_edge_frame(&outline->inner, wall, &direction, NULL);
    tl_polygon_edge_frame(&outline->inner, previous, &previous_direction, NULL);
    tl_polygon_edge_frame(&outline->inner, next, &next_direction, NULL);

    TlVec2 start = outline->inner.items[wall], end = outline->inner.items[next];
    TlVec2 moved_start = tl_vec2_add(start, offset), moved_end = tl_vec2_add(end, offset);
    TlVec2 new_start, new_end;
    if (!tl_line_intersection(outline->inner.items[previous], previous_direction, moved_start,
                              direction, &new_start))
        new_start = moved_start;
    if (!tl_line_intersection(end, next_direction, moved_end, direction, &new_end))
        new_end = moved_end;
    outline->inner.items[wall] = new_start;
    outline->inner.items[next] = new_end;
}

void tl_outline_set_inner_length(TlOutline *outline, size_t wall, double length, bool keep_angles,
                                 TlWallShifter shifter, void *context)
{
    size_t n = outline->inner.count;
    size_t next = (wall + 1) % n;
    TlVec2 start = tl_outline_wall_start(outline, wall), end = tl_outline_wall_end(outline, wall);
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(end, start));
    if (tl_vec2_is_zero(direction))
        return;
    TlVec2 offset = tl_vec2_scale(direction, length - tl_outline_inner_length(outline, wall));
    TlVec2 next_direction = tl_vec2_normalized(tl_vec2_sub(outline->inner.items[(next + 1) % n], end));
    bool next_is_parallel = fabs(tl_vec2_cross(direction, next_direction)) < 1e-6;
    if (keep_angles && n >= 4 && !next_is_parallel) {
        if (shifter)
            shifter(outline, next, offset, context);
        else
            tl_outline_shift_wall(outline, next, offset);
    } else {
        outline->inner.items[next] = tl_vec2_add(end, offset);
    }
}

void tl_outline_insert_vertex(TlOutline *outline, size_t wall, TlVec2 point)
{
    tl_polygon_insert_at(&outline->inner, wall + 1, point);
    tl_double_array_insert_at(&outline->thickness, wall + 1, outline->thickness.items[wall]);
    tl_bool_array_insert_at(&outline->locked, wall + 1, outline->locked.items[wall]);
}

bool tl_outline_remove_vertex(TlOutline *outline, size_t vertex)
{
    if (outline->inner.count <= 3)
        return false;
    /* стена, начинавшаяся в удалённом углу, сливается с предыдущей */
    tl_polygon_remove_at(&outline->inner, vertex);
    tl_double_array_remove_at(&outline->thickness, vertex);
    tl_bool_array_remove_at(&outline->locked, vertex);
    return true;
}

bool tl_outline_vertex_locked(const TlOutline *outline, size_t vertex)
{
    size_t n = outline->locked.count;
    return outline->locked.items[vertex] || outline->locked.items[(vertex + n - 1) % n];
}

bool tl_outline_any_locked(const TlOutline *outline)
{
    for (size_t i = 0; i < outline->locked.count; i++)
        if (outline->locked.items[i])
            return true;
    return false;
}

#include "app_snap.h"

#include <math.h>

#define ORTHO_DEGREES 6.0

TlVec2 app_snap_grid(double step, TlVec2 point)
{
    if (step <= 0)
        return point;
    return tl_vec2(round(point.x / step) * step, round(point.y / step) * step);
}

TlVec2 app_snap_ortho(TlVec2 point, TlVec2 start)
{
    double degrees = fmod(atan2(point.y - start.y, point.x - start.x) * 180.0 / TL_PI + 360.0, 90.0);
    if (degrees >= ORTHO_DEGREES && degrees <= 90.0 - ORTHO_DEGREES)
        return point;
    if (fabs(point.x - start.x) > fabs(point.y - start.y))
        return tl_vec2(point.x, start.y);
    return tl_vec2(start.x, point.y);
}

static void consider(TlVec2 candidate, TlVec2 point, double *best_distance, TlVec2 *best, bool *found)
{
    double d = tl_distance(point, candidate);
    if (d < *best_distance) {
        *best_distance = d;
        *best = candidate;
        *found = true;
    }
}

static bool nearest_vertex(const AppSnapContext *context, TlVec2 point, TlVec2 *vertex)
{
    double best_distance = app_view_pixels_to_mm(context->view, APP_SNAP_TOLERANCE_PX);
    bool found = false;
    TlProject *project = context->project;
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        for (size_t i = 0; i < outline->inner.count; i++)
            if (!(outline == context->skip_outline && (int)i == context->skip_vertex))
                consider(outline->inner.items[i], point, &best_distance, vertex, &found);
        if (outline == context->skip_outline)
            continue;
        TlPolygon outer = tl_outline_outer(outline);
        for (size_t i = 0; i < outer.count; i++)
            consider(outer.items[i], point, &best_distance, vertex, &found);
        tl_polygon_free(&outer);
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        if (partition == context->skip_partition)
            continue;
        consider(partition->start, point, &best_distance, vertex, &found);
        consider(partition->end, point, &best_distance, vertex, &found);
    }
    return found;
}

AppSnapResult app_snap_point(const AppSnapContext *context, TlVec2 point, const TlVec2 *refs,
                             size_t ref_count, bool free)
{
    AppSnapResult result = {point, false, false, {0, 0}, {0, 0}};
    if (free)
        return result;
    if (nearest_vertex(context, point, &result.point)) {
        result.on_vertex = true;
        return result;
    }
    result.point = app_snap_grid(context->grid_step, point);
    double tolerance = app_view_pixels_to_mm(context->view, APP_SNAP_TOLERANCE_PX);
    for (size_t i = 0; i < ref_count; i++) {
        if (fabs(point.x - refs[i].x) < tolerance)
            result.point.x = refs[i].x;
        if (fabs(point.y - refs[i].y) < tolerance)
            result.point.y = refs[i].y;
    }
    return result;
}

AppSnapResult app_snap_wall_point(const AppSnapContext *context, TlVec2 point, const TlVec2 *start,
                                  bool free)
{
    AppSnapResult result = app_snap_point(context, point, start, start ? 1 : 0, free);
    if (free || result.on_vertex)
        return result;
    TlVec2 q = start ? app_snap_ortho(result.point, *start) : result.point;
    result.point = q;

    double tolerance = app_view_pixels_to_mm(context->view, APP_SNAP_TOLERANCE_PX);
    double best_distance = tolerance;
    TlFaceList faces = tl_project_faces(context->project);
    const TlFace *best = NULL;
    for (size_t i = 0; i < faces.count; i++) {
        const TlFace *face = &faces.items[i];
        if (face->host.kind == TL_HOST_PARTITION && face->host.partition == context->skip_partition)
            continue;
        double d = tl_point_segment_distance(q, face->start, face->end, NULL);
        if (d < best_distance) {
            best_distance = d;
            best = face;
        }
    }
    if (best) {
        result.on_face = true;
        result.face_start = best->start;
        result.face_end = best->end;
        TlVec2 face_direction = tl_vec2_normalized(tl_vec2_sub(best->end, best->start));
        TlVec2 hit;
        bool along_start = start && tl_distance(*start, q) > 1 &&
                           tl_line_intersection(*start, tl_vec2_normalized(tl_vec2_sub(q, *start)),
                                                best->start, face_direction, &hit) &&
                           tl_distance(hit, q) < tolerance * 2;
        if (along_start) {
            result.point = hit; /* по линии от начала — до грани */
        } else {
            double t;
            tl_point_segment_distance(q, best->start, best->end, &t);
            result.point = tl_vec2_lerp(best->start, best->end, t);
        }
    }
    tl_face_list_free(&faces);
    return result;
}

double app_snap_along(double along, double width, double band_length, double grid_step)
{
    if (grid_step > 0)
        along = round((along - width / 2) / grid_step) * grid_step + width / 2;
    if (band_length >= width)
        along = fmax(width / 2, fmin(along, band_length - width / 2));
    return along;
}

TlVec2 app_snap_stretch(const AppSnapContext *context, TlVec2 fixed_end, TlVec2 moving_end,
                        TlVec2 target, bool free)
{
    if (free)
        return target;
    TlVec2 axis = tl_vec2_normalized(tl_vec2_sub(moving_end, fixed_end));
    if (tl_vec2_is_zero(axis))
        return target;
    double length = fmax(1.0, tl_vec2_dot(tl_vec2_sub(target, fixed_end), axis));
    TlVec2 on_axis = tl_vec2_add(fixed_end, tl_vec2_scale(axis, length));

    double best_distance = app_view_pixels_to_mm(context->view, APP_SNAP_TOLERANCE_PX);
    bool found = false;
    TlVec2 best = on_axis;
    TlFaceList faces = tl_project_faces(context->project);
    for (size_t i = 0; i < faces.count; i++) {
        const TlFace *face = &faces.items[i];
        if (face->host.kind == TL_HOST_PARTITION && face->host.partition == context->skip_partition)
            continue;
        TlVec2 hit;
        TlVec2 face_direction = tl_vec2_normalized(tl_vec2_sub(face->end, face->start));
        if (!tl_line_intersection(fixed_end, axis, face->start, face_direction, &hit))
            continue;
        bool on_face = tl_point_segment_distance(hit, face->start, face->end, NULL) < 0.5;
        bool ahead = tl_vec2_dot(tl_vec2_sub(hit, fixed_end), axis) > 1;
        if (on_face && ahead && tl_distance(hit, on_axis) < best_distance) {
            best_distance = tl_distance(hit, on_axis);
            best = hit;
            found = true;
        }
    }
    tl_face_list_free(&faces);
    if (found)
        return best;
    if (context->grid_step > 0)
        length = fmax(context->grid_step, round(length / context->grid_step) * context->grid_step);
    return tl_vec2_add(fixed_end, tl_vec2_scale(axis, length));
}

#include "tl_snap.h"

#include <math.h>

#include "tl_align.h"

#define ORTHO_DEGREES 6.0

void tl_guides_add(TlGuides *guides, TlVec2 from, bool vertical)
{
    for (int i = 0; i < guides->count; i++)
        if (guides->items[i].vertical == vertical && tl_distance(guides->items[i].from, from) < 1e-9)
            return;
    if (guides->count < TL_MAX_GUIDES)
        guides->items[guides->count++] = (TlGuide){from, vertical};
}

void tl_guides_keep_valid(TlGuides *guides, TlVec2 point)
{
    int kept = 0;
    for (int i = 0; i < guides->count; i++) {
        const TlGuide *guide = &guides->items[i];
        double offset = guide->vertical ? point.x - guide->from.x : point.y - guide->from.y;
        if (fabs(offset) < 0.5 && tl_distance(point, guide->from) > 1)
            guides->items[kept++] = *guide;
    }
    guides->count = kept;
}

TlVec2 tl_snap_grid(double step, TlVec2 point)
{
    if (step <= 0)
        return point;
    return tl_vec2(round(point.x / step) * step, round(point.y / step) * step);
}

TlVec2 tl_snap_ortho(TlVec2 point, TlVec2 start)
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

static bool nearest_vertex(const TlSnapContext *context, TlVec2 point, TlVec2 *vertex)
{
    double best_distance = context->tolerance;
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
    const TlUnderlay *underlay = &project->underlay; /* углы DXF-подложки — чтобы обводить план */
    if (underlay->visible && underlay->lines.count) {
        TlVec2 local = tl_underlay_from_plan(underlay, point);
        double reach = best_distance / underlay->scale;
        for (size_t i = 0; i < underlay->lines.count; i++) {
            const TlPolygon *line = &underlay->lines.items[i];
            for (size_t k = 0; k < line->count; k++)
                if (fabs(line->items[k].x - local.x) < reach && fabs(line->items[k].y - local.y) < reach)
                    consider(tl_underlay_to_plan(underlay, line->items[k]), point, &best_distance, vertex, &found);
        }
    }
    return found;
}

TlSnapResult tl_snap_point(const TlSnapContext *context, TlVec2 point, const TlVec2 *refs,
                             size_t ref_count, bool free)
{
    TlSnapResult result = {.point = point};
    if (free)
        return result;
    if (nearest_vertex(context, point, &result.point)) {
        result.on_vertex = true;
        return result;
    }
    result.point = tl_snap_grid(context->grid_step, point);
    if (!context->align || ref_count == 0)
        return result;
    double tolerance = context->tolerance;
    TlTracking tracking = tl_align_tracking(point, refs, ref_count, tolerance);
    if (tracking.has_x) {
        result.point.x = tracking.point.x;
        tl_guides_add(&result.guides, tracking.x_reference, true);
    }
    if (tracking.has_y) {
        result.point.y = tracking.point.y;
        tl_guides_add(&result.guides, tracking.y_reference, false);
    }
    return result;
}

/* Ближайшая точка на линиях DXF-подложки не дальше tolerance (иначе — сама точка). */
static TlVec2 underlay_line_point(const TlUnderlay *underlay, TlVec2 point, double tolerance)
{
    TlVec2 local = tl_underlay_from_plan(underlay, point), best = point;
    double reach = tolerance / underlay->scale, best_distance = reach;
    for (size_t i = 0; i < underlay->lines.count; i++) {
        const TlPolygon *line = &underlay->lines.items[i];
        for (size_t k = 0; k + 1 < line->count; k++) {
            TlVec2 a = line->items[k], b = line->items[k + 1];
            if (fmin(a.x, b.x) > local.x + reach || fmax(a.x, b.x) < local.x - reach ||
                fmin(a.y, b.y) > local.y + reach || fmax(a.y, b.y) < local.y - reach)
                continue;
            double t, d = tl_point_segment_distance(local, a, b, &t);
            if (d < best_distance) {
                best_distance = d;
                best = tl_underlay_to_plan(underlay, tl_vec2_lerp(a, b, t));
            }
        }
    }
    return best;
}

/* Опорные точки для выравнивания: start, точки инструмента, углы домов, концы перегородок. */
static TlPolygon tracking_references(const TlSnapContext *context, const TlVec2 *start)
{
    TlPolygon refs = {0};
    if (start)
        tl_polygon_push(&refs, *start);
    for (size_t i = 0; i < context->extra_ref_count; i++)
        tl_polygon_push(&refs, context->extra_refs[i]);
    TlProject *project = context->project;
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        for (size_t i = 0; i < outline->inner.count; i++)
            tl_polygon_push(&refs, outline->inner.items[i]);
        TlPolygon outer = tl_outline_outer(outline);
        for (size_t i = 0; i < outer.count; i++)
            tl_polygon_push(&refs, outer.items[i]);
        tl_polygon_free(&outer);
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        if (partition == context->skip_partition)
            continue;
        tl_polygon_push(&refs, partition->start);
        tl_polygon_push(&refs, partition->end);
    }
    return refs;
}

TlSnapResult tl_snap_wall_point(const TlSnapContext *context, TlVec2 point, const TlVec2 *start,
                                  bool free)
{
    TlPolygon refs = context->align ? tracking_references(context, start) : (TlPolygon){0};
    TlSnapResult result = tl_snap_point(context, point, refs.items, refs.count, free);
    tl_polygon_free(&refs);
    if (free || result.on_vertex)
        return result;
    TlVec2 q = result.point;
    if (start && context->align) {
        q = tl_snap_ortho(result.point, *start);
        if (q.x == start->x && q.y != start->y)
            tl_guides_add(&result.guides, *start, true);
        else if (q.y == start->y && q.x != start->x)
            tl_guides_add(&result.guides, *start, false);
    }
    result.point = q;

    double tolerance = context->tolerance;
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
    } else if (context->project->underlay.visible && context->project->underlay.lines.count) {
        result.point = underlay_line_point(&context->project->underlay, q, tolerance);
    }
    tl_face_list_free(&faces);
    tl_guides_keep_valid(&result.guides, result.point);
    return result;
}

double tl_snap_along(double along, double width, double band_length, double grid_step)
{
    if (grid_step > 0)
        along = round((along - width / 2) / grid_step) * grid_step + width / 2;
    if (band_length >= width)
        along = fmax(width / 2, fmin(along, band_length - width / 2));
    return along;
}

TlVec2 tl_snap_stretch(const TlSnapContext *context, TlVec2 fixed_end, TlVec2 moving_end,
                        TlVec2 target, bool free)
{
    if (free)
        return target;
    TlVec2 axis = tl_vec2_normalized(tl_vec2_sub(moving_end, fixed_end));
    if (tl_vec2_is_zero(axis))
        return target;
    double length = fmax(1.0, tl_vec2_dot(tl_vec2_sub(target, fixed_end), axis));
    TlVec2 on_axis = tl_vec2_add(fixed_end, tl_vec2_scale(axis, length));

    double best_distance = context->tolerance;
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

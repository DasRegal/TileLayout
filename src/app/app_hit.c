#include "app_hit.h"

#include <math.h>

static AppSelection none(void) { return (AppSelection){0}; }

/* Дома в порядке проверки: выделенный — первым. */
static size_t ordered_outlines(TlProject *project, const AppSelection *current, TlOutline **order)
{
    TlOutline *preferred = current ? app_selection_outline(current) : NULL;
    size_t count = 0;
    if (preferred)
        order[count++] = preferred;
    for (size_t i = 0; i < project->outlines.count; i++)
        if (project->outlines.items[i] != preferred)
            order[count++] = project->outlines.items[i];
    return count;
}

static bool hit_vertex(TlOutline **outlines, size_t count, TlVec2 world, double tolerance, AppSelection *hit)
{
    for (size_t o = 0; o < count; o++)
        for (size_t i = 0; i < outlines[o]->inner.count; i++)
            if (tl_distance(world, outlines[o]->inner.items[i]) < tolerance) {
                *hit = (AppSelection){.kind = APP_SELECT_VERTEX, .outline = outlines[o], .index = (int)i};
                return true;
            }
    return false;
}

static bool hit_partition_end(TlProject *project, TlVec2 world, double tolerance, AppSelection *hit)
{
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        int which = tl_distance(world, partition->start) < tolerance ? 1
                    : tl_distance(world, partition->end) < tolerance ? 2 : 0;
        if (which) {
            *hit = (AppSelection){.kind = APP_SELECT_PARTITION, .partition = partition, .index = which};
            return true;
        }
    }
    return false;
}

/* Часть отрезка под курсором: 1 — начало, 2 — конец, 0 — сам отрезок, −1 — мимо.
   Концы важнее середины. */
static int segment_part_at(TlVec2 start, TlVec2 end, TlVec2 world, double end_tolerance, double line_tolerance)
{
    if (tl_distance(world, start) < end_tolerance)
        return 1;
    if (tl_distance(world, end) < end_tolerance)
        return 2;
    return tl_point_segment_distance(world, start, end, NULL) < line_tolerance ? 0 : -1;
}

/* Линейки лежат поверх плана — проверяются первыми. */
static bool hit_ruler(TlProject *project, TlVec2 world, double end_tolerance, double line_tolerance,
                      AppSelection *hit)
{
    for (size_t i = project->rulers.count; i-- > 0;) {
        TlRuler *ruler = project->rulers.items[i];
        int part = segment_part_at(ruler->start, ruler->end, world, end_tolerance, line_tolerance);
        if (part >= 0) {
            *hit = (AppSelection){.kind = APP_SELECT_RULER, .ruler = ruler, .index = part};
            return true;
        }
    }
    return false;
}

static bool hit_expansion_joint(TlProject *project, TlVec2 world, double end_tolerance, double line_tolerance,
                                AppSelection *hit)
{
    for (size_t i = project->expansion_joints.count; i-- > 0;) {
        TlExpansionJoint *joint = project->expansion_joints.items[i];
        int part = segment_part_at(joint->start, joint->end, world, end_tolerance, fmax(line_tolerance, joint->gap / 2));
        if (part >= 0) {
            *hit = (AppSelection){.kind = APP_SELECT_EXPANSION_JOINT, .expansion_joint = joint, .index = part};
            return true;
        }
    }
    return false;
}

/* Маркеры выделенной перегородки: концы и углы тела — ближайший к курсору. */
static bool hit_selected_partition_handle(TlProject *project, const AppSelection *current, TlVec2 world,
                                          double tolerance, AppSelection *hit)
{
    if (!current || current->kind != APP_SELECT_PARTITION)
        return false;
    TlPartition *partition = current->partition;
    TlPolygon body = tl_project_partition_body(project, partition, 0);
    double best = tolerance;
    int index = -1;
    const TlVec2 ends[2] = {partition->start, partition->end};
    for (int end = 0; end < 2; end++)
        if (tl_distance(world, ends[end]) < best) {
            best = tl_distance(world, ends[end]);
            index = end + 1;
        }
    for (int corner = 0; corner < (int)body.count; corner++)
        if (tl_distance(world, body.items[corner]) < best) {
            best = tl_distance(world, body.items[corner]);
            index = APP_PARTITION_FIRST_CORNER + corner;
        }
    tl_polygon_free(&body);
    if (index < 0)
        return false;
    *hit = (AppSelection){.kind = APP_SELECT_PARTITION, .partition = partition, .index = index};
    return true;
}

static bool near_polygon(const TlPolygon *polygon, TlVec2 world, double tolerance)
{
    return tl_polygon_contains(polygon, world) || tl_polygon_on_boundary(polygon, world, tolerance);
}

static bool hit_opening(TlProject *project, TlVec2 world, double tolerance, AppSelection *hit)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon cut;
        if (!tl_project_opening_cut(project, project->openings.items[i], &cut))
            continue;
        bool inside = near_polygon(&cut, world, tolerance);
        tl_polygon_free(&cut);
        if (inside) {
            *hit = (AppSelection){.kind = APP_SELECT_OPENING, .opening = project->openings.items[i]};
            return true;
        }
    }
    return false;
}

static bool hit_partition_body(TlProject *project, TlVec2 world, double tolerance, AppSelection *hit)
{
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        TlPolygon body = tl_project_partition_body(project, partition, 0);
        bool inside = near_polygon(&body, world, tolerance);
        tl_polygon_free(&body);
        if (inside) {
            *hit = (AppSelection){.kind = APP_SELECT_PARTITION, .partition = partition};
            return true;
        }
    }
    return false;
}

static bool hit_wall(TlOutline **outlines, size_t count, TlVec2 world, double tolerance, AppSelection *hit)
{
    for (size_t o = 0; o < count; o++) {
        TlOutline *outline = outlines[o];
        TlPolygon outer = tl_outline_outer(outline);
        bool found = false;
        for (size_t wall = 0; wall < tl_outline_wall_count(outline) && !found; wall++) {
            double distance = tl_point_segment_distance(world, tl_outline_wall_start(outline, wall),
                                                        tl_outline_wall_end(outline, wall), NULL);
            bool inside_body = false;
            if (outline->thickness.items[wall] > 0) {
                TlPolygon quad = tl_outline_wall_quad(outline, wall, &outer);
                inside_body = tl_polygon_contains(&quad, world);
                tl_polygon_free(&quad);
            }
            if (distance < tolerance || inside_body) {
                *hit = (AppSelection){.kind = APP_SELECT_WALL, .outline = outline, .index = (int)wall};
                found = true;
            }
        }
        tl_polygon_free(&outer);
        if (found)
            return true;
    }
    return false;
}

AppSelection app_hit_test(TlProject *project, const AppView *view, TlVec2 screen,
                          const AppSelection *current)
{
    TlVec2 world = app_view_to_world(view, screen);
    double vertex_tolerance = app_view_pixels_to_mm(view, APP_VERTEX_TOLERANCE_PX);
    double wall_tolerance = app_view_pixels_to_mm(view, APP_WALL_TOLERANCE_PX);
    TlOutline **outlines = g_new(TlOutline *, project->outlines.count + 1);
    size_t outline_count = ordered_outlines(project, current, outlines);
    AppSelection hit = none();
    bool found = hit_ruler(project, world, vertex_tolerance, wall_tolerance, &hit) ||
                 hit_expansion_joint(project, world, vertex_tolerance, wall_tolerance, &hit) ||
                 hit_selected_partition_handle(project, current, world, vertex_tolerance, &hit) ||
                 hit_vertex(outlines, outline_count, world, vertex_tolerance, &hit) ||
                 hit_partition_end(project, world, vertex_tolerance, &hit) ||
                 hit_opening(project, world, app_view_pixels_to_mm(view, 3), &hit) ||
                 hit_partition_body(project, world, wall_tolerance, &hit) ||
                 hit_wall(outlines, outline_count, world, wall_tolerance, &hit);
    if (!found) {
        TlSpace *space = tl_project_space_at(project, world);
        if (space)
            hit = (AppSelection){.kind = APP_SELECT_SPACE, .space = space};
    }
    g_free(outlines);
    return hit;
}

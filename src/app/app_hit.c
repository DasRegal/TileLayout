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

static bool in_node(TlProject *project, TlPartition *partition, TlPartitionEnd end)
{
    TlNode node;
    return tl_project_node_of(project, partition, end, &node);
}

/* Свободный конец перегородки (конец в узле двигают за середину узла). */
static bool hit_partition_end(TlProject *project, TlVec2 world, double tolerance, AppSelection *hit)
{
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        int which = tl_distance(world, partition->start) < tolerance ? 1
                    : tl_distance(world, partition->end) < tolerance ? 2 : 0;
        if (which && in_node(project, partition, which == 1 ? TL_PARTITION_START : TL_PARTITION_END))
            which = 0;
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

/* Узел относится к выделению: выделен он сам или одна из его перегородок. */
static bool node_is_selected(const TlNode *node, const AppSelection *current)
{
    if (!current || (current->kind != APP_SELECT_NODE && current->kind != APP_SELECT_PARTITION))
        return false;
    for (size_t i = 0; i < node->count; i++)
        if (node->ends[i].partition == current->partition &&
            (current->kind == APP_SELECT_PARTITION || node->ends[i].end == current->end))
            return true;
    return false;
}

/* Точки узлов: середину можно взять всегда, углы — у выделенного узла или узла
   выделенной перегородки. Ближайшая к курсору. */
static bool hit_node(TlProject *project, const AppSelection *current, TlVec2 world, double tolerance,
                     AppSelection *hit)
{
    TlNodeList nodes = tl_project_nodes(project);
    double best = tolerance;
    for (size_t n = 0; n < nodes.count; n++) {
        const TlNode *node = &nodes.items[n];
        int points = node_is_selected(node, current) ? (int)node->count : 0;
        for (int index = 0; index <= points; index++) {
            double distance = tl_distance(world, app_node_point(node, index));
            if (distance < best) {
                best = distance;
                *hit = (AppSelection){.kind = APP_SELECT_NODE, .partition = node->ends[0].partition,
                                      .end = node->ends[0].end, .index = index};
            }
        }
    }
    tl_node_list_free(&nodes);
    return best < tolerance;
}

/* Точки примыкания (конец перегородки на боку другой) — их можно взять всегда:
   за них тянут примыкание вдоль перегородки или уводят в узел. */
static bool hit_tee(TlProject *project, TlVec2 world, double tolerance, AppSelection *hit)
{
    TlTeeList tees = tl_project_tees(project);
    double best = tolerance;
    for (size_t i = 0; i < tees.count; i++) {
        double distance = tl_distance(world, tees.items[i].point);
        if (distance < best) {
            best = distance;
            *hit = (AppSelection){.kind = APP_SELECT_TEE, .partition = tees.items[i].branch, .end = tees.items[i].end};
        }
    }
    tl_tee_list_free(&tees);
    return best < tolerance;
}

/* Маркеры выделенной перегородки: свободные концы и углы тела у них — ближайший
   к курсору (у конца в узле — точки узла). */
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
    const bool free_end[2] = {!in_node(project, partition, TL_PARTITION_START),
                              !in_node(project, partition, TL_PARTITION_END)};
    for (int end = 0; end < 2; end++)
        if (free_end[end] && tl_distance(world, ends[end]) < best) {
            best = tl_distance(world, ends[end]);
            index = end + 1;
        }
    for (int corner = 0; corner < (int)body.count; corner++)
        if (free_end[tl_partition_corner_end((TlPartitionCorner)corner) == TL_PARTITION_START ? 0 : 1] &&
            tl_distance(world, body.items[corner]) < best) {
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

/* Стык комнат в проёме — узкой полосой у своей линии (не больше трети глубины
   проёма): щелчок по остальному проёму выделяет дверь, даже если стык лежит
   на её краю. */
static bool hit_threshold(TlProject *project, const AppSelection *current, TlVec2 world, double tolerance,
                          double end_tolerance, AppSelection *hit)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        TlOpening *opening = project->openings.items[i];
        TlVec2 from, to, right, left;
        if (!tl_project_opening_threshold(project, opening, &from, &to) ||
            !tl_project_opening_section(project, opening, &right, &left))
            continue;
        /* у выделенного стыка концы тянутся по отдельности — стык наискосок */
        if (current && current->kind == APP_SELECT_OPENING && current->opening == opening &&
            current->index >= APP_OPENING_THRESHOLD) {
            double to_from = tl_distance(world, from), to_to = tl_distance(world, to);
            if (fmin(to_from, to_to) < end_tolerance) {
                *hit = (AppSelection){.kind = APP_SELECT_OPENING, .opening = opening,
                                      .index = to_from <= to_to ? APP_OPENING_THRESHOLD_FROM : APP_OPENING_THRESHOLD_TO};
                return true;
            }
        }
        double band = fmin(tolerance, tl_distance(right, left) / 3);
        if (tl_point_segment_distance(world, from, to, NULL) < band) {
            *hit = (AppSelection){.kind = APP_SELECT_OPENING, .opening = opening, .index = APP_OPENING_THRESHOLD};
            return true;
        }
    }
    return false;
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
                 hit_node(project, current, world, vertex_tolerance, &hit) ||
                 hit_tee(project, world, vertex_tolerance, &hit) ||
                 hit_selected_partition_handle(project, current, world, vertex_tolerance, &hit) ||
                 hit_vertex(outlines, outline_count, world, vertex_tolerance, &hit) ||
                 hit_partition_end(project, world, vertex_tolerance, &hit) ||
                 hit_threshold(project, current, world, app_view_pixels_to_mm(view, APP_THRESHOLD_TOLERANCE_PX),
                               vertex_tolerance, &hit) ||
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

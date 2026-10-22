#include "tl_measure_snap.h"

#include <math.h>

#include "tl_layout.h"

/* Лучший найденный кандидат. */
typedef struct SnapSearch {
    TlVec2 probe;
    double radius;
    TlSnapPoint best;
    double best_distance;
} SnapSearch;

bool tl_snap_kind_is_line(TlSnapKind kind) { return kind >= TL_SNAP_WALL_EDGE; }

/* Сила притяжения: у точек — по виду, все линии равны между собой. */
static int rank(TlSnapKind kind) { return tl_snap_kind_is_line(kind) ? TL_SNAP_WALL_EDGE : (int)kind; }

static void offer_with_direction(SnapSearch *search, TlVec2 point, TlSnapKind kind, TlVec2 direction)
{
    double distance = tl_distance(search->probe, point);
    if (distance > search->radius)
        return;
    bool stronger = search->best.kind == TL_SNAP_NONE || rank(kind) < rank(search->best.kind);
    bool closer = rank(kind) == rank(search->best.kind) && distance < search->best_distance;
    if (stronger || closer) {
        search->best = (TlSnapPoint){point, kind, direction};
        search->best_distance = distance;
    }
}

static void offer(SnapSearch *search, TlVec2 point, TlSnapKind kind)
{
    offer_with_direction(search, point, kind, tl_vec2(0, 0));
}

/* Ближайшая точка отрезка — притяжение к линии. */
static void offer_segment(SnapSearch *search, TlVec2 start, TlVec2 end, TlSnapKind kind)
{
    double along;
    tl_point_segment_distance(search->probe, start, end, &along);
    offer_with_direction(search, tl_vec2_lerp(start, end, along), kind,
                         tl_vec2_normalized(tl_vec2_sub(end, start)));
}

/* Углы, середины и сами отрезки контура; line_kind — грань или ось. */
static void offer_contour(SnapSearch *search, const TlPolygon *contour, TlSnapKind line_kind)
{
    for (size_t edge = 0; edge < contour->count; edge++) {
        TlVec2 start = tl_polygon_edge_start(contour, edge), end = tl_polygon_edge_end(contour, edge);
        offer(search, start, TL_SNAP_CORNER);
        offer(search, tl_vec2_midpoint(start, end), TL_SNAP_WALL_MIDDLE);
        offer_segment(search, start, end, line_kind);
    }
}

/* Грани стен домов (внутренние и наружные), тела перегородок, вырезы проёмов. */
static void offer_wall_surfaces(SnapSearch *search, TlProject *project)
{
    for (size_t i = 0; i < project->outlines.count; i++) {
        TlOutline *outline = project->outlines.items[i];
        offer_contour(search, &outline->inner, TL_SNAP_WALL_EDGE);
        TlPolygon outer = tl_outline_outer(outline);
        offer_contour(search, &outer, TL_SNAP_WALL_EDGE);
        tl_polygon_free(&outer);
    }
    TlPolygonList bodies = tl_project_partition_bodies(project, 0);
    for (size_t i = 0; i < bodies.count; i++)
        offer_contour(search, &bodies.items[i], TL_SNAP_WALL_EDGE);
    tl_polygon_list_free_all(&bodies);
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon cut;
        if (!tl_project_opening_cut(project, project->openings.items[i], &cut))
            continue;
        offer_contour(search, &cut, TL_SNAP_WALL_EDGE);
        tl_polygon_free(&cut);
    }
}

/* Оси наружных стен и перегородок — середина их толщины. */
static void offer_wall_axes(SnapSearch *search, TlProject *project)
{
    for (size_t i = 0; i < project->outlines.count; i++) {
        TlPolygon axis = tl_outline_axis(project->outlines.items[i]);
        offer_contour(search, &axis, TL_SNAP_WALL_AXIS);
        tl_polygon_free(&axis);
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        offer(search, tl_vec2_midpoint(partition->start, partition->end), TL_SNAP_WALL_MIDDLE);
        offer_segment(search, partition->start, partition->end, TL_SNAP_WALL_AXIS);
    }
}

static bool on_floor(const TlLayout *layout, TlVec2 local)
{
    if (!tl_polygon_contains(&layout->floor_outline, local))
        return false;
    for (size_t i = 0; i < layout->floor_holes.count; i++)
        if (tl_polygon_contains(&layout->floor_holes.items[i], local))
            return false;
    return true;
}

static void offer_joint_point(SnapSearch *search, const TlLayout *layout, TlVec2 local, TlSnapKind kind,
                              TlVec2 local_direction)
{
    if (!on_floor(layout, local))
        return;
    TlVec2 origin = tl_affine_apply(&layout->local_to_world, tl_vec2(0, 0));
    TlVec2 direction = tl_vec2_sub(tl_affine_apply(&layout->local_to_world, local_direction), origin);
    offer_with_direction(search, tl_affine_apply(&layout->local_to_world, local), kind, direction);
}

/* Середины швов вокруг плитки — её прямоугольник, расширенный на полшва. */
static void offer_tile_joints(SnapSearch *search, const TlLayout *layout, TlRect tile, double half_joint,
                              TlVec2 local_probe)
{
    TlRect joints = {tile.left - half_joint, tile.top - half_joint, tile.right + half_joint, tile.bottom + half_joint};
    TlRect reach = {joints.left - search->radius, joints.top - search->radius, joints.right + search->radius,
                    joints.bottom + search->radius};
    if (!tl_rect_contains_point(reach, local_probe))
        return;
    TlPolygon corners = tl_polygon_from_rect(joints);
    for (size_t edge = 0; edge < corners.count; edge++) {
        TlVec2 start = tl_polygon_edge_start(&corners, edge), end = tl_polygon_edge_end(&corners, edge);
        double along;
        tl_point_segment_distance(local_probe, start, end, &along);
        offer_joint_point(search, layout, start, TL_SNAP_JOINT_CROSSING, tl_vec2(0, 0));
        offer_joint_point(search, layout, tl_vec2_lerp(start, end, along), TL_SNAP_TILE_JOINT,
                          tl_vec2_normalized(tl_vec2_sub(end, start)));
    }
    tl_polygon_free(&corners);
}

static void offer_tiles(SnapSearch *search, TlProject *project)
{
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        const TlTileSettings *tile = tl_space_effective_tile(space);
        if (!tile->enabled)
            continue;
        TlLayout *layout = tl_space_layout(space);
        if (!layout || layout->too_many)
            continue;
        TlAffine world_to_local = tl_affine_inverted(&layout->local_to_world);
        TlVec2 local_probe = tl_affine_apply(&world_to_local, search->probe);
        for (size_t t = 0; t < layout->tiles.count; t++)
            offer_tile_joints(search, layout, layout->tiles.items[t].rect, tile->joint_width / 2, local_probe);
    }
}

bool tl_measure_snap(TlProject *project, TlVec2 probe, double radius, TlSnapTargets targets,
                     TlSnapPoint *result)
{
    SnapSearch search = {probe, radius, {probe, TL_SNAP_NONE, {0, 0}}, INFINITY};
    if (targets & TL_SNAP_TO_WALL_SURFACES)
        offer_wall_surfaces(&search, project);
    if (targets & TL_SNAP_TO_WALL_AXES)
        offer_wall_axes(&search, project);
    if (targets & TL_SNAP_TO_TILE_JOINTS)
        offer_tiles(&search, project);
    *result = search.best;
    return search.best.kind != TL_SNAP_NONE;
}

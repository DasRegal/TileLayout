#include "tl_drag.h"

#include <math.h>

#include "tl_align.h"
#include "tl_layout.h"

TlVec2 tl_node_point(const TlNode *node, int index)
{
    if (index <= 0)
        return tl_node_center(node);
    TlNodeCorner corners[TL_NODE_MAX];
    tl_node_corners(node, corners);
    return corners[index - 1].first;
}

/* Доля глубины проёма под точкой: у косяка в начале, в конце или посередине
   (стык целиком). depth — глубина проёма там, мм. */
static double threshold_share_at(TlProject *project, const TlOpening *opening, int part, TlVec2 world,
                                 double *depth)
{
    TlVec2 from_jamb[2], to_jamb[2];
    if (!tl_project_opening_jambs(project, opening, from_jamb, to_jamb))
        return 0;
    TlVec2 right = part == TL_DRAG_THRESHOLD_FROM ? from_jamb[0]
                   : part == TL_DRAG_THRESHOLD_TO ? to_jamb[0] : tl_vec2_midpoint(from_jamb[0], to_jamb[0]);
    TlVec2 left = part == TL_DRAG_THRESHOLD_FROM ? from_jamb[1]
                  : part == TL_DRAG_THRESHOLD_TO ? to_jamb[1] : tl_vec2_midpoint(from_jamb[1], to_jamb[1]);
    TlVec2 across = tl_vec2_sub(left, right);
    double length = tl_vec2_length(across);
    if (depth)
        *depth = length;
    return length > 0 ? fmin(1, fmax(0, tl_vec2_dot(tl_vec2_sub(world, right), across) / (length * length))) : 0;
}

/* ---- блокировки ------------------------------------------------------------- */

static bool node_locked(const TlNode *node)
{
    for (size_t i = 0; i < node->count; i++)
        if (node->ends[i].partition->locked)
            return true;
    return false;
}

static bool partition_inside(const TlPartition *partition, const TlOutline *outline)
{
    return tl_outline_contains_or_touches(outline, partition->start) &&
           tl_outline_contains_or_touches(outline, partition->end);
}

/* Дом нельзя двигать, если заблокировано что-то в нём. */
static bool house_locked(TlProject *project, const TlOutline *house)
{
    if (tl_outline_any_locked(house))
        return true;
    for (size_t i = 0; i < project->partitions.count; i++)
        if (project->partitions.items[i]->locked && partition_inside(project->partitions.items[i], house))
            return true;
    for (size_t i = 0; i < project->openings.count; i++) {
        TlBand band;
        if (project->openings.items[i]->locked && tl_project_opening_host(project, project->openings.items[i], &band) &&
            band.host.kind == TL_HOST_WALL && band.host.outline == house)
            return true;
    }
    return false;
}

/* ---- начало ----------------------------------------------------------------- */

bool tl_drag_begin(TlDrag *drag, TlProject *project, TlDragTarget target, TlVec2 start)
{
    *drag = (TlDrag){.target = target, .start = start};
    switch (target.kind) {
    case TL_DRAG_VERTEX:
    case TL_DRAG_WALL: {
        TlOutline *outline = target.outline;
        size_t n = outline->inner.count, index = (size_t)target.index;
        bool locked = target.kind == TL_DRAG_VERTEX ? tl_outline_vertex_locked(outline, index)
                                                    : outline->locked.items[index];
        if (index >= n || locked)
            return false;
        drag->original_corners = tl_polygon_copy(&outline->inner);
        if (target.kind == TL_DRAG_WALL && target.link_walls) {
            drag->links = tl_project_linked_walls(project, outline, index);
            for (size_t i = 0; i < drag->links.count; i++)
                tl_polygon_list_push(&drag->links_original, tl_polygon_copy(&drag->links.items[i].outline->inner));
        }
        drag->token = tl_project_begin_edit(project, NULL);
        return true;
    }
    case TL_DRAG_HOUSE:
        if (house_locked(project, target.outline))
            return false;
        drag->original_corners = tl_polygon_copy(&target.outline->inner);
        drag->token = tl_project_begin_edit(project, target.outline);
        return true;
    case TL_DRAG_PARTITION: {
        TlPartition *partition = target.partition;
        if (partition->locked)
            return false;
        drag->original_start = partition->start;
        drag->original_end = partition->end;
        if (target.index >= TL_DRAG_PARTITION_FIRST_CORNER)
            drag->original_corner_offset = partition->corner_offsets[target.index - TL_DRAG_PARTITION_FIRST_CORNER];
        drag->token = tl_project_begin_edit(project, NULL);
        return true;
    }
    case TL_DRAG_NODE: {
        TlNode node;
        if (!tl_project_node_of(project, target.partition, target.end, &node) || node_locked(&node))
            return false;
        drag->original_point = tl_node_point(&node, target.index);
        drag->original_node = node.point;
        drag->token = tl_project_begin_edit(project, NULL);
        return true;
    }
    case TL_DRAG_TEE: {
        TlTee tee;
        if (!tl_project_tee_of(project, target.partition, target.end, &tee) || tee.branch->locked || tee.host->locked)
            return false;
        drag->frozen = tee.branch;
        drag->original_point = tee.point;
        drag->token = tl_project_begin_edit(project, NULL);
        return true;
    }
    case TL_DRAG_OPENING:
        if (target.opening->locked || !tl_project_opening_host(project, target.opening, &drag->band))
            return false;
        tl_band_local(&drag->band, target.opening->center, &drag->original_along, NULL);
        return true;
    case TL_DRAG_THRESHOLD:
        if (target.opening->locked)
            return false;
        drag->original_shares[0] = target.opening->threshold;
        drag->original_shares[1] = target.opening->threshold_end;
        drag->original_share = threshold_share_at(project, target.opening, TL_DRAG_THRESHOLD_LINE, start, NULL);
        return true;
    case TL_DRAG_TILES: {
        const TlTileSettings *tile = tl_space_effective_tile(target.space);
        drag->original_offset_x = tile->offset_x;
        drag->original_offset_y = tile->offset_y;
        return true;
    }
    case TL_DRAG_RULER:
        drag->original_start = target.ruler->start;
        drag->original_end = target.ruler->end;
        return true;
    case TL_DRAG_JOINT:
        drag->original_start = target.joint->start;
        drag->original_end = target.joint->end;
        return true;
    default:
        return false;
    }
}

/* ---- перетаскивание ------------------------------------------------------- */

/* Сетка плитки: сдвиг мыши — в осях раскладки, целыми мм; к одному шагу сетки. */
static void drag_tiles(TlDrag *drag, TlVec2 delta, bool free)
{
    TlTileSettings *tile = tl_space_effective_tile(drag->target.space);
    double radians = tile->rotation_degrees * TL_PI / 180;
    double along = delta.x * cos(radians) + delta.y * sin(radians);
    double across = -delta.x * sin(radians) + delta.y * cos(radians);
    double x = drag->original_offset_x + along, y = drag->original_offset_y + across;
    if (!free) {
        x = round(x);
        y = round(y);
    }
    tl_layout_normalize_offsets(tile, &x, &y);
    tile->offset_x = x;
    tile->offset_y = y;
}

/* Притягивание доли к граням и середине, иначе — к целым мм; free — точно. */
static double snap_share(double share, double depth, double tolerance, bool free)
{
    static const double STOPS[] = {0, 0.5, 1};
    if (free || depth <= 0)
        return share;
    for (size_t i = 0; i < sizeof STOPS / sizeof STOPS[0]; i++)
        if (fabs(share - STOPS[i]) < tolerance / depth)
            return STOPS[i];
    return round(share * depth) / depth;
}

/* Стык — поперёк стены или перегородки: доля глубины проёма. За линию — целиком,
   с прежним наклоном; за конец у косяка — только этот конец (стык наискосок). */
static void drag_threshold(TlDrag *drag, TlProject *project, TlVec2 world, const TlSnapContext *snap, bool free)
{
    TlOpening *opening = drag->target.opening;
    int part = drag->target.index;
    double depth;
    double share = threshold_share_at(project, opening, part, world, &depth);
    if (depth <= 0)
        return;
    if (part == TL_DRAG_THRESHOLD_FROM) {
        opening->threshold = snap_share(share, depth, snap->tolerance, free);
    } else if (part == TL_DRAG_THRESHOLD_TO) {
        opening->threshold_end = snap_share(share, depth, snap->tolerance, free);
    } else {
        double middle = (drag->original_shares[0] + drag->original_shares[1]) / 2;
        double shift = snap_share(middle + share - drag->original_share, depth, snap->tolerance, free) - middle;
        /* наклон сохраняется: сдвиг не выводит ни один конец за грани */
        double low = fmin(drag->original_shares[0], drag->original_shares[1]);
        double high = fmax(drag->original_shares[0], drag->original_shares[1]);
        shift = fmax(-low, fmin(1 - high, shift));
        opening->threshold = drag->original_shares[0] + shift;
        opening->threshold_end = drag->original_shares[1] + shift;
    }
    tl_project_update_spaces(project);
}

/* Отрезок — линейка или шов: за линию — целиком, за конец — с притягиванием
   и выравниванием от другого конца. */
static void drag_segment(TlDrag *drag, TlProject *project, TlVec2 *start, TlVec2 *end, TlSnapTargets targets,
                         TlVec2 delta, const TlSnapContext *snap, bool free, TlGuides *guides)
{
    *start = drag->original_start;
    *end = drag->original_end;
    drag->measure = (TlSnapPoint){{0, 0}, TL_SNAP_NONE, {0, 0}};
    if (drag->target.index == 0) {
        *start = tl_vec2_add(*start, delta);
        *end = tl_vec2_add(*end, delta);
        return;
    }
    TlVec2 *moving = drag->target.index == 1 ? start : end;
    const TlVec2 *other = drag->target.index == 1 ? end : start;
    TlVec2 target = tl_vec2_add(*moving, delta);
    *moving = free ? target
                   : tl_measure_point(project, target, other, snap->tolerance, targets, snap->magnet, snap->align,
                                      &drag->measure, guides);
}

static void restore_outline(TlOutline *outline, const TlPolygon *corners)
{
    tl_polygon_free(&outline->inner);
    outline->inner = tl_polygon_copy(corners);
}

static double snap_distance(const TlSnapContext *snap, double distance, bool free)
{
    return free || snap->grid_step <= 0 ? distance : round(distance / snap->grid_step) * snap->grid_step;
}

static void drag_opening(TlDrag *drag, TlProject *project, TlVec2 delta, const TlSnapContext *snap, bool free)
{
    TlOpening *opening = drag->target.opening;
    double along = drag->original_along + tl_vec2_dot(delta, drag->band.direction);
    if (!free)
        along = tl_snap_along(along, opening->width, drag->band.length, snap->grid_step);
    opening->center = tl_band_point(&drag->band, along, tl_band_middle(&drag->band));
    tl_project_update_spaces(project); /* пол проёма переходит с ним */
}

/* Конец перегородки: близко к прежней оси — растягивание по ней, близко к
   горизонтали/вертикали — поворот точно на неё (если выравнивание включено),
   иначе — свободный поворот с привязками к углам и граням. */
static TlVec2 partition_end_target(TlPartition *partition, TlVec2 fixed, TlVec2 moving, TlVec2 target,
                                   const TlSnapContext *snap, bool free, TlGuides *guides)
{
    if (free)
        return target;
    TlSnapContext context = *snap;
    context.skip_partition = partition;
    const TlVec2 directions[] = {tl_vec2_sub(moving, fixed), {1, 0}, {0, 1}};
    size_t direction_count = context.align ? 3 : 1;
    TlVec2 aligned;
    size_t which;
    if (!tl_align_direction(fixed, target, directions, direction_count, context.tolerance, &aligned, &which)) {
        context.align = false; /* поворот: только углы, грани, сетка */
        return tl_snap_wall_point(&context, target, NULL, false).point;
    }
    TlVec2 end = tl_snap_stretch(&context, fixed, aligned, aligned, false);
    if (which > 0 && guides)
        tl_guides_add(guides, fixed, which == 2);
    return end;
}

static void drag_partition(TlDrag *drag, TlProject *project, TlVec2 delta, const TlSnapContext *snap, bool free,
                           TlGuides *guides)
{
    TlPartition *partition = drag->target.partition;
    TlEditOptions options = {.master = partition};
    int index = drag->target.index;
    if (index >= TL_DRAG_PARTITION_FIRST_CORNER) { /* угол тела — только поперёк оси, целыми мм */
        TlPartitionCorner corner = (TlPartitionCorner)(index - TL_DRAG_PARTITION_FIRST_CORNER);
        TlVec2 outward = tl_vec2_scale(tl_vec2_perpendicular(tl_partition_direction(partition)),
                                       tl_partition_corner_side(corner));
        double offset = drag->original_corner_offset + tl_vec2_dot(delta, outward);
        partition->corner_offsets[corner] = fmax(0, free ? offset : round(offset));
    } else if (index == TL_DRAG_PARTITION_BODY) { /* тело — только поперёк себя */
        TlVec2 across =
            tl_vec2_perpendicular(tl_vec2_normalized(tl_vec2_sub(drag->original_end, drag->original_start)));
        double shift = snap_distance(snap, tl_vec2_dot(delta, across), free);
        partition->start = tl_vec2_add(drag->original_start, tl_vec2_scale(across, shift));
        partition->end = tl_vec2_add(drag->original_end, tl_vec2_scale(across, shift));
    } else { /* конец — вдоль оси или с поворотом */
        bool moving_start = index == TL_DRAG_PARTITION_START;
        TlVec2 moving = moving_start ? drag->original_start : drag->original_end;
        TlVec2 fixed = moving_start ? drag->original_end : drag->original_start;
        *tl_partition_end_point(partition, moving_start ? TL_PARTITION_START : TL_PARTITION_END) =
            partition_end_target(partition, fixed, moving, tl_vec2_add(moving, delta), snap, free, guides);
        options.frozen = partition; /* второй конец остаётся на месте */
    }
    tl_project_apply_edit(project, drag->token, &options);
}

/* Середина узла двигает узел целиком, угол — углы тел у узла; по сетке. */
static void drag_node(TlDrag *drag, TlProject *project, TlVec2 delta, const TlSnapContext *snap, bool free)
{
    TlNode node;
    if (!tl_project_node_of(project, drag->target.partition, drag->target.end, &node))
        return;
    TlVec2 target = tl_vec2_add(drag->original_point, delta);
    if (!free)
        target = tl_snap_grid(snap->grid_step, target);
    if (drag->target.index == 0)
        tl_project_move_node(project, &node, tl_vec2_add(drag->original_node, tl_vec2_sub(target, drag->original_point)));
    else
        tl_project_set_node_corner(project, &node, (size_t)drag->target.index - 1, target);
    TlEditOptions options = {.frozen = drag->frozen, .keep_ends = drag->frozen != NULL};
    tl_project_apply_edit(project, drag->token, &options);
}

/* Точка примыкания: пока не дальше граней перегородки — скользит по её боку;
   увели за грань — перегородка делится, и дальше тянется уже узел. */
static void drag_tee(TlDrag *drag, TlProject *project, TlVec2 delta, const TlSnapContext *snap, bool free)
{
    TlTee tee;
    if (!tl_project_tee_of(project, drag->target.partition, drag->target.end, &tee))
        return;
    TlVec2 target = tl_vec2_add(drag->original_point, delta);
    if (!free)
        target = tl_snap_grid(snap->grid_step, target);
    bool became_node = tl_project_move_tee(project, &tee, target);
    TlEditOptions options = {.frozen = drag->frozen, .keep_ends = true};
    tl_project_apply_edit(project, drag->token, &options);
    if (became_node) { /* дальше тянется узел: его точка — там, где курсор */
        drag->target.kind = TL_DRAG_NODE;
        drag->target.index = 0;
        drag->original_node = drag->original_point;
    }
}

static void drag_outline(TlDrag *drag, TlProject *project, TlVec2 delta, const TlSnapContext *snap, bool free,
                         TlGuides *guides)
{
    TlOutline *outline = drag->target.outline;
    restore_outline(outline, &drag->original_corners);
    if (drag->target.kind == TL_DRAG_HOUSE) {
        TlVec2 first = drag->original_corners.items[0];
        if (!free) { /* привязка первого угла дома */
            TlSnapContext context = *snap;
            context.skip_outline = outline;
            context.skip_vertex = 0;
            delta = tl_vec2_sub(tl_snap_point(&context, tl_vec2_add(first, delta), NULL, 0, false).point, first);
        }
        tl_outline_translate(outline, delta);
        TlEditOptions options = {.translate = delta, .has_translate = true};
        tl_project_apply_edit(project, drag->token, &options);
        return;
    }
    size_t index = (size_t)drag->target.index;
    if (drag->target.kind == TL_DRAG_VERTEX) {
        size_t n = outline->inner.count;
        const TlVec2 neighbours[2] = {outline->inner.items[(index + n - 1) % n], outline->inner.items[(index + 1) % n]};
        TlSnapContext context = *snap;
        context.skip_outline = outline;
        context.skip_vertex = (int)index;
        TlSnapResult result =
            tl_snap_point(&context, tl_vec2_add(drag->original_corners.items[index], delta), neighbours, 2, free);
        outline->inner.items[index] = result.point;
        if (guides)
            *guides = result.guides;
    } else {
        TlVec2 normal;
        tl_outline_wall_frame(outline, index, NULL, &normal);
        TlVec2 shift = tl_vec2_scale(normal, snap_distance(snap, tl_vec2_dot(delta, normal), free));
        tl_outline_shift_wall(outline, index, shift);
        for (size_t i = 0; i < drag->links.count; i++) {
            restore_outline(drag->links.items[i].outline, &drag->links_original.items[i]);
            tl_outline_shift_wall(drag->links.items[i].outline, drag->links.items[i].wall, shift);
        }
    }
    tl_project_apply_edit(project, drag->token, NULL);
}

void tl_drag_move(TlDrag *drag, TlProject *project, TlVec2 world, const TlSnapContext *snap, bool free,
                  TlGuides *guides)
{
    if (guides)
        *guides = (TlGuides){0};
    TlVec2 delta = tl_vec2_sub(world, drag->start);
    switch (drag->target.kind) {
    case TL_DRAG_OPENING: drag_opening(drag, project, delta, snap, free); return;
    case TL_DRAG_THRESHOLD: drag_threshold(drag, project, world, snap, free); return;
    case TL_DRAG_TILES: drag_tiles(drag, delta, free); return;
    case TL_DRAG_RULER:
        drag_segment(drag, project, &drag->target.ruler->start, &drag->target.ruler->end, TL_SNAP_TO_ALL, delta, snap,
                     free, guides);
        return;
    case TL_DRAG_JOINT:
        drag_segment(drag, project, &drag->target.joint->start, &drag->target.joint->end, TL_SNAP_TO_WALL_SURFACES,
                     delta, snap, free, guides);
        tl_project_update_spaces(project); /* шов режет плитку — раскладка меняется */
        return;
    case TL_DRAG_PARTITION: drag_partition(drag, project, delta, snap, free, guides); break;
    case TL_DRAG_NODE: drag_node(drag, project, delta, snap, free); break;
    case TL_DRAG_TEE: drag_tee(drag, project, delta, snap, free); break;
    case TL_DRAG_VERTEX:
    case TL_DRAG_WALL:
    case TL_DRAG_HOUSE: drag_outline(drag, project, delta, snap, free, guides); break;
    default: return;
    }
    drag->geometry_changed = true;
}

void tl_drag_end(TlDrag *drag, TlProject *project)
{
    if (drag->geometry_changed) { /* узел, вернувшийся на прямую, снова одна перегородка */
        tl_project_merge_straight_nodes(project);
        tl_project_update_spaces(project);
    }
    tl_drag_free(drag);
}

void tl_drag_free(TlDrag *drag)
{
    tl_polygon_free(&drag->original_corners);
    tl_wall_ref_list_free(&drag->links);
    tl_polygon_list_free_all(&drag->links_original);
    tl_edit_token_free(drag->token);
    *drag = (TlDrag){0};
}

/*
 * Инструмент «Выбор»: выделение и перетаскивание.
 *   угол дома — с привязкой к углам и выравниванием по соседним;
 *   стена — параллельно себе (со смежной стеной соседнего дома);
 *   комната — переносит весь дом с перегородками и проёмами;
 *   перегородка за тело — только поперёк себя; за конец — вдоль своей оси,
 *     а если увести в сторону — поворачивается (с выравниванием по 0°/90°);
 *     за угол тела (у выделенной) — угол сдвигается поперёк оси, меняя форму;
 *   узел перегородок за середину — целиком (перегородки тянутся за ним), за
 *     угол — меняются углы тел у узла;
 *   проём — вдоль своей стены; стык комнат в нём — поперёк стены, за конец у
 *     косяка (у выделенного стыка) — только этот конец, стык наискосок;
 *   Ctrl — сетка плитки комнаты;
 *   линейка и деформационный шов за линию — целиком, за конец — с притягиванием.
 * Двойной клик по стене дома добавляет угол.
 */
#include "app_canvas_private.h"

#include <math.h>

#include "app_commands.h"
#include "app_hit.h"
#include "app_render.h"
#include "tl_align.h"
#include "tl_layout.h"

#define DRAG_THRESHOLD_PX 3.0

static void free_drag(AppDrag *drag)
{
    tl_polygon_free(&drag->original_corners);
    tl_wall_ref_list_free(&drag->links);
    tl_polygon_list_free_all(&drag->links_original);
    tl_edit_token_free(drag->token);
    *drag = (AppDrag){0};
}

/* ---- начало перетаскивания ------------------------------------------------ */

void app_select_begin_tiles_drag(AppCanvas *canvas, const AppPointer *pointer)
{
    TlSpace *space = tl_project_space_at(canvas->document->project, pointer->world);
    if (!space)
        return;
    app_document_select(canvas->document, (AppSelection){.kind = APP_SELECT_SPACE, .space = space});
    const TlTileSettings *tile = tl_space_effective_tile(space);
    canvas->drag = (AppDrag){.kind = APP_DRAG_TILES, .start = pointer->world, .space = space,
                             .original_offset_x = tile->offset_x, .original_offset_y = tile->offset_y};
    app_canvas_set_cursor(canvas, "move");
}

static void remember_outline(AppDrag *drag, TlOutline *outline)
{
    drag->outline = outline;
    drag->original_corners = tl_polygon_copy(&outline->inner);
}

static double threshold_share_at(TlProject *project, const TlOpening *opening, int part, TlVec2 world,
                                 double *depth);

static void begin_drag(AppCanvas *canvas, const AppPointer *pointer, AppSelection hit)
{
    TlProject *project = canvas->document->project;
    AppDrag *drag = &canvas->drag;
    *drag = (AppDrag){.start = pointer->world, .index = hit.index};
    switch (hit.kind) {
    case APP_SELECT_VERTEX:
    case APP_SELECT_WALL:
        drag->kind = hit.kind == APP_SELECT_VERTEX ? APP_DRAG_VERTEX : APP_DRAG_WALL;
        remember_outline(drag, hit.outline);
        if (hit.kind == APP_SELECT_WALL && canvas->document->preferences.link_walls) {
            drag->links = tl_project_linked_walls(project, hit.outline, (size_t)hit.index);
            for (size_t i = 0; i < drag->links.count; i++)
                tl_polygon_list_push(&drag->links_original, tl_polygon_copy(&drag->links.items[i].outline->inner));
        }
        drag->token = tl_project_begin_edit(project, NULL);
        break;
    case APP_SELECT_SPACE:
        if (!hit.space->house)
            return;
        drag->kind = APP_DRAG_HOUSE;
        remember_outline(drag, hit.space->house);
        drag->token = tl_project_begin_edit(project, hit.space->house);
        break;
    case APP_SELECT_PARTITION:
        drag->kind = APP_DRAG_PARTITION;
        drag->partition = hit.partition;
        drag->original_start = hit.partition->start;
        drag->original_end = hit.partition->end;
        TlPartitionCorner corner;
        if (app_selection_partition_corner(&hit, &corner))
            drag->original_corner_offset = hit.partition->corner_offsets[corner];
        drag->token = tl_project_begin_edit(project, NULL);
        break;
    case APP_SELECT_NODE: {
        TlNode node;
        if (!app_selection_node(&hit, project, &node))
            return;
        for (size_t i = 0; i < node.count; i++)
            if (node.ends[i].partition->locked) {
                app_document_status(canvas->document, "Перегородка в узле заблокирована");
                return;
            }
        drag->kind = APP_DRAG_NODE;
        drag->partition = hit.partition;
        drag->end = hit.end;
        drag->original_point = app_node_point(&node, hit.index);
        drag->original_node = node.point;
        drag->token = tl_project_begin_edit(project, NULL);
        break;
    }
    case APP_SELECT_TEE: {
        TlTee tee;
        if (!app_selection_tee(&hit, project, &tee))
            return;
        if (tee.branch->locked || tee.host->locked) {
            app_document_status(canvas->document, "Перегородка заблокирована");
            return;
        }
        drag->kind = APP_DRAG_TEE;
        drag->partition = hit.partition;
        drag->end = hit.end;
        drag->frozen = tee.branch;
        drag->original_point = tee.point;
        drag->token = tl_project_begin_edit(project, NULL);
        break;
    }
    case APP_SELECT_RULER:
        drag->kind = APP_DRAG_RULER;
        drag->ruler = hit.ruler;
        drag->original_start = hit.ruler->start;
        drag->original_end = hit.ruler->end;
        break;
    case APP_SELECT_EXPANSION_JOINT:
        drag->kind = APP_DRAG_EXPANSION_JOINT;
        drag->expansion_joint = hit.expansion_joint;
        drag->original_start = hit.expansion_joint->start;
        drag->original_end = hit.expansion_joint->end;
        break;
    case APP_SELECT_OPENING:
        if (hit.index >= APP_OPENING_THRESHOLD) {
            drag->kind = APP_DRAG_THRESHOLD;
            drag->opening = hit.opening;
            drag->original_shares[0] = hit.opening->threshold;
            drag->original_shares[1] = hit.opening->threshold_end;
            drag->original_share = threshold_share_at(project, hit.opening, APP_OPENING_THRESHOLD, pointer->world, NULL);
            break;
        }
        if (!tl_project_opening_host(project, hit.opening, &drag->band))
            return;
        drag->kind = APP_DRAG_OPENING;
        drag->opening = hit.opening;
        tl_band_local(&drag->band, hit.opening->center, &drag->original_along, NULL);
        break;
    default: return;
    }
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    if (pointer->control) {
        app_select_begin_tiles_drag(canvas, pointer);
        return;
    }
    AppSelection hit = app_hit_test(canvas->document->project, &canvas->view, pointer->screen,
                                    &canvas->document->selection);
    app_document_select(canvas->document, hit);
    if (hit.kind == APP_SELECT_NONE) {
        app_canvas_start_pan(canvas, pointer->screen);
        return;
    }
    if (app_selection_is_locked(canvas->document->project, &hit)) {
        app_document_status(canvas->document, "Заблокировано от перемещения — снимите блокировку на панели");
        return;
    }
    begin_drag(canvas, pointer, hit);
}

/* ---- перетаскивание ------------------------------------------------------- */

static void restore_outline(TlOutline *outline, const TlPolygon *corners)
{
    tl_polygon_free(&outline->inner);
    outline->inner = tl_polygon_copy(corners);
}

static double snap_distance(AppCanvas *canvas, double distance, bool free)
{
    double step = canvas->document->preferences.snap_step;
    return free || step <= 0 ? distance : round(distance / step) * step;
}

static void drag_tiles(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    TlTileSettings *tile = tl_space_effective_tile(drag->space);
    double radians = tile->rotation_degrees * G_PI / 180;
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

static void drag_opening(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    double along = drag->original_along + tl_vec2_dot(delta, drag->band.direction);
    if (!free)
        along = tl_snap_along(along, drag->opening->width, drag->band.length,
                               canvas->document->preferences.snap_step);
    drag->opening->center = tl_band_point(&drag->band, along, tl_band_middle(&drag->band));
    tl_project_update_spaces(canvas->document->project); /* пол проёма переходит с ним */
}

/* Доля глубины проёма под точкой: у косяка в начале, в конце или посередине
   (линия целиком). depth — глубина проёма там, мм. */
static double threshold_share_at(TlProject *project, const TlOpening *opening, int part, TlVec2 world,
                                 double *depth)
{
    TlVec2 from_jamb[2], to_jamb[2];
    if (!app_render_doorway_jambs(project, opening, from_jamb, to_jamb))
        return 0;
    TlVec2 right = part == APP_OPENING_THRESHOLD_FROM ? from_jamb[0]
                   : part == APP_OPENING_THRESHOLD_TO ? to_jamb[0] : tl_vec2_midpoint(from_jamb[0], to_jamb[0]);
    TlVec2 left = part == APP_OPENING_THRESHOLD_FROM ? from_jamb[1]
                  : part == APP_OPENING_THRESHOLD_TO ? to_jamb[1] : tl_vec2_midpoint(from_jamb[1], to_jamb[1]);
    TlVec2 across = tl_vec2_sub(left, right);
    double length = tl_vec2_length(across);
    if (depth)
        *depth = length;
    return length > 0 ? fmin(1, fmax(0, tl_vec2_dot(tl_vec2_sub(world, right), across) / (length * length))) : 0;
}

/* Притягивание доли к граням и середине, иначе — к целым мм; Shift — точно. */
static double snap_share(AppCanvas *canvas, double share, double depth, bool free)
{
    static const double STOPS[] = {0, 0.5, 1};
    if (free || depth <= 0)
        return share;
    double tolerance = app_view_pixels_to_mm(&canvas->view, APP_SNAP_TOLERANCE_PX) / depth;
    for (size_t i = 0; i < G_N_ELEMENTS(STOPS); i++)
        if (fabs(share - STOPS[i]) < tolerance)
            return STOPS[i];
    return round(share * depth) / depth;
}

/* Стык — поперёк стены или перегородки: доля глубины проёма. За линию — целиком,
   с прежним наклоном; за конец у косяка — только этот конец (стык наискосок). */
static void drag_threshold(AppCanvas *canvas, AppDrag *drag, TlVec2 world, bool free)
{
    TlProject *project = canvas->document->project;
    TlOpening *opening = drag->opening;
    double depth;
    double share = threshold_share_at(project, opening, drag->index, world, &depth);
    if (depth <= 0)
        return;
    if (drag->index == APP_OPENING_THRESHOLD_FROM) {
        opening->threshold = snap_share(canvas, share, depth, free);
    } else if (drag->index == APP_OPENING_THRESHOLD_TO) {
        opening->threshold_end = snap_share(canvas, share, depth, free);
    } else {
        double middle = (drag->original_shares[0] + drag->original_shares[1]) / 2;
        double shift = snap_share(canvas, middle + share - drag->original_share, depth, free) - middle;
        /* наклон сохраняется: сдвиг не выводит ни один конец за грани */
        double low = fmin(drag->original_shares[0], drag->original_shares[1]);
        double high = fmax(drag->original_shares[0], drag->original_shares[1]);
        shift = fmax(-low, fmin(1 - high, shift));
        opening->threshold = drag->original_shares[0] + shift;
        opening->threshold_end = drag->original_shares[1] + shift;
    }
    tl_project_update_spaces(project);
}

/* Отрезок — линейка или шов: за середину — целиком, за конец — с притягиванием
   и выравниванием от другого конца. */
static void drag_segment(AppCanvas *canvas, AppDrag *drag, TlVec2 *start, TlVec2 *end, TlSnapTargets targets,
                         TlVec2 delta, bool free)
{
    *start = drag->original_start;
    *end = drag->original_end;
    if (drag->index == 0) {
        *start = tl_vec2_add(*start, delta);
        *end = tl_vec2_add(*end, delta);
        return;
    }
    TlVec2 *moving = drag->index == 1 ? start : end;
    const TlVec2 *other = drag->index == 1 ? end : start;
    *moving = app_canvas_measure_point(canvas, tl_vec2_add(*moving, delta), other, targets, free);
}

/* Конец перегородки: близко к прежней оси — растягивание по ней, близко к
   горизонтали/вертикали — поворот точно на неё (если автовыравнивание
   включено), иначе — свободный поворот с привязками к углам и граням. */
static TlVec2 partition_end_target(AppCanvas *canvas, TlPartition *partition, TlVec2 fixed, TlVec2 moving,
                                   TlVec2 target, bool free)
{
    app_canvas_clear_guides(canvas);
    if (free)
        return target;
    TlSnapContext context = app_canvas_snap_context(canvas);
    context.skip_partition = partition;
    const TlVec2 directions[] = {tl_vec2_sub(moving, fixed), {1, 0}, {0, 1}};
    size_t direction_count = context.align ? 3 : 1;
    double tolerance = app_view_pixels_to_mm(&canvas->view, APP_SNAP_TOLERANCE_PX);
    TlVec2 aligned;
    size_t which;
    if (!tl_align_direction(fixed, target, directions, direction_count, tolerance, &aligned, &which)) {
        context.align = false; /* поворот: только углы, грани, сетка */
        return tl_snap_wall_point(&context, target, NULL, false).point;
    }
    TlVec2 end = tl_snap_stretch(&context, fixed, aligned, aligned, false);
    if (which > 0) {
        TlGuides guides = {0};
        tl_guides_add(&guides, fixed, which == 2);
        app_canvas_show_guides(canvas, &guides, end);
    }
    return end;
}

/* Угол тела — только поперёк оси: меняется его отступ от оси, целыми мм
   (с Shift — точно за мышью). */
static void drag_partition_corner(AppDrag *drag, TlPartitionCorner corner, TlVec2 delta, bool free)
{
    TlPartition *partition = drag->partition;
    TlVec2 outward = tl_vec2_scale(tl_vec2_perpendicular(tl_partition_direction(partition)),
                                   tl_partition_corner_side(corner));
    double offset = drag->original_corner_offset + tl_vec2_dot(delta, outward);
    partition->corner_offsets[corner] = fmax(0, free ? offset : round(offset));
}

static void drag_partition(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    TlPartition *partition = drag->partition;
    TlEditOptions options = {.master = partition};
    TlPartitionCorner corner;
    AppSelection grabbed = {.kind = APP_SELECT_PARTITION, .partition = partition, .index = drag->index};
    if (app_selection_partition_corner(&grabbed, &corner)) {
        drag_partition_corner(drag, corner, delta, free);
    } else if (drag->index == 0) { /* тело — только поперёк себя */
        TlVec2 across = tl_vec2_perpendicular(tl_vec2_normalized(tl_vec2_sub(drag->original_end, drag->original_start)));
        double shift = snap_distance(canvas, tl_vec2_dot(delta, across), free);
        partition->start = tl_vec2_add(drag->original_start, tl_vec2_scale(across, shift));
        partition->end = tl_vec2_add(drag->original_end, tl_vec2_scale(across, shift));
    } else {                /* конец — вдоль оси или с поворотом */
        bool moving_start = drag->index == 1;
        TlVec2 moving = moving_start ? drag->original_start : drag->original_end;
        TlVec2 fixed = moving_start ? drag->original_end : drag->original_start;
        *tl_partition_end_point(partition, moving_start ? TL_PARTITION_START : TL_PARTITION_END) =
            partition_end_target(canvas, partition, fixed, moving, tl_vec2_add(moving, delta), free);
        options.frozen = partition; /* второй конец остаётся на месте */
    }
    tl_project_apply_edit(canvas->document->project, drag->token, &options);
}

/* Середина узла двигает узел целиком, угол — углы тел у узла; по сетке
   (с Shift — точно за мышью). */
static void drag_node(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    TlProject *project = canvas->document->project;
    TlNode node;
    if (!tl_project_node_of(project, drag->partition, drag->end, &node))
        return;
    TlVec2 target = tl_vec2_add(drag->original_point, delta);
    if (!free)
        target = tl_snap_grid(canvas->document->preferences.snap_step, target);
    if (drag->index == 0)
        tl_project_move_node(project, &node, tl_vec2_add(drag->original_node, tl_vec2_sub(target, drag->original_point)));
    else
        tl_project_set_node_corner(project, &node, (size_t)drag->index - 1, target);
    TlEditOptions options = {.frozen = drag->frozen, .keep_ends = drag->frozen != NULL};
    tl_project_apply_edit(project, drag->token, &options);
}

/* Точка примыкания: пока не дальше граней перегородки — скользит по её боку;
   увели за грань — перегородка делится, и дальше тянется уже узел. */
static void drag_tee(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    TlProject *project = canvas->document->project;
    TlTee tee;
    if (!tl_project_tee_of(project, drag->partition, drag->end, &tee))
        return;
    TlVec2 target = tl_vec2_add(drag->original_point, delta);
    if (!free)
        target = tl_snap_grid(canvas->document->preferences.snap_step, target);
    bool became_node = tl_project_move_tee(project, &tee, target);
    TlEditOptions options = {.frozen = drag->frozen, .keep_ends = true};
    tl_project_apply_edit(project, drag->token, &options);
    if (!became_node)
        return;
    drag->kind = APP_DRAG_NODE; /* дальше тянется узел: его точка — там, где курсор */
    drag->index = 0;
    drag->original_node = drag->original_point;
    app_document_select(canvas->document, (AppSelection){.kind = APP_SELECT_NODE, .partition = drag->partition,
                                                         .end = drag->end});
}

static void drag_outline(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    TlProject *project = canvas->document->project;
    TlOutline *outline = drag->outline;
    if (drag->kind == APP_DRAG_HOUSE) {
        TlVec2 first = drag->original_corners.items[0];
        if (!free) { /* привязка первого угла дома */
            TlSnapContext context = app_canvas_snap_context(canvas);
            context.skip_outline = outline;
            context.skip_vertex = 0;
            delta = tl_vec2_sub(tl_snap_point(&context, tl_vec2_add(first, delta), NULL, 0, false).point, first);
        }
        restore_outline(outline, &drag->original_corners);
        tl_outline_translate(outline, delta);
        TlEditOptions options = {.translate = delta, .has_translate = true};
        tl_project_apply_edit(project, drag->token, &options);
        return;
    }
    restore_outline(outline, &drag->original_corners);
    size_t index = (size_t)drag->index;
    if (drag->kind == APP_DRAG_VERTEX) {
        size_t n = outline->inner.count;
        const TlVec2 neighbours[2] = {outline->inner.items[(index + n - 1) % n], outline->inner.items[(index + 1) % n]};
        TlSnapContext context = app_canvas_snap_context(canvas);
        context.skip_outline = outline;
        context.skip_vertex = (int)index;
        TlVec2 target = tl_vec2_add(drag->original_corners.items[index], delta);
        TlSnapResult snap = tl_snap_point(&context, target, neighbours, 2, free);
        outline->inner.items[index] = snap.point;
        app_canvas_show_guides(canvas, &snap.guides, snap.point);
    } else {
        TlVec2 normal;
        tl_outline_wall_frame(outline, index, NULL, &normal);
        TlVec2 shift = tl_vec2_scale(normal, snap_distance(canvas, tl_vec2_dot(delta, normal), free));
        tl_outline_shift_wall(outline, index, shift);
        for (size_t i = 0; i < drag->links.count; i++) {
            restore_outline(drag->links.items[i].outline, &drag->links_original.items[i]);
            tl_outline_shift_wall(drag->links.items[i].outline, drag->links.items[i].wall, shift);
        }
    }
    tl_project_apply_edit(project, drag->token, NULL);
}

static void update_hover(AppCanvas *canvas, const AppPointer *pointer)
{
    AppSelection hover = app_hit_test(canvas->document->project, &canvas->view, pointer->screen,
                                      &canvas->document->selection);
    if (app_selection_equal(&hover, &canvas->hover))
        return;
    canvas->hover = hover;
    const char *cursor = "default";
    bool segment = hover.kind == APP_SELECT_RULER || hover.kind == APP_SELECT_EXPANSION_JOINT;
    bool end_of_line = (hover.kind == APP_SELECT_PARTITION || segment) && hover.index > 0;
    if (hover.kind == APP_SELECT_VERTEX || end_of_line)
        cursor = "crosshair";
    else if (hover.kind == APP_SELECT_NODE)
        cursor = hover.index == 0 ? "move" : "crosshair";
    else if (hover.kind == APP_SELECT_TEE)
        cursor = "move";
    else if (hover.kind == APP_SELECT_WALL || hover.kind == APP_SELECT_PARTITION || segment)
        cursor = "move";
    else if (hover.kind == APP_SELECT_OPENING)
        cursor = hover.index == APP_OPENING_THRESHOLD ? "move" : "ew-resize";
    else if (hover.kind == APP_SELECT_SPACE)
        cursor = "grab";
    app_canvas_set_cursor(canvas, cursor);
    app_canvas_redraw(canvas);
}

void app_select_motion(AppCanvas *canvas, const AppPointer *pointer)
{
    AppDrag *drag = &canvas->drag;
    if (drag->kind == APP_DRAG_NONE) {
        update_hover(canvas, pointer);
        return;
    }
    if (!drag->moved) {
        if (tl_distance(pointer->world, drag->start) * canvas->view.scale < DRAG_THRESHOLD_PX)
            return;
        drag->moved = true;
        app_document_checkpoint(canvas->document, "");
    }
    TlVec2 delta = tl_vec2_sub(pointer->world, drag->start);
    switch (drag->kind) {
    case APP_DRAG_TILES: drag_tiles(canvas, drag, delta, pointer->shift); break;
    case APP_DRAG_OPENING: drag_opening(canvas, drag, delta, pointer->shift); break;
    case APP_DRAG_THRESHOLD: drag_threshold(canvas, drag, pointer->world, pointer->shift); break;
    case APP_DRAG_PARTITION: drag_partition(canvas, drag, delta, pointer->shift); break;
    case APP_DRAG_NODE: drag_node(canvas, drag, delta, pointer->shift); break;
    case APP_DRAG_TEE: drag_tee(canvas, drag, delta, pointer->shift); break;
    case APP_DRAG_RULER:
        drag_segment(canvas, drag, &drag->ruler->start, &drag->ruler->end, TL_SNAP_TO_ALL, delta, pointer->shift);
        break;
    case APP_DRAG_EXPANSION_JOINT:
        drag_segment(canvas, drag, &drag->expansion_joint->start, &drag->expansion_joint->end,
                     TL_SNAP_TO_WALL_SURFACES, delta, pointer->shift);
        tl_project_update_spaces(canvas->document->project); /* шов режет плитку — раскладка меняется */
        break;
    default: drag_outline(canvas, drag, delta, pointer->shift); break;
    }
    app_document_commit(canvas->document);
}

/* Перетаскивание меняло стены или перегородки. */
static bool changes_geometry(AppDragKind kind)
{
    return kind == APP_DRAG_PARTITION || kind == APP_DRAG_NODE || kind == APP_DRAG_TEE || kind == APP_DRAG_VERTEX ||
           kind == APP_DRAG_WALL || kind == APP_DRAG_HOUSE;
}

void app_select_release(AppCanvas *canvas, const AppPointer *pointer)
{
    if (canvas->drag.kind == APP_DRAG_NONE)
        return;
    if (canvas->drag.moved && changes_geometry(canvas->drag.kind)) {
        /* отпустили — узел, вернувшийся на прямую, снова одна перегородка */
        tl_project_merge_straight_nodes(canvas->document->project);
        tl_project_update_spaces(canvas->document->project);
        app_document_commit(canvas->document);
    }
    free_drag(&canvas->drag);
    canvas->measure_snap.kind = TL_SNAP_NONE;
    app_canvas_clear_guides(canvas);
    canvas->hover = (AppSelection){0};
    update_hover(canvas, pointer);
}

static void double_click(AppCanvas *canvas, const AppPointer *pointer)
{
    free_drag(&canvas->drag);
    AppSelection hit = app_hit_test(canvas->document->project, &canvas->view, pointer->screen, NULL);
    if (hit.kind != APP_SELECT_WALL)
        return;
    TlVec2 a = tl_outline_wall_start(hit.outline, (size_t)hit.index);
    TlVec2 b = tl_outline_wall_end(hit.outline, (size_t)hit.index);
    double t;
    tl_point_segment_distance(pointer->world, a, b, &t);
    TlVec2 on_grid = tl_snap_grid(canvas->document->preferences.snap_step, tl_vec2_lerp(a, b, t));
    tl_point_segment_distance(on_grid, a, b, &t); /* точка должна остаться на стене */
    if (t > 0 && t < 1)
        app_command_insert_vertex(canvas->document, hit.outline, (size_t)hit.index, tl_vec2_lerp(a, b, t));
}

static bool cancel(AppCanvas *canvas)
{
    if (canvas->drag.kind == APP_DRAG_NONE)
        return false;
    free_drag(&canvas->drag);
    canvas->measure_snap.kind = TL_SNAP_NONE;
    app_canvas_clear_guides(canvas);
    return true;
}

const AppTool APP_SELECT_TOOL = {
    .cursor = "default",
    .press = press,
    .motion = app_select_motion,
    .release = app_select_release,
    .double_click = double_click,
    .cancel = cancel,
};

/*
 * Инструмент «Выбор»: выделение и перетаскивание.
 *   угол дома — с привязкой к углам и выравниванием по соседним;
 *   стена — параллельно себе (со смежной стеной соседнего дома);
 *   комната — переносит весь дом с перегородками и проёмами;
 *   перегородка за тело — только поперёк себя; за конец — вдоль своей оси,
 *     а если увести в сторону — поворачивается (с выравниванием по 0°/90°);
 *     за угол тела (у выделенной) — угол сдвигается поперёк оси, меняя форму;
 *   проём — вдоль своей стены; Ctrl — сетка плитки комнаты;
 *   линейка и деформационный шов за линию — целиком, за конец — с притягиванием.
 * Двойной клик по стене дома добавляет угол.
 */
#include "app_canvas_private.h"

#include <math.h>

#include "app_commands.h"
#include "app_hit.h"
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
        along = app_snap_along(along, drag->opening->width, drag->band.length,
                               canvas->document->preferences.snap_step);
    drag->opening->center = tl_band_point(&drag->band, along, tl_band_middle(&drag->band));
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
    AppSnapContext context = app_canvas_snap_context(canvas);
    context.skip_partition = partition;
    const TlVec2 directions[] = {tl_vec2_sub(moving, fixed), {1, 0}, {0, 1}};
    size_t direction_count = context.align ? 3 : 1;
    double tolerance = app_view_pixels_to_mm(&canvas->view, APP_SNAP_TOLERANCE_PX);
    TlVec2 aligned;
    size_t which;
    if (!tl_align_direction(fixed, target, directions, direction_count, tolerance, &aligned, &which)) {
        context.align = false; /* поворот: только углы, грани, сетка */
        return app_snap_wall_point(&context, target, NULL, false).point;
    }
    TlVec2 end = app_snap_stretch(&context, fixed, aligned, aligned, false);
    if (which > 0) {
        AppGuides guides = {0};
        app_guides_add(&guides, fixed, which == 2);
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

static void drag_outline(AppCanvas *canvas, AppDrag *drag, TlVec2 delta, bool free)
{
    TlProject *project = canvas->document->project;
    TlOutline *outline = drag->outline;
    if (drag->kind == APP_DRAG_HOUSE) {
        TlVec2 first = drag->original_corners.items[0];
        if (!free) { /* привязка первого угла дома */
            AppSnapContext context = app_canvas_snap_context(canvas);
            context.skip_outline = outline;
            context.skip_vertex = 0;
            delta = tl_vec2_sub(app_snap_point(&context, tl_vec2_add(first, delta), NULL, 0, false).point, first);
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
        AppSnapContext context = app_canvas_snap_context(canvas);
        context.skip_outline = outline;
        context.skip_vertex = (int)index;
        TlVec2 target = tl_vec2_add(drag->original_corners.items[index], delta);
        AppSnapResult snap = app_snap_point(&context, target, neighbours, 2, free);
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
    else if (hover.kind == APP_SELECT_WALL || hover.kind == APP_SELECT_PARTITION || segment)
        cursor = "move";
    else if (hover.kind == APP_SELECT_OPENING)
        cursor = "ew-resize";
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
    case APP_DRAG_PARTITION: drag_partition(canvas, drag, delta, pointer->shift); break;
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

void app_select_release(AppCanvas *canvas, const AppPointer *pointer)
{
    if (canvas->drag.kind == APP_DRAG_NONE)
        return;
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
    TlVec2 on_grid = app_snap_grid(canvas->document->preferences.snap_step, tl_vec2_lerp(a, b, t));
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

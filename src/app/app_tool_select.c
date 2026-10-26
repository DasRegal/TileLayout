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
    tl_drag_free(&drag->geometry);
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

static double threshold_share_at(TlProject *project, const TlOpening *opening, int part, TlVec2 world,
                                 double *depth);

/* Что тянуть в ядре (tl_drag) — по выделенному. false — не геометрия. */
static bool drag_target(const AppCanvas *canvas, AppSelection hit, TlDragTarget *target)
{
    *target = (TlDragTarget){.index = hit.index, .partition = hit.partition, .end = hit.end,
                             .opening = hit.opening, .outline = hit.outline,
                             .link_walls = canvas->document->preferences.link_walls};
    switch (hit.kind) {
    case APP_SELECT_VERTEX: target->kind = TL_DRAG_VERTEX; return true;
    case APP_SELECT_WALL: target->kind = TL_DRAG_WALL; return true;
    case APP_SELECT_SPACE:
        target->kind = TL_DRAG_HOUSE;
        target->outline = hit.space->house;
        return target->outline != NULL;
    case APP_SELECT_PARTITION: target->kind = TL_DRAG_PARTITION; return true;
    case APP_SELECT_NODE: target->kind = TL_DRAG_NODE; return true;
    case APP_SELECT_TEE: target->kind = TL_DRAG_TEE; return true;
    case APP_SELECT_OPENING:
        target->kind = TL_DRAG_OPENING;
        return hit.index < APP_OPENING_THRESHOLD;
    default: return false;
    }
}

static void begin_drag(AppCanvas *canvas, const AppPointer *pointer, AppSelection hit)
{
    TlProject *project = canvas->document->project;
    AppDrag *drag = &canvas->drag;
    *drag = (AppDrag){.start = pointer->world, .index = hit.index};
    TlDragTarget target;
    if (drag_target(canvas, hit, &target)) {
        if (!tl_drag_begin(&drag->geometry, project, target, pointer->world)) {
            if (hit.kind == APP_SELECT_NODE || hit.kind == APP_SELECT_TEE)
                app_document_status(canvas->document, "Перегородка заблокирована");
            return;
        }
        drag->kind = APP_DRAG_GEOMETRY;
        return;
    }
    switch (hit.kind) {
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
    case APP_SELECT_OPENING: /* стык в проёме (сам проём — геометрия) */
        drag->kind = APP_DRAG_THRESHOLD;
        drag->opening = hit.opening;
        drag->original_shares[0] = hit.opening->threshold;
        drag->original_shares[1] = hit.opening->threshold_end;
        drag->original_share = threshold_share_at(project, hit.opening, APP_OPENING_THRESHOLD, pointer->world, NULL);
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

/* Дом, стена, угол, перегородка, узел, примыкание, проём — в ядре; здесь —
   линии выравнивания и выделение (примыкание, ушедшее за грань, — узел). */
static void drag_geometry(AppCanvas *canvas, AppDrag *drag, const AppPointer *pointer)
{
    TlSnapContext context = app_canvas_snap_context(canvas);
    TlGuides guides;
    TlDragKind before = drag->geometry.target.kind;
    tl_drag_move(&drag->geometry, canvas->document->project, pointer->world, &context, pointer->shift, &guides);
    if (guides.count)
        app_canvas_show_guides(canvas, &guides, pointer->world);
    else
        app_canvas_clear_guides(canvas);
    if (before == TL_DRAG_TEE && drag->geometry.target.kind == TL_DRAG_NODE)
        app_document_select(canvas->document, (AppSelection){.kind = APP_SELECT_NODE,
                                                             .partition = drag->geometry.target.partition,
                                                             .end = drag->geometry.target.end});
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
    case APP_DRAG_GEOMETRY: drag_geometry(canvas, drag, pointer); break;
    case APP_DRAG_TILES: drag_tiles(canvas, drag, delta, pointer->shift); break;
    case APP_DRAG_THRESHOLD: drag_threshold(canvas, drag, pointer->world, pointer->shift); break;
    case APP_DRAG_RULER:
        drag_segment(canvas, drag, &drag->ruler->start, &drag->ruler->end, TL_SNAP_TO_ALL, delta, pointer->shift);
        break;
    case APP_DRAG_EXPANSION_JOINT:
        drag_segment(canvas, drag, &drag->expansion_joint->start, &drag->expansion_joint->end,
                     TL_SNAP_TO_WALL_SURFACES, delta, pointer->shift);
        tl_project_update_spaces(canvas->document->project); /* шов режет плитку — раскладка меняется */
        break;
    default: break;
    }
    app_document_commit(canvas->document);
}

void app_select_release(AppCanvas *canvas, const AppPointer *pointer)
{
    if (canvas->drag.kind == APP_DRAG_NONE)
        return;
    if (canvas->drag.kind == APP_DRAG_GEOMETRY && canvas->drag.moved) {
        tl_drag_end(&canvas->drag.geometry, canvas->document->project);
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

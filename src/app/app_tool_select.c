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
 *   Shift+щелчок по комнате — добавить её к выделенным (или убрать): плитку,
 *     клей и СВП можно задать сразу нескольким комнатам;
 *   линейка и деформационный шов за линию — целиком, за конец — с притягиванием.
 * Двойной клик по стене дома добавляет угол.
 * Как тянется каждый предмет — в ядре (tl_drag.c), общем с веб-версией; здесь —
 * что под курсором, порог начала, курсор мыши, линии выравнивания.
 */
#include "app_canvas_private.h"

#include <math.h>

#include "app_commands.h"
#include "app_hit.h"

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
    canvas->drag = (AppDrag){.start = pointer->world};
    if (tl_drag_begin(&canvas->drag.geometry, canvas->document->project,
                      (TlDragTarget){.kind = TL_DRAG_TILES, .space = space}, pointer->world))
        canvas->drag.kind = APP_DRAG_GEOMETRY;
    app_canvas_set_cursor(canvas, "move");
}

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
    case APP_SELECT_OPENING: /* сам проём или стык в нём (у стыка index — как TL_DRAG_THRESHOLD_*) */
        target->kind = hit.index >= APP_OPENING_THRESHOLD ? TL_DRAG_THRESHOLD : TL_DRAG_OPENING;
        return true;
    case APP_SELECT_RULER:
        target->kind = TL_DRAG_RULER;
        target->ruler = hit.ruler;
        return true;
    case APP_SELECT_EXPANSION_JOINT:
        target->kind = TL_DRAG_JOINT;
        target->joint = hit.expansion_joint;
        return true;
    case APP_SELECT_PIPE:
        target->kind = TL_DRAG_PIPE;
        target->pipe = hit.pipe;
        return true;
    case APP_SELECT_DRAIN:
        target->kind = TL_DRAG_DRAIN;
        target->index = hit.index;
        return true;
    case APP_SELECT_FIXTURE:
        target->kind = TL_DRAG_FIXTURE;
        target->index = hit.index;
        return true;
    case APP_SELECT_PANNO:
        target->kind = TL_DRAG_PANNO;
        target->space = hit.space;
        target->index = hit.index;
        return true;
    default: return false;
    }
}

static void begin_drag(AppCanvas *canvas, const AppPointer *pointer, AppSelection hit)
{
    AppDrag *drag = &canvas->drag;
    *drag = (AppDrag){.start = pointer->world};
    TlDragTarget target;
    if (!drag_target(canvas, hit, &target))
        return;
    if (!tl_drag_begin(&drag->geometry, canvas->document->project, target, pointer->world)) {
        if (hit.kind == APP_SELECT_NODE || hit.kind == APP_SELECT_TEE)
            app_document_status(canvas->document, "Перегородка заблокирована");
        return;
    }
    drag->kind = APP_DRAG_GEOMETRY;
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    if (pointer->control) {
        app_select_begin_tiles_drag(canvas, pointer);
        return;
    }
    AppSelection hit = app_hit_test(canvas->document->project, &canvas->view, pointer->screen,
                                    &canvas->document->selection);
    if (pointer->shift && hit.kind == APP_SELECT_SPACE) {
        app_document_toggle_space(canvas->document, hit.space);
        return;
    }
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

/* Всё тянет ядро (дома, стены, перегородки, узлы, проёмы, стыки, линейки, швы,
   сетку плитки); здесь — линии выравнивания, метка притяжения и выделение
   (примыкание, ушедшее за грань, — узел). */
static void drag_geometry(AppCanvas *canvas, AppDrag *drag, const AppPointer *pointer)
{
    TlSnapContext context = app_canvas_snap_context(canvas);
    TlGuides guides;
    TlDragKind before = drag->geometry.target.kind;
    tl_drag_move(&drag->geometry, canvas->document->project, pointer->world, &context, pointer->shift, &guides);
    canvas->measure_snap = drag->geometry.measure;
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
    drag_geometry(canvas, drag, pointer);
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
    if (hit.kind == APP_SELECT_SPACE && hit.space && canvas->open_space_listener) {
        canvas->open_space_listener(hit.space, canvas->open_space_listener_data);
        return;
    }
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

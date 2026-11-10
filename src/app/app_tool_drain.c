/*
 * Инструмент «Трап»: щелчок — точечный трап (по сетке) или линейный (у стены — вдоль
 * неё); вид — из настроек правки (поле на панели). Shift — без привязок.
 */
#include "app_canvas_private.h"

#include "app_commands.h"
#include "app_render.h"

#define WALL_REACH_MM 400.0

static TlDrain placed(AppCanvas *canvas, const AppPointer *pointer)
{
    AppDocument *document = canvas->document;
    double step = document->preferences.snap_step;
    TlVec2 point = pointer->shift || step <= 0 ? pointer->world : tl_snap_grid(step, pointer->world);
    TlDrain drain = tl_drain_default((TlDrainKind)document->preferences.drain_kind, point);
    TlSpace *space = tl_project_space_at(document->project, point);
    if (space && drain.kind == TL_DRAIN_POINT) /* решётка — по плитке комнаты */
        drain.angle = tl_space_effective_tile(space)->rotation_degrees;
    if (space && drain.kind == TL_DRAIN_LINEAR && !pointer->shift) {
        TlPolygon walls = tl_space_wall_outline(space);
        tl_drain_snap_to_walls(&drain, &walls, WALL_REACH_MM);
        tl_polygon_free(&walls);
    }
    return drain;
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    if (pointer->button == 1)
        app_command_add_drain(canvas->document, placed(canvas, pointer));
}

static void motion(AppCanvas *canvas, const AppPointer *pointer)
{
    canvas->cursor = pointer->world;
    canvas->drain_preview = placed(canvas, pointer);
    app_canvas_redraw(canvas);
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    app_render_drain(cr, &canvas->view, &canvas->drain_preview, app_color_hex(APP_BLUE));
}

const AppTool APP_DRAIN_TOOL = {
    .cursor = "crosshair",
    .press = press,
    .motion = motion,
    .draw = draw,
};

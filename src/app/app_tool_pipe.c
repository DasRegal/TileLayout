/*
 * Инструмент «Труба»: щелчок — отверстие под трубу или слив в этой точке (по
 * сетке; Shift — без неё). Диаметр — из настроек правки (поле на панели).
 * Отверстий может быть сколько угодно: инструмент остаётся включённым.
 */
#include "app_canvas_private.h"

#include "app_commands.h"
#include "app_render.h"

static TlVec2 snapped(AppCanvas *canvas, const AppPointer *pointer)
{
    double step = canvas->document->preferences.snap_step;
    return pointer->shift || step <= 0 ? pointer->world : tl_snap_grid(step, pointer->world);
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    if (pointer->button != 1)
        return;
    app_command_add_pipe(canvas->document, snapped(canvas, pointer));
}

static void motion(AppCanvas *canvas, const AppPointer *pointer)
{
    canvas->cursor = snapped(canvas, pointer);
    app_canvas_redraw(canvas);
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    TlPipe preview = {canvas->cursor, canvas->document->preferences.pipe_diameter};
    app_render_pipe(cr, &canvas->view, &preview, app_color_hex(APP_BLUE), true);
}

const AppTool APP_PIPE_TOOL = {
    .cursor = "crosshair",
    .press = press,
    .motion = motion,
    .draw = draw,
};

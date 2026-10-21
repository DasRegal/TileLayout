/*
 * Инструмент «Перегородка»: клик — начало, клик — конец (или длина с
 * клавиатуры и Enter). Точки прилипают к углам, граням стен и сетке; у
 * стены видны расстояния до её концов.
 */
#include "app_canvas_private.h"

#include "app_commands.h"
#include "app_draw.h"

static void track(AppCanvas *canvas, const AppPointer *pointer)
{
    AppSnapContext context = app_canvas_snap_context(canvas);
    canvas->wall_snap = app_snap_wall_point(&context, pointer->world,
                                            canvas->has_wall_start ? &canvas->wall_start : NULL, pointer->shift);
    canvas->cursor = canvas->wall_snap.point;
    app_canvas_redraw(canvas);
}

static void finish(AppCanvas *canvas, TlVec2 end)
{
    TlVec2 start = canvas->wall_start;
    canvas->has_wall_start = false;
    canvas->typed[0] = '\0';
    app_command_add_partition(canvas->document, start, app_canvas_typed_target(canvas, start, end));
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    track(canvas, pointer);
    if (!canvas->has_wall_start) {
        canvas->wall_start = canvas->cursor;
        canvas->has_wall_start = true;
        canvas->typed[0] = '\0';
    } else {
        finish(canvas, canvas->cursor);
    }
    app_canvas_redraw(canvas);
}

static bool key(AppCanvas *canvas, guint keyval, gunichar character)
{
    if (!canvas->has_wall_start)
        return false;
    if (app_canvas_edit_typed(canvas, keyval, character))
        return true;
    if (keyval == GDK_KEY_BackSpace) {
        canvas->has_wall_start = false;
        app_canvas_redraw(canvas);
        return true;
    }
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && canvas->typed[0]) {
        TlVec2 end = app_canvas_typed_target(canvas, canvas->wall_start, canvas->cursor);
        canvas->typed[0] = '\0';
        canvas->has_wall_start = false;
        app_command_add_partition(canvas->document, canvas->wall_start, end);
        app_canvas_redraw(canvas);
        return true;
    }
    return false;
}

static bool cancel(AppCanvas *canvas)
{
    if (!canvas->has_wall_start)
        return false;
    canvas->has_wall_start = false;
    canvas->typed[0] = '\0';
    return true;
}

static void draw_face_distances(AppCanvas *canvas, cairo_t *cr)
{
    if (!canvas->wall_snap.on_face || canvas->typed[0])
        return;
    app_canvas_draw_dimension(canvas, cr, canvas->wall_snap.face_start, canvas->cursor, false, NULL);
    app_canvas_draw_dimension(canvas, cr, canvas->cursor, canvas->wall_snap.face_end, false, NULL);
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    if (canvas->has_wall_start) {
        TlVec2 start = canvas->wall_start;
        TlVec2 end = app_canvas_typed_target(canvas, start, canvas->cursor);
        if (tl_distance(start, end) >= 1) {
            TlPartition preview = {start, end, canvas->document->preferences.partition_thickness, false};
            TlPolygon body = tl_partition_body(&preview, 0);
            app_path_polygon(cr, &canvas->view, &body);
            app_set_color(cr, app_color_rgba(11, 128, 67, 0.47));
            cairo_fill_preserve(cr);
            app_stroke(cr, app_color_hex(APP_GREEN), 1.5);
            tl_polygon_free(&body);
            app_canvas_draw_dimension(canvas, cr, start, end, true, NULL);
        }
        TlVec2 s = app_view_to_screen(&canvas->view, start);
        cairo_arc(cr, s.x, s.y, 4, 0, 2 * G_PI);
        app_set_color(cr, app_color_hex(APP_GREEN));
        cairo_fill(cr);
    }
    draw_face_distances(canvas, cr);
    app_canvas_draw_cross(canvas, cr, canvas->cursor);
}

const AppTool APP_PARTITION_TOOL = {
    .cursor = "crosshair",
    .press = press,
    .motion = track,
    .key = key,
    .cancel = cancel,
    .secondary = cancel,
    .draw = draw,
};

/*
 * Инструменты «Прямоугольник» и «Контур». На пустом месте получается новый
 * дом, внутри дома — комната из перегородок (app_command_add_contour).
 * В контуре длину стороны можно набрать на клавиатуре и нажать Enter.
 */
#include "app_canvas_private.h"

#include <math.h>

#include "app_commands.h"
#include "app_draw.h"
#include "app_hit.h"

#define MIN_RECT_SIDE 10.0

/* Точка курсора: привязки и выравнивание с уже поставленными точками контура. */
static TlVec2 cursor_point(AppCanvas *canvas, const AppPointer *pointer)
{
    AppSnapContext context = app_canvas_snap_context(canvas);
    context.extra_refs = canvas->draw_points.items;
    context.extra_ref_count = canvas->draw_points.count;
    const TlVec2 *last = tl_polygon_last(&canvas->draw_points);
    AppSnapResult snap = app_snap_wall_point(&context, pointer->world, last, pointer->shift);
    app_canvas_show_guides(canvas, &snap.guides, snap.point);
    return snap.point;
}

static void finish_contour(AppCanvas *canvas)
{
    TlPolygon contour = tl_polygon_copy(&canvas->draw_points);
    if (contour.count >= 2 && tl_distance(contour.items[0], *tl_polygon_last(&contour)) < 1e-6)
        contour.count--;
    tl_polygon_clear(&canvas->draw_points);
    canvas->typed[0] = '\0';
    if (contour.count >= 3 && tl_polygon_area(&contour) > 1) {
        app_command_add_contour(canvas->document, contour.items, contour.count);
        app_canvas_set_tool(canvas, APP_TOOL_SELECT);
    }
    tl_polygon_free(&contour);
    app_canvas_redraw(canvas);
}

static void track_cursor(AppCanvas *canvas, const AppPointer *pointer)
{
    canvas->cursor = cursor_point(canvas, pointer);
    app_canvas_redraw(canvas);
}

/* ---- контур по точкам -------------------------------------------------------- */

static bool near_first_point(AppCanvas *canvas, TlVec2 point)
{
    return canvas->draw_points.count >= 3 &&
           tl_distance(point, canvas->draw_points.items[0]) * canvas->view.scale < APP_VERTEX_TOLERANCE_PX;
}

static void contour_press(AppCanvas *canvas, const AppPointer *pointer)
{
    TlVec2 point = cursor_point(canvas, pointer);
    if (near_first_point(canvas, point)) {
        finish_contour(canvas);
        return;
    }
    const TlVec2 *last = tl_polygon_last(&canvas->draw_points);
    if (!last || tl_distance(*last, point) > 1e-6)
        tl_polygon_push(&canvas->draw_points, point);
    canvas->typed[0] = '\0';
    app_canvas_redraw(canvas);
}

static void contour_double_click(AppCanvas *canvas, const AppPointer *pointer)
{
    if (canvas->draw_points.count >= 3)
        finish_contour(canvas);
}

static bool contour_key(AppCanvas *canvas, guint keyval, gunichar character)
{
    if (!canvas->draw_points.count)
        return false;
    if (app_canvas_edit_typed(canvas, keyval, character))
        return true;
    if (keyval == GDK_KEY_BackSpace) {
        canvas->draw_points.count--;
        app_canvas_redraw(canvas);
        return true;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (canvas->typed[0]) {
            TlVec2 next = app_canvas_typed_target(canvas, *tl_polygon_last(&canvas->draw_points), canvas->cursor);
            tl_polygon_push(&canvas->draw_points, next);
            canvas->typed[0] = '\0';
            app_canvas_redraw(canvas);
        } else if (canvas->draw_points.count >= 3) {
            finish_contour(canvas);
        }
        return true;
    }
    return false;
}

static bool contour_cancel(AppCanvas *canvas)
{
    if (!canvas->draw_points.count)
        return false;
    tl_polygon_clear(&canvas->draw_points);
    canvas->typed[0] = '\0';
    return true;
}

static bool contour_secondary(AppCanvas *canvas)
{
    if (!canvas->draw_points.count)
        return false;
    finish_contour(canvas);
    return true;
}

static void contour_draw(AppCanvas *canvas, cairo_t *cr)
{
    if (canvas->draw_points.count) {
        TlPolygon preview = tl_polygon_copy(&canvas->draw_points);
        TlVec2 last = *tl_polygon_last(&preview);
        TlVec2 current = app_canvas_typed_target(canvas, last, canvas->cursor);
        tl_polygon_push(&preview, current);
        app_canvas_draw_preview_polygon(canvas, cr, &preview, false);
        for (size_t i = 0; i + 2 < preview.count; i++)
            app_canvas_draw_dimension(canvas, cr, preview.items[i], preview.items[i + 1], false, NULL);
        app_canvas_draw_dimension(canvas, cr, last, current, true, NULL);
        if (near_first_point(canvas, current)) {
            TlVec2 first = app_view_to_screen(&canvas->view, canvas->draw_points.items[0]);
            cairo_arc(cr, first.x, first.y, 9, 0, 2 * G_PI);
            app_stroke(cr, app_color_hex(APP_GREEN), 2);
        }
        tl_polygon_free(&preview);
    }
    app_canvas_draw_cross(canvas, cr, canvas->cursor);
}

const AppTool APP_CONTOUR_TOOL = {
    .cursor = "crosshair",
    .press = contour_press,
    .motion = track_cursor,
    .double_click = contour_double_click,
    .key = contour_key,
    .cancel = contour_cancel,
    .secondary = contour_secondary,
    .draw = contour_draw,
};

/* ---- прямоугольник ------------------------------------------------------------- */

static void rect_press(AppCanvas *canvas, const AppPointer *pointer)
{
    canvas->rect_start = cursor_point(canvas, pointer);
    canvas->has_rect_start = true;
    app_canvas_redraw(canvas);
}

static void rect_release(AppCanvas *canvas, const AppPointer *pointer)
{
    if (!canvas->has_rect_start)
        return;
    canvas->has_rect_start = false;
    TlVec2 a = canvas->rect_start, b = cursor_point(canvas, pointer);
    if (fabs(b.x - a.x) > MIN_RECT_SIDE && fabs(b.y - a.y) > MIN_RECT_SIDE) {
        double left = fmin(a.x, b.x), right = fmax(a.x, b.x);
        double top = fmin(a.y, b.y), bottom = fmax(a.y, b.y);
        const TlVec2 corners[] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
        app_command_add_contour(canvas->document, corners, 4);
        app_canvas_set_tool(canvas, APP_TOOL_SELECT);
    }
    app_canvas_redraw(canvas);
}

static bool rect_cancel(AppCanvas *canvas)
{
    bool had_start = canvas->has_rect_start;
    canvas->has_rect_start = false;
    return had_start;
}

static void rect_draw(AppCanvas *canvas, cairo_t *cr)
{
    if (canvas->has_rect_start) {
        TlVec2 a = canvas->rect_start, b = canvas->cursor;
        const TlVec2 corners[] = {a, {b.x, a.y}, b, {a.x, b.y}};
        TlPolygon preview = tl_polygon_from_points(corners, 4);
        app_canvas_draw_preview_polygon(canvas, cr, &preview, true);
        app_canvas_draw_dimension(canvas, cr, corners[0], corners[1], false, NULL);
        app_canvas_draw_dimension(canvas, cr, corners[1], corners[2], false, NULL);
        tl_polygon_free(&preview);
    }
    app_canvas_draw_cross(canvas, cr, canvas->cursor);
}

const AppTool APP_RECT_TOOL = {
    .cursor = "crosshair",
    .press = rect_press,
    .motion = track_cursor,
    .release = rect_release,
    .cancel = rect_cancel,
    .draw = rect_draw,
};

#include "app_canvas_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_commands.h"
#include "app_draw.h"
#include "app_hit.h"
#include "app_render.h"

#define FIT_MARGIN 70
#define ZOOM_STEP 1.15

static const AppTool *const TOOLS[APP_TOOL_COUNT] = {
    &APP_SELECT_TOOL, &APP_RECT_TOOL, &APP_CONTOUR_TOOL,
    &APP_PARTITION_TOOL, &APP_OPENING_TOOL, &APP_TILES_TOOL,
};

/* ---- общее для инструментов ---------------------------------------------- */

void app_canvas_redraw(AppCanvas *canvas) { gtk_widget_queue_draw(canvas->area); }

void app_canvas_set_cursor(AppCanvas *canvas, const char *name)
{
    GdkWindow *window = gtk_widget_get_window(canvas->area);
    if (!window)
        return;
    GdkCursor *cursor = gdk_cursor_new_from_name(gdk_window_get_display(window), name ? name : "default");
    gdk_window_set_cursor(window, cursor);
    if (cursor)
        g_object_unref(cursor);
}

AppSnapContext app_canvas_snap_context(AppCanvas *canvas)
{
    return (AppSnapContext){canvas->document->project, &canvas->view,
                            canvas->document->preferences.snap_step, NULL, -1, NULL};
}

TlVec2 app_canvas_typed_target(const AppCanvas *canvas, TlVec2 start, TlVec2 toward)
{
    if (!canvas->typed[0])
        return toward;
    double length = g_ascii_strtod(canvas->typed, NULL);
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(toward, start));
    if (tl_vec2_is_zero(direction))
        direction = tl_vec2(1, 0);
    return tl_vec2_add(start, tl_vec2_scale(direction, length));
}

bool app_canvas_edit_typed(AppCanvas *canvas, guint keyval, gunichar character)
{
    size_t length = strlen(canvas->typed);
    bool has_point = strchr(canvas->typed, '.') != NULL;
    if (g_unichar_isdigit(character) || ((character == '.' || character == ',') && !has_point)) {
        if (length + 1 < sizeof canvas->typed) {
            canvas->typed[length] = g_unichar_isdigit(character) ? (char)character : '.';
            canvas->typed[length + 1] = '\0';
        }
        app_canvas_redraw(canvas);
        return true;
    }
    if (keyval == GDK_KEY_BackSpace && length) {
        canvas->typed[length - 1] = '\0';
        app_canvas_redraw(canvas);
        return true;
    }
    return false;
}

void app_canvas_draw_dimension(AppCanvas *canvas, cairo_t *cr, TlVec2 a, TlVec2 b, bool bold,
                               const char *color_hex)
{
    double length = tl_distance(a, b);
    if (length < 1)
        return;
    char text[48];
    if (bold && canvas->typed[0])
        snprintf(text, sizeof text, "⌨ %s", canvas->typed);
    else
        snprintf(text, sizeof text, "%.0f", length);
    app_render_dimension_text(cr, app_view_to_screen(&canvas->view, a), app_view_to_screen(&canvas->view, b),
                              12, text, color_hex ? color_hex : APP_GREEN, bold);
}

void app_canvas_draw_cross(AppCanvas *canvas, cairo_t *cr, TlVec2 world)
{
    TlVec2 s = app_view_to_screen(&canvas->view, world);
    cairo_move_to(cr, s.x - 8, s.y);
    cairo_line_to(cr, s.x + 8, s.y);
    cairo_move_to(cr, s.x, s.y - 8);
    cairo_line_to(cr, s.x, s.y + 8);
    app_stroke(cr, app_color_hex(APP_GREEN), 1);
}

static void draw_handle(cairo_t *cr, TlVec2 screen, double radius, bool active, const char *color)
{
    cairo_arc(cr, screen.x, screen.y, radius, 0, 2 * G_PI);
    app_set_color(cr, active ? app_color_hex(color) : app_color_hex("#ffffff"));
    cairo_fill_preserve(cr);
    app_stroke(cr, app_color_hex(color), 1.5);
}

void app_canvas_draw_preview_polygon(AppCanvas *canvas, cairo_t *cr, const TlPolygon *polygon, bool closed)
{
    for (size_t i = 0; i < polygon->count; i++) {
        TlVec2 s = app_view_to_screen(&canvas->view, polygon->items[i]);
        (i ? cairo_line_to : cairo_move_to)(cr, s.x, s.y);
    }
    if (closed && polygon->count) {
        cairo_close_path(cr);
        app_set_color(cr, app_color_rgba(11, 128, 67, 0.12));
        cairo_fill_preserve(cr);
    }
    app_stroke_dashed(cr, app_color_hex(APP_GREEN), 2, 6);
    for (size_t i = 0; i < polygon->count; i++)
        draw_handle(cr, app_view_to_screen(&canvas->view, polygon->items[i]), 3.5, false, APP_GREEN);
}

/* ---- выделение и наведение ------------------------------------------------ */

static void fill_and_outline(cairo_t *cr, AppColor fill, const char *edge, double width)
{
    app_set_color(cr, fill);
    cairo_fill_preserve(cr);
    app_stroke(cr, app_color_hex(edge), width);
}

static void draw_hover(AppCanvas *canvas, cairo_t *cr)
{
    const AppSelection *hover = &canvas->hover;
    if (hover->kind == APP_SELECT_WALL) {
        TlPolygon quad = tl_outline_wall_quad(hover->outline, (size_t)hover->index, NULL);
        app_path_polygon(cr, &canvas->view, &quad);
        app_set_color(cr, app_color_rgba(80, 160, 255, 0.35));
        cairo_fill(cr);
        tl_polygon_free(&quad);
    } else if (hover->kind == APP_SELECT_PARTITION) {
        TlPolygon body = tl_partition_body(hover->partition, 0);
        app_path_polygon(cr, &canvas->view, &body);
        app_set_color(cr, app_color_rgba(80, 160, 255, 0.35));
        cairo_fill(cr);
        tl_polygon_free(&body);
    }
}

/* Расстояния от краёв проёма до концов стены — по стороне комнаты. */
static void draw_opening_distances(AppCanvas *canvas, cairo_t *cr, const TlBand *band, double along,
                                   double width, const char *color)
{
    bool wall = band->host.kind == TL_HOST_WALL;
    double across = wall ? band->near : band->far;
    TlVec2 normal = wall ? tl_vec2_scale(band->normal, -1) : band->normal;
    double edges[2][2] = {{0, along - width / 2}, {along + width / 2, band->length}};
    for (int i = 0; i < 2; i++) {
        if (edges[i][1] - edges[i][0] < 1)
            continue;
        TlVec2 a = app_view_to_screen(&canvas->view, tl_band_point(band, edges[i][0], across));
        TlVec2 b = app_view_to_screen(&canvas->view, tl_band_point(band, edges[i][1], across));
        char text[32];
        snprintf(text, sizeof text, "%.0f", edges[i][1] - edges[i][0]);
        AppTextStyle style = {11.3, true, app_color_hex(color), app_color_rgba(255, 255, 255, 0.84)};
        app_text_along(cr, text, &style, a, b, normal, 12);
    }
}

void app_canvas_draw_opening_preview(AppCanvas *canvas, cairo_t *cr, const TlBand *band, double along,
                                     double width, const char *color, bool preview)
{
    double from, to, across_from, across_to;
    tl_project_opening_extent(canvas->document->project, band, along, width, &from, &to, &across_from, &across_to);
    TlPolygon rect = tl_band_rect(band, from, to, across_from, across_to);
    app_path_polygon(cr, &canvas->view, &rect);
    AppColor fill = preview ? app_color_rgba(11, 128, 67, 0.43) : app_color_rgba(26, 115, 232, 0.27);
    fill_and_outline(cr, fill, color, preview ? 1.5 : 2.2);
    tl_polygon_free(&rect);
    draw_opening_distances(canvas, cr, band, along, width, color);
}

static void draw_selection(AppCanvas *canvas, cairo_t *cr)
{
    const AppSelection *s = &canvas->document->selection;
    AppView *view = &canvas->view;
    if (s->kind == APP_SELECT_PARTITION) {
        TlPolygon body = tl_partition_body(s->partition, 0);
        app_path_polygon(cr, view, &body);
        fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.35), APP_BLUE, 2.2);
        tl_polygon_free(&body);
        draw_handle(cr, app_view_to_screen(view, s->partition->start), s->index == 1 ? 6 : 4.5, s->index == 1, APP_BLUE);
        draw_handle(cr, app_view_to_screen(view, s->partition->end), s->index == 2 ? 6 : 4.5, s->index == 2, APP_BLUE);
        app_canvas_draw_dimension(canvas, cr, s->partition->start, s->partition->end, false, APP_BLUE);
        return;
    }
    if (s->kind == APP_SELECT_OPENING) {
        TlBand band;
        if (tl_project_opening_host(canvas->document->project, s->opening, &band)) {
            double along;
            tl_band_local(&band, s->opening->center, &along, NULL);
            app_canvas_draw_opening_preview(canvas, cr, &band, along, s->opening->width, APP_BLUE, false);
        }
        return;
    }
    TlOutline *outline = app_selection_outline(s);
    if (!outline)
        return;
    if (s->kind == APP_SELECT_SPACE) {
        app_path_polygon_with_holes(cr, view, &s->space->outline, &s->space->holes);
        fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.14), APP_BLUE, 2.2);
    } else if (s->kind == APP_SELECT_WALL) {
        TlPolygon quad = tl_outline_wall_quad(outline, (size_t)s->index, NULL);
        app_path_polygon(cr, view, &quad);
        fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.43), APP_BLUE, 2.2);
        tl_polygon_free(&quad);
    }
    for (size_t i = 0; i < outline->inner.count; i++) {
        bool active = s->kind == APP_SELECT_VERTEX && (size_t)s->index == i;
        bool hovered = canvas->hover.kind == APP_SELECT_VERTEX && canvas->hover.outline == outline &&
                       (size_t)canvas->hover.index == i;
        draw_handle(cr, app_view_to_screen(view, outline->inner.items[i]), active || hovered ? 6 : 4.5, active, APP_BLUE);
    }
}

void app_canvas_render(AppCanvas *canvas, cairo_t *cr, int width, int height)
{
    canvas->view.width = width;
    canvas->view.height = height;
    if (!canvas->fitted && width > 1) {
        app_canvas_fit(canvas);
        canvas->fitted = true;
    }
    const AppViewFlags *flags = &canvas->document->view;
    AppRenderOptions options = app_render_screen_options();
    options.show_tiles = flags->tiles;
    options.show_clips = flags->clips;
    options.show_numbers = flags->numbers;
    options.show_dims = flags->dims;
    options.show_grid = flags->grid;
    options.highlighted_outline = app_selection_outline(&canvas->document->selection);
    app_render_plan(cr, canvas->document->project, &canvas->view, &options);
    if (canvas->tool_id == APP_TOOL_SELECT)
        draw_hover(canvas, cr);
    draw_selection(canvas, cr);
    if (canvas->tool->draw)
        canvas->tool->draw(canvas, cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    app_canvas_render(data, cr, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));
    return FALSE;
}

/* ---- мышь ----------------------------------------------------------------- */

static AppPointer pointer_from(AppCanvas *canvas, double x, double y, guint button, GdkModifierType state)
{
    TlVec2 screen = tl_vec2(x, y);
    return (AppPointer){screen, app_view_to_world(&canvas->view, screen), button,
                        (state & GDK_SHIFT_MASK) != 0, (state & GDK_CONTROL_MASK) != 0};
}

void app_canvas_start_pan(AppCanvas *canvas, TlVec2 screen)
{
    canvas->panning = true;
    canvas->pan_origin_screen = screen;
    canvas->pan_origin_x = canvas->view.pan_x;
    canvas->pan_origin_y = canvas->view.pan_y;
    app_canvas_set_cursor(canvas, "grabbing");
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    AppCanvas *canvas = data;
    gtk_widget_grab_focus(widget);
    AppPointer pointer = pointer_from(canvas, event->x, event->y, event->button, event->state);
    if (event->type == GDK_2BUTTON_PRESS) {
        if (event->button == 1 && canvas->tool->double_click)
            canvas->tool->double_click(canvas, &pointer);
        return TRUE;
    }
    if (event->type != GDK_BUTTON_PRESS)
        return TRUE;
    if (event->button == 3 && canvas->tool->secondary && canvas->tool->secondary(canvas)) {
        app_canvas_redraw(canvas);
        return TRUE;
    }
    if (event->button == 2 || event->button == 3) {
        app_canvas_start_pan(canvas, pointer.screen);
        return TRUE;
    }
    if (event->button == 1 && canvas->tool->press)
        canvas->tool->press(canvas, &pointer);
    return TRUE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    AppCanvas *canvas = data;
    AppPointer pointer = pointer_from(canvas, event->x, event->y, 0, event->state);
    canvas->cursor = pointer.world;
    if (canvas->panning) {
        canvas->view.pan_x = canvas->pan_origin_x + pointer.screen.x - canvas->pan_origin_screen.x;
        canvas->view.pan_y = canvas->pan_origin_y + pointer.screen.y - canvas->pan_origin_screen.y;
        app_canvas_redraw(canvas);
        return TRUE;
    }
    if (canvas->tool->motion)
        canvas->tool->motion(canvas, &pointer);
    if (canvas->pointer_listener)
        canvas->pointer_listener(canvas->cursor, canvas->pointer_listener_data);
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    AppCanvas *canvas = data;
    AppPointer pointer = pointer_from(canvas, event->x, event->y, event->button, event->state);
    if (canvas->panning) {
        canvas->panning = false;
        app_canvas_set_cursor(canvas, canvas->tool->cursor);
        return TRUE;
    }
    if (event->button == 1 && canvas->tool->release)
        canvas->tool->release(canvas, &pointer);
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    AppCanvas *canvas = data;
    double factor = 1.0;
    if (event->direction == GDK_SCROLL_UP)
        factor = ZOOM_STEP;
    else if (event->direction == GDK_SCROLL_DOWN)
        factor = 1 / ZOOM_STEP;
    else if (event->direction == GDK_SCROLL_SMOOTH)
        factor = event->delta_y < 0 ? ZOOM_STEP : event->delta_y > 0 ? 1 / ZOOM_STEP : 1.0;
    app_view_zoom_at(&canvas->view, factor, tl_vec2(event->x, event->y));
    app_canvas_redraw(canvas);
    return TRUE;
}

/* ---- клавиатура ------------------------------------------------------------- */

static bool nudge_with_arrows(AppCanvas *canvas, GdkEventKey *event)
{
    double dx = 0, dy = 0;
    switch (event->keyval) {
    case GDK_KEY_Left: dx = -1; break;
    case GDK_KEY_Right: dx = 1; break;
    case GDK_KEY_Up: dy = -1; break;
    case GDK_KEY_Down: dy = 1; break;
    default: return false;
    }
    if (canvas->document->selection.kind == APP_SELECT_NONE)
        return false;
    double step = (event->state & GDK_SHIFT_MASK) ? 1.0
                  : canvas->document->preferences.snap_step > 0 ? canvas->document->preferences.snap_step
                                                                 : 1.0;
    bool tiles = (event->state & GDK_CONTROL_MASK) || canvas->tool_id == APP_TOOL_TILES;
    app_command_nudge(canvas->document, dx * step, dy * step, tiles);
    return true;
}

static bool switch_tool_by_letter(AppCanvas *canvas, gunichar character)
{
    static const struct {
        gunichar latin, cyrillic;
        int tool; /* -1 — «Вписать» */
    } LETTERS[] = {{'v', 0x043c, APP_TOOL_SELECT},  {'r', 0x043a, APP_TOOL_RECT},
                   {'p', 0x0437, APP_TOOL_CONTOUR}, {'w', 0x0446, APP_TOOL_PARTITION},
                   {'o', 0x0449, APP_TOOL_OPENING}, {'t', 0x0435, APP_TOOL_TILES},
                   {'f', 0x0430, -1}};
    gunichar lower = g_unichar_tolower(character);
    for (size_t i = 0; i < sizeof LETTERS / sizeof LETTERS[0]; i++)
        if (lower == LETTERS[i].latin || lower == LETTERS[i].cyrillic) {
            if (LETTERS[i].tool < 0)
                app_canvas_fit(canvas);
            else
                app_canvas_set_tool(canvas, (AppToolId)LETTERS[i].tool);
            return true;
        }
    return false;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    AppCanvas *canvas = data;
    gunichar character = gdk_keyval_to_unicode(event->keyval);
    if (canvas->tool->key && canvas->tool->key(canvas, event->keyval, character))
        return TRUE;
    if (event->keyval == GDK_KEY_Delete) {
        app_command_delete_selection(canvas->document);
        return TRUE;
    }
    if (nudge_with_arrows(canvas, event))
        return TRUE;
    if (!(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) && character && switch_tool_by_letter(canvas, character))
        return TRUE;
    return FALSE;
}

/* ---- публичное ---------------------------------------------------------------- */

static void on_document_changed(AppDocument *document, unsigned changes, void *data)
{
    if (changes & (APP_CHANGED_MODEL | APP_CHANGED_SELECTION | APP_CHANGED_VIEW))
        app_canvas_redraw(data);
}

AppCanvas *app_canvas_new(AppDocument *document)
{
    AppCanvas *canvas = g_new0(AppCanvas, 1);
    canvas->document = document;
    canvas->view = (AppView){0.1, 60, 60, 800, 600};
    canvas->tool = &APP_SELECT_TOOL;
    canvas->area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(canvas->area, TRUE);
    gtk_widget_set_size_request(canvas->area, 400, 300);
    gtk_widget_add_events(canvas->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                            GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
                                            GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(canvas->area, "draw", G_CALLBACK(on_draw), canvas);
    g_signal_connect(canvas->area, "button-press-event", G_CALLBACK(on_button_press), canvas);
    g_signal_connect(canvas->area, "button-release-event", G_CALLBACK(on_button_release), canvas);
    g_signal_connect(canvas->area, "motion-notify-event", G_CALLBACK(on_motion), canvas);
    g_signal_connect(canvas->area, "scroll-event", G_CALLBACK(on_scroll), canvas);
    g_signal_connect(canvas->area, "key-press-event", G_CALLBACK(on_key_press), canvas);
    app_document_add_listener(document, on_document_changed, canvas);
    return canvas;
}

GtkWidget *app_canvas_widget(AppCanvas *canvas) { return canvas->area; }

void app_canvas_set_tool(AppCanvas *canvas, AppToolId tool)
{
    canvas->tool_id = tool;
    canvas->tool = TOOLS[tool];
    tl_polygon_clear(&canvas->draw_points);
    canvas->typed[0] = '\0';
    canvas->has_rect_start = canvas->has_wall_start = canvas->has_opening_preview = false;
    canvas->wall_snap.on_face = false;
    canvas->hover = (AppSelection){0};
    if (canvas->tool->activate)
        canvas->tool->activate(canvas);
    app_canvas_set_cursor(canvas, canvas->tool->cursor);
    gtk_widget_grab_focus(canvas->area);
    if (canvas->tool_listener)
        canvas->tool_listener(tool, canvas->tool_listener_data);
    app_canvas_redraw(canvas);
}

AppToolId app_canvas_tool(const AppCanvas *canvas) { return canvas->tool_id; }

void app_canvas_on_tool_changed(AppCanvas *canvas, AppCanvasToolListener listener, void *user_data)
{
    canvas->tool_listener = listener;
    canvas->tool_listener_data = user_data;
}

void app_canvas_on_pointer(AppCanvas *canvas, AppCanvasPointerListener listener, void *user_data)
{
    canvas->pointer_listener = listener;
    canvas->pointer_listener_data = user_data;
}

void app_canvas_fit(AppCanvas *canvas)
{
    TlRect bounds;
    if (app_render_plan_bounds(canvas->document->project, &bounds))
        app_view_fit(&canvas->view, bounds, FIT_MARGIN);
    else
        canvas->view = (AppView){0.1, 60, 60, canvas->view.width, canvas->view.height};
    app_canvas_redraw(canvas);
}

void app_canvas_zoom(AppCanvas *canvas, double factor)
{
    app_view_zoom_at(&canvas->view, factor, tl_vec2(canvas->view.width / 2, canvas->view.height / 2));
    app_canvas_redraw(canvas);
}

void app_canvas_escape(AppCanvas *canvas)
{
    if (canvas->tool->cancel && canvas->tool->cancel(canvas))
        app_canvas_redraw(canvas);
    else if (canvas->tool_id != APP_TOOL_SELECT)
        app_canvas_set_tool(canvas, APP_TOOL_SELECT);
    else
        app_document_clear_selection(canvas->document);
}

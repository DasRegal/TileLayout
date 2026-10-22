#include "app_draw.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>

#define FONT_FAMILY "Sans"

static bool mono_mode;

/* ---- цвета ------------------------------------------------------------- */

AppColor app_color_rgba(int red, int green, int blue, double alpha)
{
    return (AppColor){red / 255.0, green / 255.0, blue / 255.0, alpha};
}

AppColor app_color_hex(const char *hex)
{
    unsigned int red, green, blue;
    if (hex && sscanf(hex, "#%02x%02x%02x", &red, &green, &blue) == 3)
        return app_color_rgba((int)red, (int)green, (int)blue, 1.0);
    return app_color_rgba(128, 128, 128, 1.0);
}

AppColor app_color_darker(AppColor color, double factor)
{
    return (AppColor){color.red / factor, color.green / factor, color.blue / factor, color.alpha};
}

void app_draw_set_mono(bool mono) { mono_mode = mono; }

bool app_draw_is_mono(void) { return mono_mode; }

void app_set_color(cairo_t *cr, AppColor color)
{
    if (mono_mode)
        cairo_set_source_rgb(cr, 0, 0, 0);
    else
        cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
}

void app_set_fill_color(cairo_t *cr, AppColor color)
{
    if (mono_mode)
        cairo_set_source_rgb(cr, 1, 1, 1);
    else
        cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
}

/* ---- контуры ------------------------------------------------------------- */

static void move_or_line(cairo_t *cr, const AppView *view, TlVec2 world, bool first)
{
    TlVec2 screen = app_view_to_screen(view, world);
    if (first)
        cairo_move_to(cr, screen.x, screen.y);
    else
        cairo_line_to(cr, screen.x, screen.y);
}

void app_path_polygon(cairo_t *cr, const AppView *view, const TlPolygon *polygon)
{
    for (size_t i = 0; i < polygon->count; i++)
        move_or_line(cr, view, polygon->items[i], i == 0);
    if (polygon->count)
        cairo_close_path(cr);
}

void app_path_polygon_with_holes(cairo_t *cr, const AppView *view, const TlPolygon *outline,
                                 const TlPolygonList *holes)
{
    app_path_polygon(cr, view, outline);
    for (size_t i = 0; holes && i < holes->count; i++)
        app_path_polygon(cr, view, &holes->items[i]);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
}

void app_path_line(cairo_t *cr, const AppView *view, TlVec2 from, TlVec2 to)
{
    move_or_line(cr, view, from, true);
    move_or_line(cr, view, to, false);
}

void app_stroke(cairo_t *cr, AppColor color, double width_px)
{
    app_set_color(cr, color);
    cairo_set_line_width(cr, width_px);
    cairo_set_dash(cr, NULL, 0, 0);
    cairo_stroke(cr);
}

void app_stroke_dashed(cairo_t *cr, AppColor color, double width_px, double dash_px)
{
    app_set_color(cr, color);
    cairo_set_line_width(cr, width_px);
    double dashes[2] = {dash_px, dash_px};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
}

void app_rounded_rect(cairo_t *cr, double x, double y, double width, double height, double radius)
{
    radius = fmin(radius, fmin(width, height) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, x + radius, y + height - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

/* ---- текст --------------------------------------------------------------- */

static PangoLayout *make_layout(cairo_t *cr, const char *text, const AppTextStyle *style)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(FONT_FAMILY);
    pango_font_description_set_absolute_size(font, style->size_px * PANGO_SCALE);
    pango_font_description_set_weight(font, style->bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, text, -1);
    return layout;
}

static cairo_t *measuring_context(void)
{
    static cairo_t *cr;
    if (!cr) {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cr = cairo_create(surface);
        cairo_surface_destroy(surface);
    }
    return cr;
}

void app_text_size(const char *text, const AppTextStyle *style, double *width, double *height)
{
    PangoLayout *layout = make_layout(measuring_context(), text, style);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    if (width)
        *width = w;
    if (height)
        *height = h;
}

static void draw_layout_centered(cairo_t *cr, PangoLayout *layout, const AppTextStyle *style)
{
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    if (style->background.alpha > 0) {
        app_rounded_rect(cr, -w / 2.0 - 3, -h / 2.0, w + 6, h, 3);
        app_set_fill_color(cr, mono_mode ? (AppColor){1, 1, 1, 1} : style->background);
        cairo_fill(cr);
    }
    app_set_color(cr, style->color);
    cairo_move_to(cr, -w / 2.0, -h / 2.0);
    pango_cairo_show_layout(cr, layout);
    cairo_new_path(cr); /* иначе следующая фигура продолжится линией от текста */
}

void app_text_centered(cairo_t *cr, const char *text, const AppTextStyle *style, TlVec2 center,
                       double angle_degrees)
{
    PangoLayout *layout = make_layout(cr, text, style);
    cairo_save(cr);
    cairo_translate(cr, center.x, center.y);
    cairo_rotate(cr, angle_degrees * G_PI / 180.0);
    draw_layout_centered(cr, layout, style);
    cairo_restore(cr);
    g_object_unref(layout);
}

void app_text_along(cairo_t *cr, const char *text, const AppTextStyle *style, TlVec2 a, TlVec2 b,
                    TlVec2 normal, double offset_px)
{
    TlVec2 middle = tl_vec2_add(tl_vec2_midpoint(a, b), tl_vec2_scale(normal, offset_px));
    double angle = atan2(b.y - a.y, b.x - a.x) * 180.0 / G_PI;
    if (angle > 90 || angle <= -90)
        angle += 180;
    app_text_centered(cr, text, style, middle, angle);
}

void app_text_box(cairo_t *cr, const char *const *lines, size_t line_count, double size_px,
                  TlVec2 center)
{
    double widths[8], heights[8], box_width = 0, box_height = 6;
    line_count = line_count > 8 ? 8 : line_count;
    for (size_t i = 0; i < line_count; i++) {
        AppTextStyle style = {size_px, i == 0, APP_TRANSPARENT, APP_TRANSPARENT};
        app_text_size(lines[i], &style, &widths[i], &heights[i]);
        box_width = fmax(box_width, widths[i] + 14);
        box_height += heights[i];
    }
    double x = center.x - box_width / 2, y = center.y - box_height / 2;
    app_rounded_rect(cr, x, y, box_width, box_height, 4);
    app_set_fill_color(cr, app_color_rgba(255, 255, 255, 0.88));
    cairo_fill_preserve(cr);
    app_set_color(cr, app_color_hex("#9aa3ad"));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    double line_y = y + 3;
    for (size_t i = 0; i < line_count; i++) {
        AppTextStyle style = {size_px, i == 0, app_color_hex("#20242a"), APP_TRANSPARENT};
        app_text_centered(cr, lines[i], &style, tl_vec2(center.x, line_y + heights[i] / 2), 0);
        line_y += heights[i];
    }
}

void app_text_in_rect(cairo_t *cr, const char *text, const AppTextStyle *style, double x, double y,
                      double width, double height, int align)
{
    PangoLayout *layout = make_layout(cr, text, style);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, align < 0   ? PANGO_ALIGN_LEFT
                                       : align > 0 ? PANGO_ALIGN_RIGHT
                                                   : PANGO_ALIGN_CENTER);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    app_set_color(cr, style->color);
    cairo_move_to(cr, x, y + (height - h) / 2);
    pango_cairo_show_layout(cr, layout);
    cairo_new_path(cr); /* иначе следующая фигура продолжится линией от текста */
    g_object_unref(layout);
}

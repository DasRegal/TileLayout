#include "app_icons.h"

#include <gdk/gdk.h>
#include <math.h>
#include <string.h>

#include "app_draw.h"

/* Все иконки рисуются на поле 32×32. */

static void ink(cairo_t *cr, const char *hex, double width)
{
    AppColor c = app_color_hex(hex);
    cairo_set_source_rgba(cr, c.red, c.green, c.blue, 1);
    cairo_set_line_width(cr, width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
}

static void fill_hex(cairo_t *cr, const char *hex)
{
    AppColor c = app_color_hex(hex);
    cairo_set_source_rgba(cr, c.red, c.green, c.blue, 1);
}

static void polyline(cairo_t *cr, const double *xy, size_t count, bool closed)
{
    for (size_t i = 0; i < count; i++)
        (i ? cairo_line_to : cairo_move_to)(cr, xy[2 * i], xy[2 * i + 1]);
    if (closed)
        cairo_close_path(cr);
}

static void arrow_head(cairo_t *cr, double x, double y, double degrees, double size)
{
    double a = degrees * G_PI / 180;
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x - size * cos(a - 0.5), y - size * sin(a - 0.5));
    cairo_line_to(cr, x - size * cos(a + 0.5), y - size * sin(a + 0.5));
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void tiles_grid(cairo_t *cr, double x0, double y0, int n, double size, double gap)
{
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            fill_hex(cr, (i + j) % 2 == 0 ? "#d9c9a8" : "#b8a888");
            cairo_rectangle(cr, x0 + i * (size + gap), y0 + j * (size + gap), size, size);
            cairo_fill(cr);
        }
}

static void draw_undo(cairo_t *cr, bool mirrored)
{
    if (mirrored) {
        cairo_translate(cr, 32, 0);
        cairo_scale(cr, -1, 1);
    }
    ink(cr, "#37474f", 2.6);
    cairo_move_to(cr, 9, 13);
    cairo_curve_to(cr, 16, 6, 28, 9, 26, 20);
    cairo_curve_to(cr, 25, 25, 20, 27, 15, 27);
    cairo_stroke(cr);
    arrow_head(cr, 6, 15, 150, 8);
}

static void draw_select(cairo_t *cr)
{
    const double xy[] = {9, 4, 9, 26, 14, 21, 18, 29, 21, 27.5, 17, 20, 24, 20};
    polyline(cr, xy, 7, true);
    fill_hex(cr, "#ffffff");
    cairo_fill_preserve(cr);
    ink(cr, "#37474f", 1.8);
    cairo_stroke(cr);
}

static void walls(cairo_t *cr, const double *xy, size_t count, double width)
{
    polyline(cr, xy, count, true);
    ink(cr, "#6f7681", width);
    cairo_stroke(cr);
}

static void handles(cairo_t *cr, const double *xy, size_t count, double radius)
{
    for (size_t i = 0; i < count; i++) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, xy[2 * i], xy[2 * i + 1], radius, 0, 2 * G_PI);
        fill_hex(cr, "#ffffff");
        cairo_fill_preserve(cr);
        ink(cr, "#0b8043", 1.5);
        cairo_stroke(cr);
    }
}

static void draw_rect(cairo_t *cr)
{
    fill_hex(cr, "#d9c9a8");
    cairo_rectangle(cr, 6, 8, 20, 16);
    cairo_fill(cr);
    const double xy[] = {5, 7, 27, 7, 27, 25, 5, 25};
    walls(cr, xy, 4, 4);
    const double corners[] = {5, 7, 27, 25};
    handles(cr, corners, 2, 2.6);
}

static void draw_contour(cairo_t *cr)
{
    const double xy[] = {5, 6, 20, 6, 20, 14, 27, 14, 27, 26, 5, 26};
    polyline(cr, xy, 6, true);
    fill_hex(cr, "#d9c9a8");
    cairo_fill(cr);
    walls(cr, xy, 6, 3.5);
    handles(cr, xy, 6, 2.2);
}

static void draw_partition(cairo_t *cr)
{
    fill_hex(cr, "#eef0f2");
    cairo_rectangle(cr, 4, 4, 24, 24);
    cairo_fill(cr);
    const double xy[] = {4, 4, 28, 4, 28, 28, 4, 28};
    walls(cr, xy, 4, 3);
    fill_hex(cr, "#6f7681");
    cairo_rectangle(cr, 14, 4, 4.5, 17);
    cairo_fill(cr);
    cairo_arc(cr, 16.2, 21, 2.6, 0, 2 * G_PI);
    fill_hex(cr, "#ffffff");
    cairo_fill_preserve(cr);
    ink(cr, "#1a73e8", 1.6);
    cairo_stroke(cr);
}

/* Две плитки и шов между ними — пунктир в щели. */
static void draw_expansion_joint(cairo_t *cr)
{
    fill_hex(cr, "#d9c9a8");
    cairo_rectangle(cr, 3, 5, 11, 22);
    cairo_rectangle(cr, 18, 5, 11, 22);
    cairo_fill(cr);
    ink(cr, "#37414d", 1.3);
    cairo_rectangle(cr, 3, 5, 11, 22);
    cairo_rectangle(cr, 18, 5, 11, 22);
    cairo_stroke(cr);
    ink(cr, "#37414d", 1.4);
    const double dashes[2] = {2.5, 2};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_move_to(cr, 16, 3);
    cairo_line_to(cr, 16, 29);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
}

/* Плитка с отверстием под трубу: круг коронки с крестом центра. */
static void draw_pipe(cairo_t *cr)
{
    fill_hex(cr, "#d9c9a8");
    cairo_rectangle(cr, 4, 4, 24, 24);
    cairo_fill(cr);
    ink(cr, "#8a7a5c", 1.2);
    cairo_rectangle(cr, 4, 4, 24, 24);
    cairo_stroke(cr);
    cairo_arc(cr, 16, 16, 7, 0, 2 * G_PI);
    fill_hex(cr, "#e8eef6");
    cairo_fill_preserve(cr);
    ink(cr, "#3d5a80", 1.8);
    cairo_stroke(cr);
    cairo_move_to(cr, 12, 16);
    cairo_line_to(cr, 20, 16);
    cairo_move_to(cr, 16, 12);
    cairo_line_to(cr, 16, 20);
    ink(cr, "#3d5a80", 1.2);
    cairo_stroke(cr);
}

/* Плоская линейка по диагонали с делениями. */
static void draw_ruler(cairo_t *cr)
{
    cairo_save(cr);
    cairo_translate(cr, 16, 16);
    cairo_rotate(cr, -G_PI / 4);
    app_rounded_rect(cr, -14, -5, 28, 10, 1.5);
    fill_hex(cr, "#f1d48a");
    cairo_fill_preserve(cr);
    ink(cr, "#5f6b7a", 1.4);
    cairo_stroke(cr);
    for (int i = 0; i < 7; i++) {
        double x = -10.5 + i * 3.5;
        cairo_move_to(cr, x, -5);
        cairo_line_to(cr, x, i % 2 == 0 ? -0.5 : -2.5);
    }
    ink(cr, "#5f6b7a", 1.1);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_opening(cairo_t *cr)
{
    fill_hex(cr, "#6f7681");
    cairo_rectangle(cr, 2, 14, 9, 6);
    cairo_rectangle(cr, 21, 14, 9, 6);
    cairo_fill(cr);
    ink(cr, "#37474f", 2);
    cairo_move_to(cr, 11, 14);
    cairo_line_to(cr, 11, 4);
    cairo_stroke(cr);
    double dashes[2] = {2, 2};
    cairo_set_dash(cr, dashes, 2, 0);
    ink(cr, "#37474f", 1.3);
    cairo_arc(cr, 11, 14, 10, -G_PI / 2, 0);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
    ink(cr, "#e8590c", 2);
    cairo_move_to(cr, 12, 26);
    cairo_line_to(cr, 20, 26);
    cairo_stroke(cr);
    arrow_head(cr, 21, 26, 0, 4.5);
    arrow_head(cr, 11, 26, 180, 4.5);
}

static void draw_tile_move(cairo_t *cr)
{
    tiles_grid(cr, 3, 3, 3, 8, 1.2);
    ink(cr, "#1a73e8", 2.2);
    for (int k = 0; k < 4; k++) {
        double a = k * G_PI / 2;
        double tx = 16 + 11 * cos(a), ty = 16 + 11 * sin(a);
        cairo_move_to(cr, 16, 16);
        cairo_line_to(cr, tx, ty);
        cairo_stroke(cr);
        arrow_head(cr, tx, ty, k * 90, 5);
    }
}

static void draw_fit(cairo_t *cr)
{
    ink(cr, "#37474f", 2.4);
    const double corners[4][4] = {{5, 5, 1, 1}, {27, 5, -1, 1}, {5, 27, 1, -1}, {27, 27, -1, -1}};
    for (int i = 0; i < 4; i++) {
        cairo_move_to(cr, corners[i][0] + 7 * corners[i][2], corners[i][1]);
        cairo_line_to(cr, corners[i][0], corners[i][1]);
        cairo_line_to(cr, corners[i][0], corners[i][1] + 7 * corners[i][3]);
    }
    cairo_stroke(cr);
    fill_hex(cr, "#b8a888");
    cairo_rectangle(cr, 11, 11, 10, 10);
    cairo_fill(cr);
}

static void draw_svp(cairo_t *cr)
{
    tiles_grid(cr, 3, 3, 2, 12.5, 1.5);
    fill_hex(cr, "#e8590c");
    const double points[4][2] = {{16.2, 9}, {16.2, 23}, {9, 16.2}, {23, 16.2}};
    for (int i = 0; i < 4; i++)
        cairo_rectangle(cr, points[i][0] - 2.6, points[i][1] - 2.6, 5.2, 5.2);
    cairo_fill(cr);
}

static void draw_numbers(cairo_t *cr)
{
    tiles_grid(cr, 3, 3, 2, 12.5, 1.5);
    const char *digits[4] = {"1", "2", "3", "4"};
    const double origins[4][2] = {{3, 3}, {17, 3}, {3, 17}, {17, 17}};
    for (int k = 0; k < 4; k++) {
        AppTextStyle style = {10, true, app_color_hex("#37474f"), APP_TRANSPARENT};
        app_text_centered(cr, digits[k], &style, tl_vec2(origins[k][0] + 6.25, origins[k][1] + 6.25), 0);
    }
}

static void draw_lock(cairo_t *cr, bool closed)
{
    ink(cr, "#37474f", 2.6);
    if (closed) {
        cairo_arc(cr, 16, 11, 6, G_PI, 2 * G_PI);
        cairo_move_to(cr, 10, 11);
        cairo_line_to(cr, 10, 15);
        cairo_move_to(cr, 22, 11);
        cairo_line_to(cr, 22, 15);
    } else {
        cairo_arc(cr, 21, 9, 6, G_PI, 2 * G_PI);
        cairo_move_to(cr, 27, 9);
        cairo_line_to(cr, 27, 13);
    }
    cairo_stroke(cr);
    app_rounded_rect(cr, 7, 14, 18, 14, 2.5);
    fill_hex(cr, closed ? "#f2b400" : "#cfd8dc");
    cairo_fill_preserve(cr);
    ink(cr, "#37474f", 1.5);
    cairo_stroke(cr);
    cairo_arc(cr, 16, 20, 1.8, 0, 2 * G_PI);
    fill_hex(cr, "#37474f");
    cairo_fill(cr);
}

static void draw_house(cairo_t *cr)
{
    const double xy[] = {6, 15, 16, 6, 26, 15, 26, 27, 6, 27};
    polyline(cr, xy, 5, true);
    fill_hex(cr, "#d9c9a8");
    cairo_fill_preserve(cr);
    ink(cr, "#37474f", 2);
    cairo_stroke(cr);
    ink(cr, "#0b8043", 2.6);
    cairo_move_to(cr, 16, 15);
    cairo_line_to(cr, 16, 24);
    cairo_move_to(cr, 11.5, 19.5);
    cairo_line_to(cr, 20.5, 19.5);
    cairo_stroke(cr);
}

static void draw_delete(cairo_t *cr)
{
    ink(cr, "#d93025", 3);
    cairo_move_to(cr, 8, 8);
    cairo_line_to(cr, 24, 24);
    cairo_move_to(cr, 24, 8);
    cairo_line_to(cr, 8, 24);
    cairo_stroke(cr);
}

static void draw_heart(cairo_t *cr)
{
    ink(cr, "#c8506a", 2.5);
    cairo_move_to(cr, 16, 26);
    cairo_curve_to(cr, 9, 21, 5, 17, 5, 12);
    cairo_curve_to(cr, 5, 6, 13, 5, 16, 11);
    cairo_curve_to(cr, 19, 5, 27, 6, 27, 12);
    cairo_curve_to(cr, 27, 17, 23, 21, 16, 26);
    cairo_close_path(cr);
    cairo_stroke(cr);
}

static void draw_magic(cairo_t *cr)
{
    ink(cr, "#37474f", 2.8);
    cairo_move_to(cr, 6, 26);
    cairo_line_to(cr, 20, 12);
    cairo_stroke(cr);
    ink(cr, "#e8590c", 1.8);
    const double sparks[3][3] = {{23, 8, 4}, {27, 17, 2.5}, {14, 5, 2.5}};
    for (int i = 0; i < 3; i++) {
        cairo_move_to(cr, sparks[i][0] - sparks[i][2], sparks[i][1]);
        cairo_line_to(cr, sparks[i][0] + sparks[i][2], sparks[i][1]);
        cairo_move_to(cr, sparks[i][0], sparks[i][1] - sparks[i][2]);
        cairo_line_to(cr, sparks[i][0], sparks[i][1] + sparks[i][2]);
    }
    cairo_stroke(cr);
}

static void draw_print(cairo_t *cr)
{
    ink(cr, "#37474f", 1.8);
    cairo_rectangle(cr, 9, 3, 14, 9);
    fill_hex(cr, "#ffffff");
    cairo_fill_preserve(cr);
    ink(cr, "#37474f", 1.8);
    cairo_stroke(cr);
    app_rounded_rect(cr, 4, 11, 24, 12, 2);
    fill_hex(cr, "#cfd8dc");
    cairo_fill_preserve(cr);
    ink(cr, "#37474f", 1.8);
    cairo_stroke(cr);
    cairo_rectangle(cr, 9, 18, 14, 11);
    fill_hex(cr, "#ffffff");
    cairo_fill_preserve(cr);
    ink(cr, "#37474f", 1.8);
    cairo_stroke(cr);
    ink(cr, "#1a73e8", 1.4);
    cairo_move_to(cr, 12, 22);
    cairo_line_to(cr, 20, 22);
    cairo_move_to(cr, 12, 25.5);
    cairo_line_to(cr, 18, 25.5);
    cairo_stroke(cr);
}

typedef struct IconEntry {
    const char *name;
    void (*draw)(cairo_t *cr);
} IconEntry;

/* Значок программы: буквы TL из квадратной плитки на сетке 7×5 (как packaging/linux/tilelayout.svg). */
static void draw_app(cairo_t *cr)
{
    static const char *const GLYPHS[5] = {"###.#..", ".#..#..", ".#..#..", ".#..#..", ".#..###"};
    fill_hex(cr, "#1f5f6b");
    app_rounded_rect(cr, 1, 1, 30, 30, 5);
    cairo_fill(cr);
    for (int row = 0; row < 5; row++)
        for (int column = 0; column < 7; column++) {
            if (GLYPHS[row][column] == '#')
                fill_hex(cr, (row + column) % 2 ? "#f6f1e6" : "#e6dbc2");
            else
                cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
            app_rounded_rect(cr, 3.575 + column * 3.575, 7.3 + row * 3.575, 3.15, 3.15, 0.4);
            cairo_fill(cr);
        }
}

static void draw_undo_icon(cairo_t *cr) { draw_undo(cr, false); }
static void draw_redo_icon(cairo_t *cr) { draw_undo(cr, true); }
static void draw_covering(cairo_t *cr) { tiles_grid(cr, 3, 3, 3, 8, 1.5); }
static void draw_lock_closed(cairo_t *cr) { draw_lock(cr, true); }
static void draw_lock_open(cairo_t *cr) { draw_lock(cr, false); }

static const IconEntry ICONS[] = {
    {"app", draw_app}, {"undo", draw_undo_icon}, {"redo", draw_redo_icon}, {"select", draw_select},
    {"rect", draw_rect}, {"contour", draw_contour}, {"partition", draw_partition},
    {"opening", draw_opening}, {"tile_move", draw_tile_move}, {"fit", draw_fit},
    {"covering", draw_covering}, {"svp", draw_svp}, {"numbers", draw_numbers},
    {"lock", draw_lock_closed}, {"unlock", draw_lock_open}, {"house", draw_house},
    {"delete", draw_delete}, {"magic", draw_magic}, {"print", draw_print}, {"heart", draw_heart},
    {"ruler", draw_ruler}, {"expansion_joint", draw_expansion_joint}, {"pipe", draw_pipe},
};

void app_icon_draw(cairo_t *cr, const char *name, double x, double y, double size)
{
    for (size_t i = 0; i < sizeof ICONS / sizeof ICONS[0]; i++)
        if (strcmp(ICONS[i].name, name) == 0) {
            cairo_save(cr);
            cairo_translate(cr, x, y);
            cairo_scale(cr, size / 32.0, size / 32.0);
            cairo_new_path(cr);
            ICONS[i].draw(cr);
            cairo_restore(cr);
            return;
        }
}

GdkPixbuf *app_icon_pixbuf(const char *name, int size)
{
    static GHashTable *cache;
    if (!cache)
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    char *key = g_strdup_printf("%s@%d", name, size);
    GdkPixbuf *pixbuf = g_hash_table_lookup(cache, key);
    if (pixbuf) {
        g_free(key);
        return pixbuf;
    }
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(surface);
    app_icon_draw(cr, name, 0, 0, size);
    cairo_destroy(cr);
    pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);
    cairo_surface_destroy(surface);
    g_hash_table_insert(cache, key, pixbuf);
    return pixbuf;
}

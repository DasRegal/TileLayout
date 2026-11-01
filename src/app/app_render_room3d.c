/* Объёмный вид комнаты (tl_room3d.h): ядро даёт многоугольники в порядке рисования — здесь их заливают. */
#include "app_render.h"

#include <math.h>

#include "app_catalogs.h"
#include "app_draw.h"
#include "tl_room3d.h"

/* Контур закраски (дыры — проёмы, заливка «чёт-нечет»). */
static void paint_path(cairo_t *cr, const TlPaint *paint)
{
    cairo_new_path(cr);
    for (size_t c = 0; c < paint->contours.count; c++) {
        const TlPolygon *contour = &paint->contours.items[c];
        for (size_t k = 0; k < contour->count; k++)
            (k ? cairo_line_to : cairo_move_to)(cr, contour->items[k].x, contour->items[k].y);
        cairo_close_path(cr);
    }
}

void app_render_room3d(cairo_t *cr, TlProject *project, TlSpace *space, TlCamera *camera, double width,
                       double height)
{
    cairo_pattern_t *sky = cairo_pattern_create_linear(0, 0, 0, height); /* фон: светлее к низу */
    bool dark = app_draw_is_dark();
    cairo_pattern_add_color_stop_rgb(sky, 0, dark ? 0.10 : 0.88, dark ? 0.11 : 0.9, dark ? 0.13 : 0.93);
    cairo_pattern_add_color_stop_rgb(sky, 1, dark ? 0.16 : 0.97, dark ? 0.18 : 0.97, dark ? 0.21 : 0.98);
    cairo_set_source(cr, sky);
    cairo_paint(cr);
    cairo_pattern_destroy(sky);
    TlPaintList paints = tl_room3d_paint(project, space, camera, width, height);
    cairo_save(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (size_t i = 0; i < paints.count; i++) {
        const TlPaint *paint = &paints.items[i];
        cairo_new_path(cr);
        for (size_t c = 0; c < paint->contours.count; c++) {
            const TlPolygon *contour = &paint->contours.items[c];
            for (size_t k = 0; k < contour->count; k++)
                (k ? cairo_line_to : cairo_move_to)(cr, contour->items[k].x, contour->items[k].y);
            cairo_close_path(cr);
        }
        app_set_fill_color(cr, app_color_hex(paint->fill)); /* цвет — и под рисунком */
        cairo_surface_t *image = paint->texture[0] ? app_catalogs_image(paint->texture) : NULL;
        if (image) { /* рисунок плитки — по клеткам (перспектива), поверх — свет */
            cairo_fill_preserve(cr);
            double iw = cairo_image_surface_get_width(image), ih = cairo_image_surface_get_height(image);
            bool turned = !((iw >= ih) == (paint->tile_width >= paint->tile_height) ||
                            fabs(paint->tile_width - paint->tile_height) < 0.5);
            for (size_t k = 0; k < paint->cell_count; k++) {
                const TlTextureCell *cell = &paint->cells[k];
                cairo_save(cr);
                paint_path(cr, paint); /* путь в save не входит — заново у каждой клетки */
                cairo_clip(cr);
                for (int q = 0; q < 4; q++)
                    (q ? cairo_line_to : cairo_move_to)(cr, cell->quad[q].x, cell->quad[q].y);
                cairo_close_path(cr);
                cairo_clip(cr);
                cairo_matrix_t to_screen;
                cairo_matrix_init(&to_screen, cell->matrix[0], cell->matrix[1], cell->matrix[2], cell->matrix[3],
                                  cell->matrix[4], cell->matrix[5]);
                cairo_transform(cr, &to_screen);
                if (turned) { /* плитка поперёк картинки — рисунок повёрнут, как на плане */
                    cairo_matrix_t turn;
                    cairo_matrix_init(&turn, 0, 1, -1, 0, 1, 0);
                    cairo_transform(cr, &turn);
                }
                cairo_scale(cr, 1 / iw, 1 / ih);
                cairo_set_source_surface(cr, image, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            paint_path(cr, paint);
            cairo_set_source_rgba(cr, 0, 0, 0, fmax(0, 1 - paint->shade));
        }
        if (paint->stroke[0]) {
            cairo_fill_preserve(cr);
            app_set_color(cr, app_color_hex(paint->stroke));
            cairo_set_line_width(cr, paint->stroke_width);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
    tl_paint_list_free_all(&paints);
}

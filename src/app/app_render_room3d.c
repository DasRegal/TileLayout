/* Объёмный вид комнаты (tl_room3d.h): ядро даёт многоугольники в порядке рисования — здесь их заливают. */
#include "app_render.h"

#include "app_draw.h"
#include "tl_room3d.h"

void app_render_room3d(cairo_t *cr, TlProject *project, TlSpace *space, TlCamera *camera, double width,
                       double height)
{
    cairo_pattern_t *sky = cairo_pattern_create_linear(0, 0, 0, height); /* фон: светлее к низу */
    cairo_pattern_add_color_stop_rgb(sky, 0, 0.88, 0.9, 0.93);
    cairo_pattern_add_color_stop_rgb(sky, 1, 0.97, 0.97, 0.98);
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
        app_set_fill_color(cr, app_color_hex(paint->fill));
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

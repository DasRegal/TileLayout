/*
 * Инструмент «Проём»: наведите на стену или перегородку — виден проём
 * заданной ширины и расстояния до концов стены; клик — поставить.
 */
#include "app_canvas_private.h"

#include "app_commands.h"
#include "app_hit.h"

static void preview_at(AppCanvas *canvas, TlVec2 world)
{
    TlBand band;
    double tolerance = app_view_pixels_to_mm(&canvas->view, APP_WALL_TOLERANCE_PX);
    canvas->has_opening_preview = tl_project_find_band(canvas->document->project, world, tolerance, &band) &&
                                  band.length >= 1;
    if (!canvas->has_opening_preview)
        return;
    double along;
    tl_band_local(&band, world, &along, NULL);
    canvas->opening_band = band;
    canvas->opening_along = app_snap_along(along, canvas->document->preferences.opening_width, band.length,
                                           canvas->document->preferences.snap_step);
}

static void motion(AppCanvas *canvas, const AppPointer *pointer)
{
    preview_at(canvas, pointer->world);
    app_canvas_redraw(canvas);
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    preview_at(canvas, pointer->world);
    if (!canvas->has_opening_preview) {
        app_document_status(canvas->document, "Наведите курсор на стену или перегородку");
        return;
    }
    app_command_add_opening(canvas->document, &canvas->opening_band, canvas->opening_along);
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    if (canvas->has_opening_preview)
        app_canvas_draw_opening_preview(canvas, cr, &canvas->opening_band, canvas->opening_along,
                                        canvas->document->preferences.opening_width, APP_GREEN, true);
}

const AppTool APP_OPENING_TOOL = {
    .cursor = "crosshair",
    .press = press,
    .motion = motion,
    .draw = draw,
};

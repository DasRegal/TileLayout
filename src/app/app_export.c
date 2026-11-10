#include "app_export.h"

#define FIT_MARGIN 70

bool app_export_png(TlProject *project, const char *path, int width, int height, double pixel_ratio,
                    const AppRenderOptions *options)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(width * pixel_ratio),
                                                          (int)(height * pixel_ratio));
    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, pixel_ratio, pixel_ratio);
    AppView view = {0.1, 60, 60, width, height};
    TlRect bounds;
    if (app_render_plan_bounds(project, &bounds))
        app_view_fit(&view, bounds, FIT_MARGIN);
    app_render_plan(cr, project, &view, options);
    cairo_destroy(cr);
    bool ok = cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surface);
    return ok;
}

#include "app_underlay.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <string.h>

#define MAX_IMAGE_SIDE 2400
#define JPEG_QUALITY "85"

bool app_underlay_load_image(TlUnderlay *underlay, const char *path, char **error)
{
    GError *failure = NULL;
    int width = 0, height = 0;
    if (!gdk_pixbuf_get_file_info(path, &width, &height)) {
        *error = g_strdup("это не картинка (нужен JPEG или PNG)");
        return false;
    }
    int side = MAX(width, height) > MAX_IMAGE_SIDE ? MAX_IMAGE_SIDE : MAX(width, height);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, side, side, TRUE, &failure);
    if (!pixbuf) {
        *error = g_strdup(failure->message);
        g_error_free(failure);
        return false;
    }
    GdkPixbuf *oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf); /* фото с телефона — как снято */
    g_object_unref(pixbuf);
    if (gdk_pixbuf_get_has_alpha(oriented)) { /* в JPEG — без прозрачности: на белом */
        GdkPixbuf *flat = gdk_pixbuf_composite_color_simple(oriented, gdk_pixbuf_get_width(oriented),
                                                            gdk_pixbuf_get_height(oriented), GDK_INTERP_NEAREST, 255,
                                                            8, 0xffffff, 0xffffff);
        g_object_unref(oriented);
        oriented = flat;
    }
    gchar *buffer = NULL;
    gsize size = 0;
    bool ok = gdk_pixbuf_save_to_buffer(oriented, &buffer, &size, "jpeg", &failure, "quality", JPEG_QUALITY, NULL);
    if (!ok) {
        *error = g_strdup(failure->message);
        g_error_free(failure);
        g_object_unref(oriented);
        return false;
    }
    gchar *base64 = g_base64_encode((const guchar *)buffer, size);
    gchar *url = g_strconcat("data:image/jpeg;base64,", base64, NULL);
    ok = tl_underlay_set_image(underlay, url, gdk_pixbuf_get_width(oriented), gdk_pixbuf_get_height(oriented));
    g_free(url);
    g_free(base64);
    g_free(buffer);
    g_object_unref(oriented);
    return ok;
}

bool app_underlay_load_dxf(TlUnderlay *underlay, const char *path, char **error)
{
    gchar *text = NULL;
    GError *failure = NULL;
    if (!g_file_get_contents(path, &text, NULL, &failure)) {
        *error = g_strdup(failure->message);
        g_error_free(failure);
        return false;
    }
    char reason[200];
    bool ok = tl_underlay_load_dxf(underlay, text, reason, sizeof reason);
    if (!ok)
        *error = g_strdup(reason);
    g_free(text);
    return ok;
}

/* Картинка подложки — один раз из data:-URL в поверхность cairo (пока та же строка). */
static cairo_surface_t *image_surface(const TlUnderlay *underlay)
{
    static const char *cached_url;
    static size_t cached_length;
    static cairo_surface_t *cached;
    size_t length = strlen(underlay->image);
    if (cached && cached_url == underlay->image && cached_length == length)
        return cached;
    if (cached)
        cairo_surface_destroy(cached);
    cached = NULL;
    cached_url = underlay->image;
    cached_length = length;
    const char *comma = strchr(underlay->image, ',');
    if (!comma)
        return NULL;
    gsize size = 0;
    guchar *bytes = g_base64_decode(comma + 1, &size);
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (gdk_pixbuf_loader_write(loader, bytes, size, NULL) && gdk_pixbuf_loader_close(loader, NULL)) {
        GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pixbuf)
            cached = gdk_cairo_surface_create_from_pixbuf(pixbuf, 1, NULL);
    } else {
        gdk_pixbuf_loader_close(loader, NULL);
    }
    g_object_unref(loader);
    g_free(bytes);
    return cached;
}

void app_render_underlay(cairo_t *cr, const AppView *view, const TlUnderlay *underlay)
{
    if (!underlay->visible || !tl_underlay_present(underlay))
        return;
    TlVec2 origin = app_view_to_screen(view, underlay->origin);
    double zoom = view->scale * underlay->scale;
    cairo_save(cr);
    if (underlay->image) {
        cairo_surface_t *surface = image_surface(underlay);
        if (surface) {
            cairo_translate(cr, origin.x, origin.y);
            cairo_scale(cr, zoom * underlay->image_width / cairo_image_surface_get_width(surface),
                        zoom * underlay->image_height / cairo_image_surface_get_height(surface));
            cairo_set_source_surface(cr, surface, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint_with_alpha(cr, underlay->opacity);
        }
    } else {
        cairo_set_source_rgba(cr, 0.55, 0.36, 0.12, underlay->opacity); /* коричневым — не спутать со стенами */
        cairo_set_line_width(cr, 1);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        for (size_t i = 0; i < underlay->lines.count; i++) {
            const TlPolygon *line = &underlay->lines.items[i];
            for (size_t k = 0; k < line->count; k++) {
                TlVec2 p = line->items[k];
                double x = origin.x + p.x * zoom, y = origin.y + p.y * zoom;
                if (k == 0)
                    cairo_move_to(cr, x, y);
                else
                    cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

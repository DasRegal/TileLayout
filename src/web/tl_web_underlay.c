/*
 * Подложка плана для веб-версии (tl_underlay.h): фото (data:-URL — его готовит JavaScript, уменьшив
 * картинку) или DXF-план, калибровка по двум точкам, положение и прозрачность.
 *
 * tl_web_underlay: {"present", "revision", "origin": [x, y], "scale", "opacity", "visible",
 *                   "image": data:-URL | null, "image_w", "image_h", "lines": [[x0, y0, x1, y1, …], …]}
 * В сцене (tl_web_scene) — то же без "image" и "lines": содержимое берут отсюда, когда сменилась
 * "revision".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "tl_web.h"
#include "tl_web_internal.h"

static int revision = 1;

void tl_web_underlay_touched(void) { revision++; }

TlJson *tl_web_underlay_json(bool content)
{
    const TlUnderlay *underlay = &tl_web_project()->underlay;
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "present", tl_json_bool(tl_underlay_present(underlay)));
    tl_json_object_set(json, "revision", tl_json_number(revision));
    TlJson *origin = tl_json_array();
    tl_json_array_push(origin, tl_web_number(underlay->origin.x));
    tl_json_array_push(origin, tl_web_number(underlay->origin.y));
    tl_json_object_set(json, "origin", origin);
    tl_json_object_set(json, "scale", tl_json_number(underlay->scale));
    tl_json_object_set(json, "opacity", tl_json_number(underlay->opacity));
    tl_json_object_set(json, "visible", tl_json_bool(underlay->visible));
    tl_json_object_set(json, "kind", tl_json_string(underlay->image ? "image" : underlay->lines.count ? "dxf" : "none"));
    if (!content)
        return json;
    tl_json_object_set(json, "image", underlay->image ? tl_json_string(underlay->image) : tl_json_null());
    tl_json_object_set(json, "image_w", tl_json_number(underlay->image_width));
    tl_json_object_set(json, "image_h", tl_json_number(underlay->image_height));
    TlJson *lines = tl_json_array();
    for (size_t i = 0; i < underlay->lines.count; i++)
        tl_json_array_push(lines, tl_web_polygon(&underlay->lines.items[i]));
    tl_json_object_set(json, "lines", lines);
    return json;
}

const char *tl_web_underlay(void)
{
    static char *text;
    return tl_web_keep(&text, tl_web_underlay_json(true));
}

/* Новое содержимое: отмена его не возвращает, но положение прежней подложки — в истории. */
static void replace_underlay(TlUnderlay *loaded, double now_seconds)
{
    TlUnderlay *underlay = &tl_web_project()->underlay;
    tl_web_checkpoint("", now_seconds);
    tl_underlay_clear(underlay);
    *underlay = *loaded;
    tl_web_underlay_touched();
}

bool tl_web_set_underlay_image(const char *data_url, int width, int height, double now_seconds)
{
    TlUnderlay loaded = tl_underlay_default();
    if (!tl_underlay_set_image(&loaded, data_url, width, height))
        return false;
    replace_underlay(&loaded, now_seconds);
    return true;
}

bool tl_web_load_underlay_dxf(const char *text, double now_seconds)
{
    TlUnderlay loaded = tl_underlay_default();
    if (!tl_underlay_load_dxf(&loaded, text, tl_web_error_buffer(), TL_WEB_ERROR_SIZE))
        return false;
    replace_underlay(&loaded, now_seconds);
    return true;
}

bool tl_web_set_underlay(const char *fields, double now_seconds)
{
    TlUnderlay *underlay = &tl_web_project()->underlay;
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    bool ok = parsed && tl_json_type(parsed) == TL_JSON_OBJECT && tl_underlay_present(underlay);
    if (ok) {
        TlUnderlay next = *underlay;
        const TlJson *value;
        if ((value = tl_json_get(parsed, "visible")))
            next.visible = tl_json_bool_or(value, next.visible);
        if ((value = tl_json_get(parsed, "opacity")))
            next.opacity = tl_json_number_or(value, next.opacity);
        if ((value = tl_json_get(parsed, "scale")))
            next.scale = tl_json_number_or(value, next.scale);
        if ((value = tl_json_get(parsed, "x")))
            next.origin.x = tl_json_number_or(value, next.origin.x);
        if ((value = tl_json_get(parsed, "y")))
            next.origin.y = tl_json_number_or(value, next.origin.y);
        if ((value = tl_json_get(parsed, "dx"))) /* сдвиг: точка подложки — в другую точку плана */
            next.origin.x += tl_json_number_or(value, 0);
        if ((value = tl_json_get(parsed, "dy")))
            next.origin.y += tl_json_number_or(value, 0);
        ok = next.opacity >= 0.05 && next.opacity <= 1 && next.scale > 1e-6 && next.scale < 1e6 &&
             isfinite(next.origin.x) && isfinite(next.origin.y);
        if (ok) {
            char merge_key[40] = "";
            if (tl_json_length(parsed) == 1)
                snprintf(merge_key, sizeof merge_key, "underlay_%s", tl_json_key_at(parsed, 0));
            tl_web_checkpoint(merge_key, now_seconds);
            *underlay = next;
        }
    }
    tl_json_free(parsed);
    return ok;
}

bool tl_web_calibrate_underlay(double ax, double ay, double bx, double by, double distance, double now_seconds)
{
    TlUnderlay *underlay = &tl_web_project()->underlay;
    TlUnderlay next = *underlay;
    if (!tl_underlay_present(underlay) || !tl_underlay_calibrate(&next, tl_vec2(ax, ay), tl_vec2(bx, by), distance))
        return false;
    tl_web_checkpoint("", now_seconds);
    *underlay = next;
    return true;
}

void tl_web_clear_underlay(double now_seconds)
{
    TlUnderlay *underlay = &tl_web_project()->underlay;
    if (!tl_underlay_present(underlay))
        return;
    tl_web_checkpoint("", now_seconds);
    tl_underlay_clear(underlay);
    tl_web_underlay_touched();
}

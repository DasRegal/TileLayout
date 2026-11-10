#include "app_labels.h"

#include <glib.h>
#include <math.h>

#define GAP_PX 1.0         /* зазор между подписями */
#define LEADER_FROM_PX 4.0 /* выноска — если подпись дальше от того, что подписывает */

typedef struct Segment {
    TlVec2 a, b;
} Segment;

struct AppLabels {
    TlRect bounds;
    GArray *boxes;    /* TlRect */
    GArray *segments; /* Segment */
};

AppLabels *app_labels_new(TlRect bounds)
{
    AppLabels *labels = g_new0(AppLabels, 1);
    labels->bounds = bounds;
    labels->boxes = g_array_new(FALSE, FALSE, sizeof(TlRect));
    labels->segments = g_array_new(FALSE, FALSE, sizeof(Segment));
    return labels;
}

void app_labels_free(AppLabels *labels)
{
    if (!labels)
        return;
    g_array_free(labels->boxes, TRUE);
    g_array_free(labels->segments, TRUE);
    g_free(labels);
}

void app_labels_avoid_segment(AppLabels *labels, TlVec2 a, TlVec2 b)
{
    Segment segment = {a, b};
    g_array_append_val(labels->segments, segment);
}

/* Отрезок задевает прямоугольник (отсечение Лианга — Барски). */
static bool segment_hits_rect(TlVec2 a, TlVec2 b, TlRect r)
{
    double t0 = 0, t1 = 1, dx = b.x - a.x, dy = b.y - a.y;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {a.x - r.left, r.right - a.x, a.y - r.top, r.bottom - a.y};
    for (int i = 0; i < 4; i++) {
        if (fabs(p[i]) < 1e-12) {
            if (q[i] < 0)
                return false;
            continue;
        }
        double t = q[i] / p[i];
        if (p[i] < 0)
            t0 = fmax(t0, t);
        else
            t1 = fmin(t1, t);
        if (t0 > t1)
            return false;
    }
    return true;
}

/* Габарит подписи с подложкой (как её рисует app_text_centered), повёрнутой на угол. */
static TlRect box_at(const char *text, const AppTextStyle *style, double angle_degrees, TlVec2 center)
{
    double width, height;
    app_text_size(text, style, &width, &height);
    width += 6;
    double a = angle_degrees * G_PI / 180.0;
    double half_x = (fabs(cos(a)) * width + fabs(sin(a)) * height) / 2;
    double half_y = (fabs(sin(a)) * width + fabs(cos(a)) * height) / 2;
    return (TlRect){center.x - half_x, center.y - half_y, center.x + half_x, center.y + half_y};
}

static double distance_to_box(TlVec2 p, TlRect box)
{
    double dx = fmax(fmax(box.left - p.x, 0), p.x - box.right);
    double dy = fmax(fmax(box.top - p.y, 0), p.y - box.bottom);
    return hypot(dx, dy);
}

static bool is_free(const AppLabels *labels, TlRect box, const Segment *leader)
{
    const TlRect *bounds = &labels->bounds;
    if (box.left < bounds->left || box.right > bounds->right || box.top < bounds->top || box.bottom > bounds->bottom)
        return false;
    TlRect padded = {box.left - GAP_PX, box.top - GAP_PX, box.right + GAP_PX, box.bottom + GAP_PX};
    for (guint i = 0; i < labels->boxes->len; i++) {
        TlRect other = g_array_index(labels->boxes, TlRect, i);
        if (tl_rect_intersects(padded, other) || (leader && segment_hits_rect(leader->a, leader->b, other)))
            return false;
    }
    for (guint i = 0; i < labels->segments->len; i++) {
        const Segment *segment = &g_array_index(labels->segments, Segment, i);
        if (segment_hits_rect(segment->a, segment->b, box))
            return false;
    }
    return true;
}

bool app_labels_place(AppLabels *labels, cairo_t *cr, const char *text, const AppTextStyle *style,
                      double angle_degrees, TlVec2 anchor, const TlVec2 *places, size_t place_count,
                      bool required)
{
    if (!place_count)
        return false;
    size_t chosen = place_count;
    for (size_t i = 0; i < place_count && chosen == place_count; i++) {
        Segment leader = {anchor, places[i]};
        TlRect box = box_at(text, style, angle_degrees, places[i]);
        if (is_free(labels, box, distance_to_box(anchor, box) > LEADER_FROM_PX ? &leader : NULL))
            chosen = i;
    }
    if (chosen == place_count) {
        if (!required)
            return false;
        chosen = 0;
    }
    TlVec2 center = places[chosen];
    TlRect box = box_at(text, style, angle_degrees, center);
    g_array_append_val(labels->boxes, box);
    if (distance_to_box(anchor, box) > LEADER_FROM_PX) {
        /* выноска до края подписи: дальше её закроет подложка */
        cairo_save(cr);
        cairo_move_to(cr, anchor.x, anchor.y);
        cairo_line_to(cr, center.x, center.y);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
        cairo_set_line_width(cr, 0.5);
        cairo_stroke(cr);
        cairo_restore(cr);
        app_labels_avoid_segment(labels, anchor, center); /* через выноску — ни одной подписи */
    }
    app_text_centered(cr, text, style, center, angle_degrees);
    return true;
}

bool app_labels_place_along(AppLabels *labels, cairo_t *cr, const char *text, const AppTextStyle *style,
                            TlVec2 a, TlVec2 b, TlVec2 normal, bool required)
{
    double angle = atan2(b.y - a.y, b.x - a.x) * 180.0 / G_PI;
    if (angle > 90 || angle <= -90)
        angle += 180;
    TlVec2 middle = tl_vec2_midpoint(a, b);
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(b, a));
    double text_width;
    app_text_size(text, style, &text_width, NULL);
    double beside = tl_distance(a, b) / 2 + text_width / 2 + 7; /* за концом отрезка */

    /* {отступ по normal, сдвиг вдоль отрезка} в порядке предпочтения */
    static const double PLACES[][2] = {
        {8, 0},  {16, 0},  {-8, 0}, {8, 1}, {8, -1}, {26, 0}, {-16, 0}, {-8, 1},
        {-8, -1}, {16, 1}, {16, -1}, {38, 0}, {-26, 0}, {50, 0},
    };
    enum { COUNT = sizeof PLACES / sizeof PLACES[0] };
    TlVec2 places[COUNT];
    for (size_t i = 0; i < COUNT; i++)
        places[i] = tl_vec2_add(middle, tl_vec2_add(tl_vec2_scale(normal, PLACES[i][0]),
                                                    tl_vec2_scale(direction, PLACES[i][1] * beside)));
    return app_labels_place(labels, cr, text, style, angle, middle, places, COUNT, required);
}

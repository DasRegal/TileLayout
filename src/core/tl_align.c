#include "tl_align.h"

#include <math.h>

bool tl_align_direction(TlVec2 anchor, TlVec2 point, const TlVec2 *directions, size_t direction_count,
                        double tolerance, TlVec2 *aligned, size_t *which)
{
    TlVec2 offset = tl_vec2_sub(point, anchor);
    if (tl_vec2_length(offset) <= tolerance)
        return false; /* у самой опорной точки направление не определено */
    double best_shift = tolerance;
    bool found = false;
    for (size_t i = 0; i < direction_count; i++) {
        TlVec2 direction = tl_vec2_normalized(directions[i]);
        if (tl_vec2_is_zero(direction))
            continue;
        TlVec2 projected = tl_vec2_add(anchor, tl_vec2_scale(direction, tl_vec2_dot(offset, direction)));
        double shift = tl_distance(point, projected);
        if (shift <= best_shift) {
            best_shift = shift;
            *aligned = projected;
            *which = i;
            found = true;
        }
    }
    return found;
}

TlTracking tl_align_tracking(TlVec2 point, const TlVec2 *references, size_t reference_count, double tolerance)
{
    TlTracking tracking = {.point = point};
    double best_x = tolerance, best_y = tolerance;
    for (size_t i = 0; i < reference_count; i++) {
        double dx = fabs(point.x - references[i].x), dy = fabs(point.y - references[i].y);
        if (dx <= best_x) {
            best_x = dx;
            tracking.point.x = references[i].x;
            tracking.x_reference = references[i];
            tracking.has_x = true;
        }
        if (dy <= best_y) {
            best_y = dy;
            tracking.point.y = references[i].y;
            tracking.y_reference = references[i];
            tracking.has_y = true;
        }
    }
    return tracking;
}

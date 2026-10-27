/*
 * Раскладка от линии: порога двери или оси (линейка по оси ниши, унитаза, окна).
 *
 * Линия идёт вдоль одной из осей раскладки, и смещение меняется только поперёк
 * неё: шов, край целой плитки или середина плитки ложится на линию. Отсчёт —
 * от первой плитки модуля рисунка: у прямой и шахматки так выравниваются все
 * ряды, у остальных рисунков — ряд этой плитки.
 */
#include "tl_layout.h"

#include <math.h>

#define ALONG_AXIS_SINE 0.06 /* ~3,5°: линия «вдоль оси», если отклонение меньше */

typedef enum Axis { AXIS_NONE, AXIS_X, AXIS_Y } Axis;

/* Ось раскладки, вдоль которой идёт линия (в осях раскладки). */
static Axis line_axis(TlVec2 from, TlVec2 to)
{
    TlVec2 d = tl_vec2_sub(to, from);
    double length = tl_vec2_length(d);
    if (length < 1)
        return AXIS_NONE;
    if (fabs(d.y) / length < ALONG_AXIS_SINE)
        return AXIS_X;
    if (fabs(d.x) / length < ALONG_AXIS_SINE)
        return AXIS_Y;
    return AXIS_NONE;
}

static bool first_tile(const TlTileSettings *settings, TlRect *rect)
{
    TlPatternUnit unit;
    if (!tl_pattern_unit(settings, &unit))
        return false;
    *rect = unit.tiles[0].rect;
    return true;
}

/* Смещение по одной оси: low/high — края первой плитки по этой оси, line — где линия,
   toward_high — целая плитка идёт от линии в сторону роста координаты. */
static double offset_across(double low, double high, double gap, double line, TlLineAlign mode, bool toward_high)
{
    switch (mode) {
    case TL_LINE_JOINT: return line + gap / 2 - low;
    case TL_LINE_TILE_CENTER: return line - (low + high) / 2;
    case TL_LINE_TILE_EDGE: return toward_high ? line - low : line - high;
    }
    return 0;
}

bool tl_layout_line_offsets(TlSpace *space, TlVec2 from, TlVec2 to, TlLineAlign mode, TlVec2 side,
                            double *offset_x, double *offset_y)
{
    const TlTileSettings *settings = tl_space_effective_tile(space);
    TlRect first;
    if (!first_tile(settings, &first))
        return false;
    TlAffine to_local = tl_layout_world_to_local(space);
    TlVec2 a = tl_affine_apply(&to_local, from), b = tl_affine_apply(&to_local, to);
    TlVec2 s = tl_affine_apply(&to_local, side);
    Axis axis = line_axis(a, b);
    double x = settings->offset_x, y = settings->offset_y;
    if (axis == AXIS_X) { /* линия горизонтальна — двигаем ряды по Y */
        double line = (a.y + b.y) / 2;
        y = offset_across(first.top, first.bottom, settings->joint_width, line, mode, s.y > line);
    } else if (axis == AXIS_Y) {
        double line = (a.x + b.x) / 2;
        x = offset_across(first.left, first.right, settings->joint_width, line, mode, s.x > line);
    } else {
        return false;
    }
    tl_layout_normalize_offsets(settings, &x, &y);
    *offset_x = x;
    *offset_y = y;
    return true;
}

/* Кусок плитки у косяка внутри проёма: от косяка at до ближайшего края плитки в сторону
   inward (+1 — к росту координаты). start — край первой плитки, pitch — шаг плиток. */
static double jamb_piece(double at, double start, double width, double pitch, int inward)
{
    double k = floor((at - start) / pitch);
    double tile_low = start + k * pitch, tile_high = tile_low + width;
    if (at >= tile_high) /* косяк в шве — у него целая плитка */
        return width;
    return inward > 0 ? tile_high - at : at - tile_low;
}

bool tl_layout_door_offsets(TlSpace *space, TlVec2 threshold_from, TlVec2 threshold_to, double *offset_x,
                            double *offset_y)
{
    const TlTileSettings *settings = tl_space_effective_tile(space);
    TlRect first;
    if (!first_tile(settings, &first))
        return false;
    /* В комнату — та сторона порога, где её пол. */
    TlVec2 middle = tl_vec2_midpoint(threshold_from, threshold_to);
    TlVec2 normal = tl_vec2_perpendicular(tl_vec2_normalized(tl_vec2_sub(threshold_to, threshold_from)));
    TlVec2 inside = tl_vec2_add(middle, tl_vec2_scale(normal, 20));
    if (!tl_space_contains(space, inside))
        inside = tl_vec2_sub(middle, tl_vec2_scale(normal, 20));
    double x, y;
    if (!tl_layout_line_offsets(space, threshold_from, threshold_to, TL_LINE_TILE_EDGE, inside, &x, &y))
        return false;

    /* Вдоль порога: середина проёма — на середине плитки или на шве, где куски у косяков шире. */
    TlAffine to_local = tl_layout_world_to_local(space);
    TlVec2 a = tl_affine_apply(&to_local, threshold_from), b = tl_affine_apply(&to_local, threshold_to);
    bool along_x = line_axis(a, b) == AXIS_X;
    double low = along_x ? fmin(a.x, b.x) : fmin(a.y, b.y), high = along_x ? fmax(a.x, b.x) : fmax(a.y, b.y);
    double first_low = along_x ? first.left : first.top, first_high = along_x ? first.right : first.bottom;
    double width = first_high - first_low, gap = settings->joint_width, pitch = width + gap;
    double center = (low + high) / 2;
    double best = 0, best_piece = -1;
    const TlLineAlign modes[] = {TL_LINE_TILE_CENTER, TL_LINE_JOINT};
    for (size_t m = 0; m < 2; m++) {
        double offset = offset_across(first_low, first_high, gap, center, modes[m], true);
        double start = offset + first_low;
        double piece = fmin(jamb_piece(low, start, width, pitch, +1), jamb_piece(high, start, width, pitch, -1));
        if (piece > best_piece + 0.5) {
            best_piece = piece;
            best = offset;
        }
    }
    if (along_x)
        x = best;
    else
        y = best;
    tl_layout_normalize_offsets(settings, &x, &y);
    *offset_x = x;
    *offset_y = y;
    return true;
}

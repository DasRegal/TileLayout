/*
 * Панно комнаты (tl_panno.h): где лежит, его плитки и что оно вырезает из плитки вокруг.
 *
 * Панно на стене — в координатах развёртки (tl_space_walls.c), на полу — в мире: прямоугольник
 * в осях раскладки комнаты вокруг центра комнаты со сдвигом at. Вокруг панно плитка отступает
 * на свой шов — шов между панно и плиткой.
 */
#include <math.h>

#include "tl_layout.h"

/* Оси раскладки комнаты (как у ковра): начало — угол габарита, поворот — плитки. */
static TlAffine floor_frame(TlSpace *space)
{
    TlRect bounds = tl_polygon_bounds(tl_space_frame(space));
    return tl_affine_rotation_translation(tl_vec2(bounds.left, bounds.top),
                                          tl_space_effective_tile(space)->rotation_degrees);
}

/* Прямоугольник панно (с полями margin) в осях: стены — развёртка; пола — оси раскладки комнаты. */
static bool panno_rect(TlSpace *space, const TlPanno *panno, double margin, TlRect *rect)
{
    double width = tl_panno_width(panno), height = tl_panno_height(panno);
    if (panno->wall == TL_PANNO_FLOOR) {
        TlAffine to_world = floor_frame(space);
        TlAffine to_local = tl_affine_inverted(&to_world);
        TlPolygon local = tl_affine_apply_polygon(&to_local, tl_space_room_outline(space));
        TlVec2 center = tl_vec2_add(tl_rect_center(tl_polygon_bounds(&local)), panno->at);
        tl_polygon_free(&local);
        *rect = (TlRect){center.x - width / 2, center.y - height / 2, center.x + width / 2, center.y + height / 2};
    } else {
        size_t wall = tl_space_wall_of_edge(space, panno->wall);
        if (wall == SIZE_MAX)
            return false;
        TlVec2 corner = tl_space_wall_to_development(space, wall, panno->at); /* левый нижний */
        *rect = (TlRect){corner.x, corner.y - height, corner.x + width, corner.y};
    }
    *rect = (TlRect){rect->left - margin, rect->top - margin, rect->right + margin, rect->bottom + margin};
    return true;
}

static TlPolygon rect_polygon(TlRect rect)
{
    const TlVec2 corners[] = {
        {rect.left, rect.top}, {rect.right, rect.top}, {rect.right, rect.bottom}, {rect.left, rect.bottom}};
    return tl_polygon_from_points(corners, 4);
}

TlPolygon tl_space_panno_outline(TlSpace *space, size_t index)
{
    TlRect rect;
    if (index >= space->pannos.count || !panno_rect(space, &space->pannos.items[index], 0, &rect))
        return (TlPolygon){0};
    TlPolygon outline = rect_polygon(rect);
    if (space->pannos.items[index].wall == TL_PANNO_FLOOR) {
        TlAffine to_world = floor_frame(space);
        TlPolygon world = tl_affine_apply_polygon(&to_world, &outline);
        tl_polygon_free(&outline);
        return world;
    }
    return outline;
}

TlPolygonList tl_space_panno_cutouts(TlSpace *space, size_t wall, double margin)
{
    TlPolygonList cutouts = {0};
    TlAffine to_world = floor_frame(space);
    for (size_t i = 0; i < space->pannos.count; i++) {
        const TlPanno *panno = &space->pannos.items[i];
        TlRect rect;
        if (panno->wall != wall || !panno_rect(space, panno, margin, &rect))
            continue;
        TlPolygon polygon = rect_polygon(rect);
        if (wall == TL_PANNO_FLOOR) {
            TlPolygon world = tl_affine_apply_polygon(&to_world, &polygon);
            tl_polygon_free(&polygon);
            polygon = world;
        }
        tl_polygon_list_push(&cutouts, polygon);
    }
    return cutouts;
}

TlTileSettings tl_space_panno_tile(TlSpace *space, size_t index)
{
    const TlPanno *panno = &space->pannos.items[index];
    const TlTileSettings *around =
        panno->wall == TL_PANNO_FLOOR ? tl_space_effective_tile(space) : tl_space_wall_tile(space, TL_WALL_MAIN);
    return tl_panno_tile(panno, around);
}

TlLayout *tl_space_panno_layout(TlSpace *space, size_t index)
{
    TlRect rect;
    if (index >= space->pannos.count || !panno_rect(space, &space->pannos.items[index], 0, &rect))
        return NULL;
    const TlPanno *panno = &space->pannos.items[index];
    TlTileSettings tile = tl_space_panno_tile(space, index);
    TlPolygon outline = rect_polygon(rect);
    const double insets[4] = {0, 0, 0, 0};
    TlSurface surface = {.outline = &outline, .insets = insets, .origin = {rect.left, rect.top}, .angle = 0, .tile = &tile};
    TlAffine to_world = floor_frame(space);
    TlPolygon world = {0};
    if (panno->wall == TL_PANNO_FLOOR) { /* в мир; оси — как у раскладки комнаты */
        world = tl_affine_apply_polygon(&to_world, &outline);
        surface.outline = &world;
        surface.origin = tl_affine_apply(&to_world, tl_vec2(rect.left, rect.top));
        surface.angle = tl_space_effective_tile(space)->rotation_degrees;
    }
    TlLayoutOptions options = {.use_offset = true};
    TlLayout *layout = tl_layout_compute_surface(&surface, &options);
    tl_polygon_free(&outline);
    tl_polygon_free(&world);
    return layout;
}

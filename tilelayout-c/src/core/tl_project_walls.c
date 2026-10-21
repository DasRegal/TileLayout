/*
 * Смежные стены соседних домов: наружные плоскости совпадают, нормали
 * встречные. Сдвиг или утолщение такой стены двигает и соседнюю.
 */
#include "tl_project.h"

#include <math.h>

#define LINK_TOLERANCE 2.0

static double projection_on(TlVec2 point, TlVec2 origin, TlVec2 direction)
{
    return tl_vec2_dot(tl_vec2_sub(point, origin), direction);
}

static bool walls_touch(TlOutline *outline, size_t wall, const TlPolygon *outer, TlOutline *other,
                        size_t other_wall, const TlPolygon *other_outer)
{
    TlVec2 direction, normal, other_normal;
    tl_outline_wall_frame(outline, wall, &direction, &normal);
    tl_outline_wall_frame(other, other_wall, NULL, &other_normal);
    if (tl_vec2_dot(normal, other_normal) > -0.999)
        return false;
    TlVec2 outer_point = outer->items[wall];
    TlVec2 other_outer_point = other_outer->items[other_wall];
    if (fabs(tl_vec2_dot(tl_vec2_sub(other_outer_point, outer_point), normal)) > LINK_TOLERANCE)
        return false;
    TlVec2 origin = tl_outline_wall_start(outline, wall);
    double a = projection_on(tl_outline_wall_start(other, other_wall), origin, direction);
    double b = projection_on(tl_outline_wall_end(other, other_wall), origin, direction);
    double low = fmin(a, b), high = fmax(a, b);
    double overlap = fmin(high, tl_outline_inner_length(outline, wall)) - fmax(low, 0.0);
    return overlap > LINK_TOLERANCE;
}

TlWallRefList tl_project_linked_walls(TlProject *project, TlOutline *outline, size_t wall)
{
    TlWallRefList links = {0};
    TlPolygon outer = tl_outline_outer(outline);
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *other = project->outlines.items[o];
        if (other == outline)
            continue;
        TlPolygon other_outer = tl_outline_outer(other);
        for (size_t j = 0; j < tl_outline_wall_count(other); j++)
            if (walls_touch(outline, wall, &outer, other, j, &other_outer)) {
                TlWallRef link = {other, j};
                tl_wall_ref_list_push(&links, link);
            }
        tl_polygon_free(&other_outer);
    }
    tl_polygon_free(&outer);
    return links;
}

void tl_project_shift_wall(TlProject *project, TlOutline *outline, size_t wall, TlVec2 offset,
                           bool linked)
{
    TlWallRefList links = linked ? tl_project_linked_walls(project, outline, wall) : (TlWallRefList){0};
    tl_outline_shift_wall(outline, wall, offset);
    for (size_t i = 0; i < links.count; i++)
        tl_outline_shift_wall(links.items[i].outline, links.items[i].wall, offset);
    tl_wall_ref_list_free(&links);
}

void tl_project_set_thickness(TlProject *project, TlOutline *outline, size_t wall, double value,
                              bool linked)
{
    TlWallRefList links = linked ? tl_project_linked_walls(project, outline, wall) : (TlWallRefList){0};
    TlVec2 normal;
    tl_outline_wall_frame(outline, wall, NULL, &normal);
    double delta = value - outline->thickness.items[wall];
    outline->thickness.items[wall] = value;
    for (size_t i = 0; i < links.count; i++)
        tl_outline_shift_wall(links.items[i].outline, links.items[i].wall, tl_vec2_scale(normal, delta));
    tl_wall_ref_list_free(&links);
}

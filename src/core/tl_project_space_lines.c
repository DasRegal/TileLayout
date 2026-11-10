/* Линии комнаты, от которых можно раскладывать плитку: двери и линейки. */
#include "tl_project.h"

#define ON_OUTLINE_MM 1.0
#define SIDE_PROBE_MM 2.0 /* за край пола — проверить, с какой стороны комната */

/* Край пола комнаты в проёме: линия стыка, отодвинутая в комнату на половину шва
   под дверью (плитка комнаты кончается там, а не на самом стыке). */
static bool door_edge(TlProject *project, const TlSpace *space, const TlOpening *opening, TlVec2 *from, TlVec2 *to)
{
    TlVec2 a, b;
    if (!tl_project_opening_threshold(project, opening, &a, &b) || tl_distance(a, b) < 1)
        return false;
    TlVec2 normal = tl_vec2_perpendicular(tl_vec2_normalized(tl_vec2_sub(b, a)));
    double half_gap = opening->threshold_gap / 2;
    TlVec2 middle = tl_vec2_midpoint(a, b);
    for (int side = -1; side <= 1; side += 2) {
        TlVec2 shift = tl_vec2_scale(normal, side * half_gap);
        TlVec2 probe = tl_vec2_add(middle, tl_vec2_scale(normal, side * (half_gap + SIDE_PROBE_MM)));
        TlVec2 edge_middle = tl_vec2_add(middle, shift);
        if (tl_space_contains(space, probe) && tl_polygon_on_boundary(&space->outline, edge_middle, ON_OUTLINE_MM)) {
            *from = tl_vec2_add(a, shift);
            *to = tl_vec2_add(b, shift);
            return true;
        }
    }
    return false;
}

TlOpeningList tl_project_space_doors(TlProject *project, const TlSpace *space)
{
    TlOpeningList doors = {0};
    for (size_t i = 0; i < project->openings.count; i++) {
        TlVec2 from, to;
        if (door_edge(project, space, project->openings.items[i], &from, &to))
            tl_opening_list_push(&doors, project->openings.items[i]);
    }
    return doors;
}

bool tl_project_space_door_edge(TlProject *project, const TlSpace *space, const TlOpening *opening, TlVec2 *from,
                                TlVec2 *to)
{
    return door_edge(project, space, opening, from, to);
}

TlRulerList tl_project_space_rulers(TlProject *project, const TlSpace *space)
{
    TlRulerList rulers = {0};
    for (size_t i = 0; i < project->rulers.count; i++) {
        TlRuler *ruler = project->rulers.items[i];
        if (tl_space_contains(space, tl_vec2_midpoint(ruler->start, ruler->end)))
            tl_ruler_list_push(&rulers, ruler);
    }
    return rulers;
}

/* Проёмы: в теле какой стены стоят и какой прямоугольник вырезают. */
#include "tl_project.h"

#include <math.h>

#define HOST_TOLERANCE 1.0
#define CUT_MARGIN 1.0

bool tl_project_opening_host(TlProject *project, const TlOpening *opening, TlBand *band)
{
    return tl_project_find_band(project, opening->center, HOST_TOLERANCE, band);
}

void tl_project_opening_extent(TlProject *project, const TlBand *band, double along, double width,
                               double *along_from, double *along_to, double *across_from,
                               double *across_to)
{
    double far = band->far;
    if (band->host.kind == TL_HOST_WALL) {
        /* проём проходит и через примыкающую стену соседнего дома */
        TlWallRefList links = tl_project_linked_walls(project, band->host.outline, band->host.wall);
        double neighbour = 0.0;
        for (size_t i = 0; i < links.count; i++)
            neighbour = fmax(neighbour, links.items[i].outline->thickness.items[links.items[i].wall]);
        tl_wall_ref_list_free(&links);
        far += neighbour;
    }
    *along_from = along - width / 2;
    *along_to = along + width / 2;
    *across_from = band->near - CUT_MARGIN;
    *across_to = far + CUT_MARGIN;
}

bool tl_project_opening_cut(TlProject *project, const TlOpening *opening, TlPolygon *cut)
{
    TlBand band;
    if (!tl_project_opening_host(project, opening, &band))
        return false;
    double along, from, to, across_from, across_to;
    tl_band_local(&band, opening->center, &along, NULL);
    tl_project_opening_extent(project, &band, along, opening->width, &from, &to, &across_from, &across_to);
    *cut = tl_band_rect(&band, from, to, across_from, across_to);
    return true;
}

void tl_project_remove_orphan_openings(TlProject *project)
{
    for (size_t i = project->openings.count; i-- > 0;) {
        TlBand band;
        if (!tl_project_opening_host(project, project->openings.items[i], &band))
            tl_project_delete_opening(project, project->openings.items[i]);
    }
}

/*
 * Области плоскости, ограниченные контурами многоугольников.
 *
 * Все стороны многоугольников разбиваются в точках пересечения и образуют
 * планарный граф; его грани — элементарные области. Для каждой грани
 * известно, внутри каких многоугольников она лежит, и предикат решает,
 * оставить ли её. Так строятся и пол комнат (контур дома минус перегородки),
 * и площадь перекрытия двух многоугольников.
 *
 * Соседние оставленные грани не сливаются: для разности и пересечения это
 * не нужно — у таких граней не бывает общей стороны.
 */
#ifndef TL_REGIONS_H
#define TL_REGIONS_H

#include "tl_geometry.h"

typedef struct TlRegion {
    TlPolygon outline;    /* внешний контур */
    TlPolygonList holes;  /* дыры (отдельно стоящие препятствия) */
} TlRegion;

TL_ARRAY_DEFINE(TlRegionList, TlRegion, tl_region_list)

/* inside[i] — лежит ли грань внутри polygons[i]. */
typedef bool (*TlRegionPredicate)(const bool *inside, size_t polygon_count, void *context);

TlRegionList tl_regions_build(const TlPolygon *polygons, size_t polygon_count,
                              TlRegionPredicate keep, void *context);

/* Части base, не покрытые ни одним из cutters. */
TlRegionList tl_regions_difference(const TlPolygon *base, const TlPolygon *cutters,
                                   size_t cutter_count);

double tl_polygons_overlap_area(const TlPolygon *a, const TlPolygon *b);

double tl_region_area(const TlRegion *region);
bool tl_region_contains(const TlRegion *region, TlVec2 point);
void tl_region_free(TlRegion *region);
void tl_region_list_free_all(TlRegionList *regions);

#endif

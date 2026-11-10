/*
 * Области плоскости, ограниченные контурами многоугольников.
 *
 * Все стороны многоугольников разбиваются в точках пересечения и образуют
 * планарный граф; его грани — элементарные области. Для каждой грани
 * известно, внутри каких многоугольников она лежит, и предикат решает,
 * оставить ли её. Так строятся и пол комнат (контур дома минус перегородки),
 * и площадь перекрытия двух многоугольников.
 *
 * В tl_regions_build соседние оставленные грани не сливаются: для разности
 * и пересечения это не нужно — у таких граней не бывает общей стороны.
 * Где нужно, — tl_regions_build_merged.
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

/* То же, но соседние оставленные грани сливаются в одну область — общие
 * стороны пропадают. Так комната и прилегающая к ней часть дверного проёма
 * становятся одной комнатой. */
TlRegionList tl_regions_build_merged(const TlPolygon *polygons, size_t polygon_count,
                                     TlRegionPredicate keep, void *context);

/* Части base, не покрытые ни одним из cutters. */
TlRegionList tl_regions_difference(const TlPolygon *base, const TlPolygon *cutters,
                                   size_t cutter_count);

double tl_polygons_overlap_area(const TlPolygon *a, const TlPolygon *b);

/* Область как многоугольники без дыр (вырезы раскладки дыр не знают): у области с дырами — куски
   между вертикалями через середины дыр; дыры в них — вырезы по краю. */
TlPolygonList tl_region_pieces(const TlRegion *region);

double tl_region_area(const TlRegion *region);
bool tl_region_contains(const TlRegion *region, TlVec2 point);
void tl_region_free(TlRegion *region);
void tl_region_list_free_all(TlRegionList *regions);

#endif

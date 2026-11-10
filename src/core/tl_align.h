/*
 * Автовыравнивание точек при рисовании и правке:
 *   по направлению — отрезок от опорной точки, почти параллельный одному из
 *   направлений (горизонталь, вертикаль, прежняя ось), выпрямляется по нему;
 *   по опорным точкам — точка встаёт на одну вертикаль или горизонталь с
 *   углом другой стены (вспомогательные линии «слежения»).
 * Допуск — расстояние в мм, на которое точка может сдвинуться.
 */
#ifndef TL_ALIGN_H
#define TL_ALIGN_H

#include "tl_geometry.h"

/* true — выровнено: aligned — точка на прямой anchor + t·directions[which]. */
bool tl_align_direction(TlVec2 anchor, TlVec2 point, const TlVec2 *directions, size_t direction_count,
                        double tolerance, TlVec2 *aligned, size_t *which);

typedef struct TlTracking {
    TlVec2 point;            /* выровненная точка */
    bool has_x, has_y;       /* X взят у x_reference, Y — у y_reference */
    TlVec2 x_reference, y_reference;
} TlTracking;

/* Ближайшие по X и по Y опорные точки не дальше tolerance. */
TlTracking tl_align_tracking(TlVec2 point, const TlVec2 *references, size_t reference_count, double tolerance);

#endif

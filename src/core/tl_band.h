/*
 * Тело стены в локальных координатах — общий взгляд на наружную стену
 * дома и на перегородку. along — вдоль стены от точки origin, across —
 * поперёк (по нормали); стена занимает across от near до far.
 */
#ifndef TL_BAND_H
#define TL_BAND_H

#include "tl_outline.h"
#include "tl_partition.h"

typedef enum TlHostKind { TL_HOST_NONE, TL_HOST_WALL, TL_HOST_PARTITION } TlHostKind;

/* Кому принадлежит тело стены: стене контура или перегородке. */
typedef struct TlHost {
    TlHostKind kind;
    TlOutline *outline;
    size_t wall;
    TlPartition *partition;
} TlHost;

typedef struct TlBand {
    TlHost host;
    TlVec2 origin, direction, normal;
    double length;
    double near, far;
} TlBand;

TlBand tl_band_of_wall(TlOutline *outline, size_t wall);
TlBand tl_band_of_partition(TlPartition *partition);

void tl_band_local(const TlBand *band, TlVec2 point, double *along, double *across);
TlVec2 tl_band_point(const TlBand *band, double along, double across);
TlPolygon tl_band_rect(const TlBand *band, double along_from, double along_to, double across_from,
                       double across_to);
double tl_band_thickness(const TlBand *band);
double tl_band_middle(const TlBand *band);
bool tl_host_equal(TlHost a, TlHost b);

#endif

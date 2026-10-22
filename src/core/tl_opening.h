/* Проём: центр лежит в теле стены или перегородки, ширина — вдоль неё. */
#ifndef TL_OPENING_H
#define TL_OPENING_H

#include "tl_geometry.h"

typedef struct TlOpening {
    TlVec2 center;
    double width;
    bool locked;
} TlOpening;

TlOpening *tl_opening_new(TlVec2 center, double width);
TlOpening *tl_opening_clone(const TlOpening *opening);
void tl_opening_free(TlOpening *opening);

#endif

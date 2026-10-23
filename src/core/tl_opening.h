/*
 * Проём: центр лежит в теле стены или перегородки, ширина — вдоль неё.
 * В проёме перегородки — пол; его делит между комнатами стык, параллельный
 * полотну двери: threshold — доля толщины от правого бока перегородки (0) к
 * левому (1).
 */
#ifndef TL_OPENING_H
#define TL_OPENING_H

#include "tl_geometry.h"

typedef struct TlOpening {
    TlVec2 center;
    double width;
    double threshold;
    bool locked;
} TlOpening;

#define TL_OPENING_DEFAULT_THRESHOLD 0.5 /* стык посередине толщины */

TlOpening *tl_opening_new(TlVec2 center, double width);
TlOpening *tl_opening_clone(const TlOpening *opening);
void tl_opening_free(TlOpening *opening);

#endif

/*
 * Концы линеек и деформационных швов, примагниченные к плану, держатся за то,
 * к чему примагничены: к углу или грани стены дома (внутренней, наружной,
 * оси), к углу или грани тела перегородки, к её оси, к краю проёма. Когда
 * стену, перегородку или проём двигают, конец едет с ними — длина линейки
 * остаётся настоящей.
 *
 * Конец запоминается как «контур предмета, ребро, доля вдоль ребра» до правки
 * и ставится на то же место контура после неё. Конец, не лежащий ни на чём, не
 * трогается; если у контура стало другое число углов — тоже.
 */
#ifndef TL_MEASURE_ANCHOR_H
#define TL_MEASURE_ANCHOR_H

#include "tl_project.h"

typedef struct TlMeasureAnchors TlMeasureAnchors;

TlMeasureAnchors *tl_measure_anchors_remember(TlProject *project);
void tl_measure_anchors_apply(TlProject *project, const TlMeasureAnchors *anchors);
void tl_measure_anchors_free(TlMeasureAnchors *anchors);

#endif

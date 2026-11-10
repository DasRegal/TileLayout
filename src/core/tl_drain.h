/*
 * Трап в полу душевой или мокрой зоны:
 *   точечный — решётка size × size в center (повёрнута на angle); пол сходится к нему «конвертом» —
 *     четырьмя (по числу углов части пола) треугольниками: плитку режут по линиям от трапа к углам;
 *   линейный — лоток length × size у стены (вдоль неё, angle — как у предмета), пол — одним уклоном
 *     к лотку, без разрезов.
 * Под решёткой и лотком плитки нет. Уклон slope (%) — для перепада: насколько дальний угол выше трапа.
 */
#ifndef TL_DRAIN_H
#define TL_DRAIN_H

#include "tl_geometry.h"

typedef enum TlDrainKind { TL_DRAIN_POINT, TL_DRAIN_LINEAR, TL_DRAIN_KIND_COUNT } TlDrainKind;

typedef struct TlDrain {
    TlDrainKind kind;
    TlVec2 center;
    double size;      /* решётка или ширина лотка, мм */
    double length;    /* длина лотка, мм (у точечного не нужна) */
    double angle;     /* поворот, ° */
    double slope;     /* уклон пола к трапу, % */
    bool envelope;    /* точечный: резать плитку конвертом */
} TlDrain;

TlDrain tl_drain_default(TlDrainKind kind, TlVec2 center);
const char *tl_drain_kind_name(TlDrainKind kind); /* «Трап», «Линейный трап» */
const char *tl_drain_kind_key(TlDrainKind kind);  /* "point", "linear" */
TlDrainKind tl_drain_kind_from_key(const char *key);
/* Решётка или лоток на плане (без плитки). */
TlPolygon tl_drain_outline(const TlDrain *drain);
/* Линейный — к ближайшей стороне walls не дальше reach: вдоль неё, вплотную. */
bool tl_drain_snap_to_walls(TlDrain *drain, const TlPolygon *walls, double reach);
/* Перепад, мм: уклон до дальней точки контура part (части пола с трапом). */
double tl_drain_drop(const TlDrain *drain, const TlPolygon *part);

#endif

/*
 * Деформационный (компенсационный, температурно-усадочный) шов — разрыв в
 * покрытии пола по линии от start до end шириной gap. Плитка в полосу шва
 * не заходит: шов режет её, как перегородка.
 *
 * Линия раздела (kind TL_LINE_DIVIDER) — то же самое со швом обычной ширины:
 * граница участков пола с разным покрытием (душевая из мозаики, пол без
 * плитки под кухней). Любые линии делят пол комнаты на участки (tl_space.h);
 * концы линий, лежащие на другой линии, держатся за неё при перетаскивании.
 */
#ifndef TL_EXPANSION_JOINT_H
#define TL_EXPANSION_JOINT_H

#include "tl_geometry.h"

#define TL_EXPANSION_JOINT_DEFAULT_GAP 10.0
#define TL_DIVIDER_DEFAULT_GAP 2.0

typedef enum TlLineKind { TL_LINE_EXPANSION_JOINT, TL_LINE_DIVIDER } TlLineKind;

typedef enum TlExpansionJointEnd { TL_EXPANSION_JOINT_START = 1, TL_EXPANSION_JOINT_END = 2 } TlExpansionJointEnd;

typedef struct TlExpansionJoint {
    TlVec2 start, end;
    double gap; /* мм — ширина щели */
    TlLineKind kind;
} TlExpansionJoint;

TlExpansionJoint *tl_expansion_joint_new(TlVec2 start, TlVec2 end, double gap);
void tl_expansion_joint_free(TlExpansionJoint *joint);

double tl_expansion_joint_length(const TlExpansionJoint *joint);
void tl_expansion_joint_translate(TlExpansionJoint *joint, TlVec2 offset);
TlVec2 *tl_expansion_joint_end_point(TlExpansionJoint *joint, TlExpansionJointEnd which);

/* Полоса, свободная от плитки: зазор поровну по обе стороны от линии и с
   запасом за концами — чтобы шов, доведённый до стены, наверняка её достал. */
TlPolygon tl_expansion_joint_strip(const TlExpansionJoint *joint);

#endif

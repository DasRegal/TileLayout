/*
 * Примагничивание точек измерения (концов линеек) к плану: к углам и
 * серединам граней стен, перегородок и проёмов, к самим граням, к оси стены
 * (середине толщины), к середине швов плитки и к их пересечениям.
 *
 * Точка (угол, середина, пересечение швов) сильнее линии (грань, ось, шов),
 * даже если линия ближе к курсору; точки — в порядке перечисления, линии
 * между собой — по расстоянию.
 */
#ifndef TL_MEASURE_SNAP_H
#define TL_MEASURE_SNAP_H

#include "tl_project.h"

typedef enum TlSnapKind {
    TL_SNAP_NONE,
    TL_SNAP_CORNER,         /* угол стены, перегородки или проёма */
    TL_SNAP_WALL_MIDDLE,    /* середина грани */
    TL_SNAP_JOINT_CROSSING, /* пересечение швов плитки */
    TL_SNAP_WALL_EDGE,      /* ближайшая точка грани */
    TL_SNAP_WALL_AXIS,      /* ближайшая точка оси стены или перегородки */
    TL_SNAP_TILE_JOINT      /* ближайшая точка середины шва */
} TlSnapKind;

typedef struct TlSnapPoint {
    TlVec2 point;
    TlSnapKind kind;
    TlVec2 direction;       /* у линий — направление линии, у точек — (0, 0) */
} TlSnapPoint;

bool tl_snap_kind_is_line(TlSnapKind kind);

/* К чему притягивать: линейке — ко всему, шву — только к поверхностям стен. */
typedef enum TlSnapTargets {
    TL_SNAP_TO_WALL_SURFACES = 1 << 0, /* углы, середины и края граней стен, перегородок, проёмов */
    TL_SNAP_TO_WALL_AXES = 1 << 1,     /* оси стен и перегородок — середина толщины */
    TL_SNAP_TO_TILE_JOINTS = 1 << 2,   /* швы плитки и их пересечения */
    TL_SNAP_TO_ALL = TL_SNAP_TO_WALL_SURFACES | TL_SNAP_TO_WALL_AXES | TL_SNAP_TO_TILE_JOINTS
} TlSnapTargets;

/* Ближайшая подходящая точка не дальше radius (мм); false — притягивать некуда. */
bool tl_measure_snap(TlProject *project, TlVec2 probe, double radius, TlSnapTargets targets,
                     TlSnapPoint *result);

struct TlGuides;

/* Конец линейки или шва: притягивание (magnet) и выравнивание по
   горизонтали/вертикали от другого конца anchor (align; если конец притянут
   к линии — на пересечении). snap — к чему притянулось (TL_SNAP_NONE — ни к
   чему), guides — линия выравнивания (может быть NULL). */
TlVec2 tl_measure_point(TlProject *project, TlVec2 world, const TlVec2 *anchor, double radius,
                        TlSnapTargets targets, bool magnet, bool align, TlSnapPoint *snap,
                        struct TlGuides *guides);

#endif

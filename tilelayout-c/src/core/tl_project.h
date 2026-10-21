/*
 * Проект — всё, что есть на плане: дома (контуры наружных стен),
 * перегородки, проёмы и комнаты. Комнаты не рисуются, а вычисляются:
 * пол каждого дома минус тела перегородок; каждая замкнутая область —
 * комната. Настройки комнаты переходят к новой области с наибольшим
 * перекрытием (tl_project_update_spaces).
 *
 * Правка геометрии оформляется как транзакция: tl_project_begin_edit()
 * запоминает, какие концы перегородок лежат на каких гранях и где стоят
 * проёмы; после изменения tl_project_end_edit() возвращает их на свои
 * стены и пересчитывает комнаты.
 */
#ifndef TL_PROJECT_H
#define TL_PROJECT_H

#include "tl_band.h"
#include "tl_opening.h"
#include "tl_outline.h"
#include "tl_partition.h"
#include "tl_space.h"

#define TL_MIN_SPACE_AREA 20000.0 /* мм²: меньшие области (щели) — не комнаты */

TL_ARRAY_DEFINE(TlOutlineList, TlOutline *, tl_outline_list)
TL_ARRAY_DEFINE(TlPartitionList, TlPartition *, tl_partition_list)
TL_ARRAY_DEFINE(TlOpeningList, TlOpening *, tl_opening_list)

typedef struct TlProject {
    TlOutlineList outlines;
    TlPartitionList partitions;
    TlOpeningList openings;
    TlSpaceList spaces;
    int next_space_id;
} TlProject;

typedef struct TlWallRef {
    TlOutline *outline;
    size_t wall;
} TlWallRef;

TL_ARRAY_DEFINE(TlWallRefList, TlWallRef, tl_wall_ref_list)
TL_ARRAY_DEFINE(TlBandList, TlBand, tl_band_list)

/* Узел: совпадающие концы двух перегородок. */
typedef struct TlJoint {
    TlPartition *first;
    TlPartitionEnd first_end;
    TlPartition *second;
    TlPartitionEnd second_end;
} TlJoint;

TL_ARRAY_DEFINE(TlJointList, TlJoint, tl_joint_list)

/* Грань, к которой примыкают перегородки: внутренняя сторона стены дома
 * (side = 0) или бок перегородки (side = +1 / -1). */
typedef struct TlFace {
    TlHost host;
    int side;
    TlVec2 start, end;
} TlFace;

TL_ARRAY_DEFINE(TlFaceList, TlFace, tl_face_list)

typedef struct TlEditToken TlEditToken;

/* Как завершать правку. */
typedef struct TlEditOptions {
    TlPartition *master;          /* перегородку двигает пользователь: узлы идут за ней */
    TlPartition *frozen;          /* концы этой перегородки не возвращать на грани */
    TlVec2 translate;             /* сдвиг перегородок внутри перемещаемого дома */
    bool has_translate;
    bool skip_space_update;
} TlEditOptions;

/* ---- создание ------------------------------------------------------------ */

TlProject *tl_project_new(void);
void tl_project_free(TlProject *project);
void tl_project_clear(TlProject *project);
TlProject *tl_project_demo(void);
int tl_project_take_space_id(TlProject *project);

void tl_project_add_outline(TlProject *project, TlOutline *outline);
void tl_project_add_partition(TlProject *project, TlPartition *partition);
void tl_project_add_opening(TlProject *project, TlOpening *opening);
void tl_project_delete_partition(TlProject *project, TlPartition *partition);
void tl_project_delete_opening(TlProject *project, TlOpening *opening);
/* Удаляет дом вместе с перегородками внутри и осиротевшими проёмами. */
void tl_project_delete_outline(TlProject *project, TlOutline *outline);

/* ---- комнаты ------------------------------------------------------------- */

void tl_project_update_spaces(TlProject *project);
TlSpace *tl_project_space_by_id(TlProject *project, int id);
TlSpace *tl_project_space_at(TlProject *project, TlVec2 point);

/* ---- тела стен ----------------------------------------------------------- */

TlBandList tl_project_bands(TlProject *project);
bool tl_project_band_of(TlProject *project, TlHost host, TlBand *band);
/* Стена, в теле которой лежит точка (самая толстая из подходящих). */
bool tl_project_find_band(TlProject *project, TlVec2 point, double tolerance, TlBand *band);

/* ---- соседние дома ------------------------------------------------------- */

TlWallRefList tl_project_linked_walls(TlProject *project, TlOutline *outline, size_t wall);
void tl_project_shift_wall(TlProject *project, TlOutline *outline, size_t wall, TlVec2 offset,
                           bool linked);
void tl_project_set_thickness(TlProject *project, TlOutline *outline, size_t wall, double value,
                              bool linked);

/* ---- перегородки: узлы и тела -------------------------------------------- */

TlJointList tl_project_joints(TlProject *project);
/* Заполнение угла «на ус» между двумя перегородками узла (false — не нужно). */
bool tl_project_joint_cap(const TlJoint *joint, TlPolygon *cap);
/* Тела всех перегородок и заполнения углов. */
TlPolygonList tl_project_partition_bodies(TlProject *project, double extension);

/* ---- грани ---------------------------------------------------------------- */

TlFaceList tl_project_faces(TlProject *project);
/* Текущее положение грани: точка и направление. */
bool tl_project_face_line(TlProject *project, TlHost host, int side, TlVec2 *point,
                          TlVec2 *direction);

/* ---- правка с привязками ------------------------------------------------- */

TlEditToken *tl_project_begin_edit(TlProject *project, TlOutline *moving_outline);
/* Применяет привязки к текущей геометрии; token остаётся (перетаскивание
 * мышью применяет одну правку на каждом движении). options может быть NULL. */
void tl_project_apply_edit(TlProject *project, TlEditToken *token, const TlEditOptions *options);
void tl_edit_token_free(TlEditToken *token);
/* apply + free. */
void tl_project_end_edit(TlProject *project, TlEditToken *token, const TlEditOptions *options);

/* ---- проёмы -------------------------------------------------------------- */

bool tl_project_opening_host(TlProject *project, const TlOpening *opening, TlBand *band);
/* Прямоугольник проёма: вдоль [from, to], поперёк — на всю толщину стены,
 * включая примыкающую стену соседнего дома. */
void tl_project_opening_extent(TlProject *project, const TlBand *band, double along, double width,
                               double *along_from, double *along_to, double *across_from,
                               double *across_to);
bool tl_project_opening_cut(TlProject *project, const TlOpening *opening, TlPolygon *cut);
void tl_project_remove_orphan_openings(TlProject *project);

#endif

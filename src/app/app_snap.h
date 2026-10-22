/*
 * Привязки курсора: к углам домов (внутренним и наружным) и концам
 * перегородок, к граням стен, к сетке. Автовыравнивание (можно отключить):
 * по X/Y с опорными точками — углами домов, концами перегородок — и
 * «мягкая» ортогональность; при выравнивании видны вспомогательные линии.
 * Shift отключает привязки.
 */
#ifndef APP_SNAP_H
#define APP_SNAP_H

#include "app_view.h"
#include "tl_project.h"

#define APP_SNAP_TOLERANCE_PX 10.0
#define APP_MAX_GUIDES 3

typedef struct AppSnapContext {
    TlProject *project;
    const AppView *view;
    double grid_step;               /* 0 — без сетки */
    const TlOutline *skip_outline;  /* угол skip_vertex этого дома не учитывать */
    int skip_vertex;
    const TlPartition *skip_partition;
    bool align;                     /* автовыравнивание включено */
    const TlVec2 *extra_refs;       /* опорные точки инструмента (точки контура) */
    size_t extra_ref_count;
} AppSnapContext;

/* Вспомогательная линия выравнивания: от опорной точки по вертикали или
   горизонтали до выровненной точки. */
typedef struct AppGuide {
    TlVec2 from;
    bool vertical;
} AppGuide;

typedef struct AppGuides {
    AppGuide items[APP_MAX_GUIDES];
    int count;
} AppGuides;

void app_guides_add(AppGuides *guides, TlVec2 from, bool vertical);
/* Оставляет только линии, на которых точка всё ещё лежит. */
void app_guides_keep_valid(AppGuides *guides, TlVec2 point);

typedef struct AppSnapResult {
    TlVec2 point;
    bool on_vertex;
    bool on_face;
    TlVec2 face_start, face_end;    /* грань, к которой прилипла точка */
    AppGuides guides;
} AppSnapResult;

TlVec2 app_snap_grid(double step, TlVec2 point);
/* Почти горизонтальный или вертикальный отрезок от start выпрямляется. */
TlVec2 app_snap_ortho(TlVec2 point, TlVec2 start);
/* Углы, затем сетка и выравнивание по опорным точкам refs (если context->align). */
AppSnapResult app_snap_point(const AppSnapContext *context, TlVec2 point, const TlVec2 *refs,
                             size_t ref_count, bool free);
/* Для перегородок и контуров: углы, выравнивание по start, углам домов и
   концам перегородок, ортогональность к start, грани стен. */
AppSnapResult app_snap_wall_point(const AppSnapContext *context, TlVec2 point, const TlVec2 *start,
                                  bool free);

/* Положение проёма вдоль стены: край проёма — по шагу привязки, проём
 * не выходит за стену. */
double app_snap_along(double along, double width, double band_length, double grid_step);

/* Конец перегородки при растягивании: по её оси от fixed_end, с прилипанием
 * к граням стен и шагом сетки по длине. free — свободно (можно повернуть). */
TlVec2 app_snap_stretch(const AppSnapContext *context, TlVec2 fixed_end, TlVec2 moving_end,
                        TlVec2 target, bool free);

#endif

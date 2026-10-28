/*
 * Отрисовка плана — одна для экрана, предпросмотра, PDF и печати.
 * Слои: пол и плитка, деформационные швы, трубы, стены, перегородки, проёмы, номера плиток, размеры,
 * подписи комнат, замки, линейки, масштабная линейка.
 */
#ifndef APP_RENDER_H
#define APP_RENDER_H

#include <cairo.h>

#include "app_draw.h"
#include "app_view.h"
#include "tl_layout.h"
#include "tl_project.h"

typedef struct AppRenderOptions {
    bool show_tiles;
    bool show_clips;
    bool show_dims;
    bool show_grid;
    bool show_numbers;
    bool show_room_labels;
    bool show_locks;
    bool show_rulers;
    bool show_expansion_joints;
    bool printing;           /* без фона, сетки и экранных украшений */
    bool mono;               /* только чёрный и белый */
    const TlOutline *highlighted_outline;
} AppRenderOptions;

AppRenderOptions app_render_screen_options(void);

void app_render_plan(cairo_t *cr, TlProject *project, const AppView *view,
                     const AppRenderOptions *options);

/* Линейка: тонкая линия с засечками на концах и длиной посередине. */
void app_render_ruler(cairo_t *cr, const AppView *view, const TlRuler *ruler, AppColor color, double width_px);

/* Косяки проёма в перегородке: сечения у краёв проёма, от правого бока к левому. */
bool app_render_doorway_jambs(TlProject *project, const TlOpening *opening, TlVec2 from_jamb[2], TlVec2 to_jamb[2]);
/* Стык комнат в проёме перегородки: заметный пунктир (виден и на краю двери). */
void app_render_threshold(cairo_t *cr, const AppView *view, TlVec2 from, TlVec2 to, AppColor color, double width_px);

/* Деформационный шов: полоса шириной в зазор (не уже нескольких пикселей) с
   пунктиром посередине; color — контур и пунктир. */
void app_render_expansion_joint(cairo_t *cr, const AppView *view, const TlExpansionJoint *joint, AppColor color);

/* Отверстие под трубу: круг диаметром коронки (не меньше нескольких пикселей) с
   крестом в центре; label — подпись «Ø50». */
void app_render_pipe(cairo_t *cr, const AppView *view, const TlPipe *pipe, AppColor color, bool label);

/* Плитка раскладки (обрезана по её полу); cairo — в базовом преобразовании, состояние — на вызывающем. */
void app_render_tiles(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                      const TlTileSettings *tile, TlLayout *layout);
/* Панно index комнаты: картинка на всё панно, по плиткам; to_view — из мира (пол) или развёртки (стена)
   в оси вида, NULL — как есть. */
void app_render_panno(cairo_t *cr, const AppView *view, const AppRenderOptions *options, TlSpace *space,
                      size_t index, const TlAffine *to_view);
/* Номера плиток раскладки (tl_report_assign_numbers — заранее). */
void app_render_layout_numbers(cairo_t *cr, const AppView *view, const AppTextStyle *style, const TlLayout *layout);

/* Выделенное на развёртке: номера стены, отверстия, линейки; −1 — нет. */
typedef struct AppWallsMarks {
    long wall, pipe, ruler, panno;
} AppWallsMarks;

/* Развёртка стен комнаты (tl_space_walls.c): стены в ряд, плитка, проёмы, отверстия, линейки,
   под каждой стеной — пол у неё; размеры. marks — выделенное (NULL — ничего). */
void app_render_walls(cairo_t *cr, TlProject *project, TlSpace *space, const AppView *view,
                      const AppRenderOptions *options, const AppWallsMarks *marks);
/* Габарит развёртки с подписями. */
TlRect app_render_walls_bounds(TlSpace *space);

/* Объёмный вид комнаты под камерой (tl_room3d.h; камера с distance = 0 — вся комната в кадре). */
struct TlCamera;
void app_render_room3d(cairo_t *cr, TlProject *project, TlSpace *space, struct TlCamera *camera, double width,
                       double height);

/* Трап на плане: решётка или лоток. */
void app_render_drain(cairo_t *cr, const AppView *view, const TlDrain *drain, AppColor color);
/* Предмет (ванна, поддон, тумба…) на плане: контур, значок вида, название и размер. */
void app_render_fixture(cairo_t *cr, const AppView *view, const TlFixture *fixture, AppColor color, bool label);

/* Габарит всего плана в мировых координатах (для «Вписать»). */
bool app_render_plan_bounds(TlProject *project, TlRect *bounds);

/* Подписи размеров (общие с канвой). */
void app_render_dimension_text(cairo_t *cr, TlVec2 a, TlVec2 b, double offset_px, const char *text,
                               const char *color_hex, bool bold);

#endif

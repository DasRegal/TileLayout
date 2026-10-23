/*
 * Отрисовка плана — одна для экрана, предпросмотра, PDF и печати.
 * Слои: пол и плитка, деформационные швы, стены, перегородки, проёмы, номера плиток, размеры,
 * подписи комнат, замки, линейки, масштабная линейка.
 */
#ifndef APP_RENDER_H
#define APP_RENDER_H

#include <cairo.h>

#include "app_draw.h"
#include "app_view.h"
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

/* Габарит всего плана в мировых координатах (для «Вписать»). */
bool app_render_plan_bounds(TlProject *project, TlRect *bounds);

/* Подписи размеров (общие с канвой). */
void app_render_dimension_text(cairo_t *cr, TlVec2 a, TlVec2 b, double offset_px, const char *text,
                               const char *color_hex, bool bold);

#endif

/*
 * Отрисовка плана — одна для экрана, предпросмотра, PDF и печати.
 * Слои: пол и плитка, стены, перегородки, проёмы, номера плиток, размеры,
 * подписи комнат, замки, масштабная линейка.
 */
#ifndef APP_RENDER_H
#define APP_RENDER_H

#include <cairo.h>

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
    bool printing;           /* без фона, сетки и экранных украшений */
    bool mono;               /* только чёрный и белый */
    const TlOutline *highlighted_outline;
} AppRenderOptions;

AppRenderOptions app_render_screen_options(void);

void app_render_plan(cairo_t *cr, TlProject *project, const AppView *view,
                     const AppRenderOptions *options);

/* Габарит всего плана в мировых координатах (для «Вписать»). */
bool app_render_plan_bounds(TlProject *project, TlRect *bounds);

/* Подписи размеров (общие с канвой). */
void app_render_dimension_text(cairo_t *cr, TlVec2 a, TlVec2 b, double offset_px, const char *text,
                               const char *color_hex, bool bold);

#endif

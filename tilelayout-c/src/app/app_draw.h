/*
 * Примитивы рисования для плана, панелей печати и иконок: цвета (с
 * чёрно-белым режимом), контуры в мировых координатах, подписи Pango.
 *
 * Контур строится в мировых координатах, а обводится в логических пикселях:
 * cairo хранит путь в координатах устройства, поэтому после возврата к
 * базовому преобразованию толщина линии задаётся в пикселях — одинаково и на
 * экране, и в PDF.
 */
#ifndef APP_DRAW_H
#define APP_DRAW_H

#include <cairo.h>
#include <stdbool.h>

#include "app_view.h"

typedef struct AppColor {
    double red, green, blue, alpha;
} AppColor;

#define APP_TRANSPARENT ((AppColor){0, 0, 0, 0})

/* "#rrggbb" → цвет (непрозрачный); неверная строка — серый. */
AppColor app_color_hex(const char *hex);
AppColor app_color_rgba(int red, int green, int blue, double alpha);
AppColor app_color_darker(AppColor color, double factor);

/* Чёрно-белый режим печати: светлые заливки — белые, остальное — чёрное. */
void app_draw_set_mono(bool mono);
bool app_draw_is_mono(void);
void app_set_color(cairo_t *cr, AppColor color);
void app_set_fill_color(cairo_t *cr, AppColor color);  /* «светлая» заливка */

/* ---- контуры в мировых координатах ------------------------------------- */

void app_path_polygon(cairo_t *cr, const AppView *view, const TlPolygon *polygon);
void app_path_polygon_with_holes(cairo_t *cr, const AppView *view, const TlPolygon *outline,
                                 const TlPolygonList *holes);
void app_path_line(cairo_t *cr, const AppView *view, TlVec2 from, TlVec2 to);

void app_stroke(cairo_t *cr, AppColor color, double width_px);
void app_stroke_dashed(cairo_t *cr, AppColor color, double width_px, double dash_px);

/* ---- текст -------------------------------------------------------------- */

typedef struct AppTextStyle {
    double size_px;
    bool bold;
    AppColor color;
    AppColor background;     /* alpha = 0 — без подложки */
} AppTextStyle;

void app_text_size(const char *text, const AppTextStyle *style, double *width, double *height);
/* Текст по центру точки, повёрнутый на angle_degrees. */
void app_text_centered(cairo_t *cr, const char *text, const AppTextStyle *style, TlVec2 center,
                       double angle_degrees);
/* Текст вдоль отрезка a-b (экранные точки), сдвинутый по нормали на offset_px;
 * поворачивается так, чтобы не быть вверх ногами. */
void app_text_along(cairo_t *cr, const char *text, const AppTextStyle *style, TlVec2 a, TlVec2 b,
                    TlVec2 normal, double offset_px);
/* Многострочный текст (через \n) в рамке по центру точки. */
void app_text_box(cairo_t *cr, const char *const *lines, size_t line_count, double size_px,
                  TlVec2 center);
/* Текст в прямоугольнике: align -1 — влево, 0 — по центру, 1 — вправо. */
void app_text_in_rect(cairo_t *cr, const char *text, const AppTextStyle *style, double x, double y,
                      double width, double height, int align);

void app_rounded_rect(cairo_t *cr, double x, double y, double width, double height, double radius);

#endif

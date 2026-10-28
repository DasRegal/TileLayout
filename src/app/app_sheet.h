/*
 * Лист расчёта для печати: заголовки, абзацы и таблицы (разметка Pango),
 * разбитые на страницы. Строки таблиц не разрываются, заголовок таблицы
 * повторяется на каждой странице.
 */
#ifndef APP_SHEET_H
#define APP_SHEET_H

#include <cairo.h>
#include <stdbool.h>

#include "tl_project.h"

typedef struct AppSheet AppSheet;

/* Лист расчёта проекта; width — ширина области печати в логических пикселях. */
AppSheet *app_sheet_new_calculation(TlProject *project, const char *title, bool mono, double width);
/* Смета для заказчика: материалы и работы с ценами, итог, места для подписей. */
AppSheet *app_sheet_new_estimate(TlProject *project, const char *title, bool mono, double width);
/* Карта раскроя: одинаковые подрезки — стопкой, пары — из одной плитки. */
AppSheet *app_sheet_new_cut_map(TlProject *project, const char *title, bool mono, double width);
void app_sheet_free(AppSheet *sheet);

int app_sheet_page_count(AppSheet *sheet, double page_height);
void app_sheet_render_page(AppSheet *sheet, cairo_t *cr, int page, double page_height);

#endif

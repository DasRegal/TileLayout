/*
 * Задание на печать: листы A4 с планом (при необходимости — на нескольких
 * листах), план с номерами плиток и чертежи подрезок, лист расчёта.
 * Одна отрисовка страницы для предпросмотра, PDF и принтера.
 */
#ifndef APP_PRINT_H
#define APP_PRINT_H

#include <cairo.h>

#include "app_config.h"
#include "tl_project.h"

#define APP_PX_PER_MM (96.0 / 25.4)
#define APP_PAPER_MARGIN_MM 10.0

typedef struct AppPrintJob AppPrintJob;

AppPrintJob *app_print_job_new(TlProject *project, const char *title, const AppPrintPreferences *options,
                               double offset_x_mm, double offset_y_mm);
void app_print_job_free(AppPrintJob *job);

int app_print_job_page_count(AppPrintJob *job);
void app_print_job_paper_mm(const AppPrintJob *job, double *width, double *height);
/* Масштаб плана 1:N и число листов с планом (по горизонтали и вертикали). */
void app_print_job_plan_info(const AppPrintJob *job, int *scale, int *columns, int *rows);

/* Рисует страницу; cr — в логических пикселях (96 dpi), начало — угол бумаги. */
void app_print_job_render(AppPrintJob *job, cairo_t *cr, int page);
bool app_print_job_save_pdf(AppPrintJob *job, const char *path);

/* Стандартные масштабы для выбора в окне печати. */
extern const int APP_PRINT_SCALES[];
extern const int APP_PRINT_SCALE_COUNT;

#endif

/* Текст расчёта для панели свойств (разметка Pango). */
#ifndef APP_CALC_TEXT_H
#define APP_CALC_TEXT_H

#include "tl_report.h"

/* Выделенная комната (может быть NULL) и итог по проекту. g_free(). */
char *app_calc_markup(TlProject *project, TlSpace *selected);

#endif

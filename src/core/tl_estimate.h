/*
 * Смета в рублях — из сводного расчёта (tl_report.h) и цен в настройках плитки
 * комнат. Строка сметы — позиция с ценой: вид плитки (за м², штуку или упаковку),
 * упаковки клея, СВП или крестиков, затирки, грунта, гидроизоляции, лента (за
 * метр), планки порогов и уголков, укладка (за м²) и подрезка (за погонный метр).
 * Позиции без цены в смету не попадают. Строки одной позиции с разной ценой — отдельно.
 */
#ifndef TL_ESTIMATE_H
#define TL_ESTIMATE_H

#include "tl_report.h"

typedef enum TlEstimateGroup { TL_ESTIMATE_MATERIALS, TL_ESTIMATE_WORK } TlEstimateGroup;

typedef struct TlEstimateLine {
    TlEstimateGroup group;
    char name[160];              /* «Плитка 600×300 мм», «Клей, мешок 25 кг» */
    char color[TL_COLOR_LENGTH]; /* у плитки — её цвет, иначе пусто */
    double quantity;
    const char *unit;            /* «м²», «шт.» */
    double price;                /* ₽ за единицу */
    double cost;                 /* ₽ */
} TlEstimateLine;

TL_ARRAY_DEFINE(TlEstimateLineList, TlEstimateLine, tl_estimate_line_list)

typedef struct TlEstimate {
    TlEstimateLineList lines;
    double materials, work, total; /* ₽ */
} TlEstimate;

TlEstimate tl_estimate_from_summary(const TlSummary *summary);
void tl_estimate_free(TlEstimate *estimate);

/* «12 345 ₽» — с пробелами между разрядами, копейки — только если есть. */
void tl_format_rubles(double amount, char *text, size_t size);

#endif

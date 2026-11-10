/*
 * Подписи без наложений: каждая следующая подпись ставится в первое из
 * предложенных мест, где не задевает уже поставленные подписи и линии
 * чертежа и не выходит за рамку. Подпись в стороне от того, что она
 * подписывает, получает тонкую выноску.
 */
#ifndef APP_LABELS_H
#define APP_LABELS_H

#include <stdbool.h>
#include <stddef.h>

#include "app_draw.h"
#include "tl_geometry.h"

typedef struct AppLabels AppLabels;

AppLabels *app_labels_new(TlRect bounds);
void app_labels_free(AppLabels *labels);

/* Линия чертежа, которую подписи не должны закрывать. */
void app_labels_avoid_segment(AppLabels *labels, TlVec2 a, TlVec2 b);

/* Ставит и рисует подпись. anchor — что подписывается (начало выноски).
 * Нет свободного места: required — на первое место, иначе подпись не ставится.
 * Возвращает, поставлена ли подпись. */
bool app_labels_place(AppLabels *labels, cairo_t *cr, const char *text, const AppTextStyle *style,
                      double angle_degrees, TlVec2 anchor, const TlVec2 *places, size_t place_count,
                      bool required);

/* Подпись к отрезку: у отрезка снаружи (по normal), дальше, с другой стороны, сбоку. */
bool app_labels_place_along(AppLabels *labels, cairo_t *cr, const char *text, const AppTextStyle *style,
                            TlVec2 a, TlVec2 b, TlVec2 normal, bool required);

#endif

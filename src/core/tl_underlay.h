/*
 * Подложка плана: фото или скан плана (картинка) либо чертёж DXF (линии) — под планом, чтобы
 * обвести стены. Масштаб — калибровкой: две точки на подложке и настоящее расстояние между ними.
 *
 * Оси подложки: у картинки — пиксели от левого верхнего угла, у DXF — мм от левого верхнего угла
 * чертежа (ось Y вниз, как на плане). На плане точка подложки p — в origin + p · scale.
 *
 * Содержимое (картинка, линии) тяжёлое и в историю правок не входит (tl_project_snapshot):
 * отмена меняет только положение, масштаб и прозрачность.
 */
#ifndef TL_UNDERLAY_H
#define TL_UNDERLAY_H

#include <stddef.h>

#include "tl_geometry.h"

typedef struct TlUnderlay {
    /* содержимое */
    char *image;                    /* data:-URL картинки (JPEG/PNG в base64) или NULL */
    int image_width, image_height;  /* пикселей */
    TlPolygonList lines;            /* линии DXF: открытые ломаные, мм */
    /* положение */
    TlVec2 origin;                  /* где на плане угол подложки, мм */
    double scale;                   /* мм плана на единицу подложки */
    double opacity;                 /* 0…1 */
    bool visible;
    bool placed;                    /* положение задано (подложка загружена или прочитана из файла) */
} TlUnderlay;

#define TL_UNDERLAY_MAX_POINTS 400000

/* Пустая подложка с положением по умолчанию. */
TlUnderlay tl_underlay_default(void);
bool tl_underlay_present(const TlUnderlay *underlay);
/* Убрать содержимое; положение — по умолчанию. */
void tl_underlay_clear(TlUnderlay *underlay);

TlVec2 tl_underlay_to_plan(const TlUnderlay *underlay, TlVec2 point);
TlVec2 tl_underlay_from_plan(const TlUnderlay *underlay, TlVec2 point);
/* Габарит подложки на плане (пустой — нулевой прямоугольник). */
TlRect tl_underlay_bounds(const TlUnderlay *underlay);

/* Картинка: data:-URL и её размер в пикселях; масштаб — 10 мм на пиксель до калибровки. */
bool tl_underlay_set_image(TlUnderlay *underlay, const char *data_url, int width, int height);
/* Чертёж DXF (текстовый): отрезки, ломаные, дуги, окружности, вставки блоков. Единицы — по
   $INSUNITS (без них: маленький чертёж — в метрах, иначе в мм). false — нет линий (причина — в error). */
bool tl_underlay_load_dxf(TlUnderlay *underlay, const char *text, char *error, size_t error_size);

/* Калибровка: точки a и b на плане на самом деле в distance мм друг от друга — масштаб меняется
   вокруг a (a остаётся на месте). */
bool tl_underlay_calibrate(TlUnderlay *underlay, TlVec2 a, TlVec2 b, double distance);

/* Для истории правок: забрать подложку целиком (в underlay остаётся пустая) и вернуть содержимое
   после восстановления снимка; положение — из снимка, если там была подложка (underlay->placed),
   иначе прежнее. */
TlUnderlay tl_underlay_take(TlUnderlay *underlay);
void tl_underlay_restore(TlUnderlay *underlay, TlUnderlay *taken);

#endif

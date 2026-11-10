/*
 * Вид на план: масштаб и сдвиг. Мировые координаты — миллиметры,
 * экранные — «логические пиксели» (на экране — пиксели виджета,
 * при печати — 1/96 дюйма).
 */
#ifndef APP_VIEW_H
#define APP_VIEW_H

#include "tl_geometry.h"

#define APP_VIEW_MIN_SCALE 0.005
#define APP_VIEW_MAX_SCALE 5.0

typedef struct AppView {
    double scale;            /* логических пикселей на миллиметр */
    double pan_x, pan_y;     /* где на экране мировой ноль */
    double width, height;    /* размер области отрисовки */
} AppView;

TlVec2 app_view_to_screen(const AppView *view, TlVec2 world);
TlVec2 app_view_to_world(const AppView *view, TlVec2 screen);
/* n пикселей экрана — в миллиметры. */
double app_view_pixels_to_mm(const AppView *view, double pixels);
/* Середина видимой части отрезка a–b (точки экрана) не ближе margin пикселей к краям; отрезок
   целиком за краем — ближайшая к его середине точка. Чтобы размер, который правят, был на экране. */
TlVec2 app_view_visible_midpoint(const AppView *view, TlVec2 a, TlVec2 b, double margin);

/* Вписать прямоугольник мира в область с полями margin пикселей. */
void app_view_fit(AppView *view, TlRect world, double margin);
/* Масштаб относительно точки экрана (она остаётся на месте). */
void app_view_zoom_at(AppView *view, double factor, TlVec2 screen_anchor);

#endif

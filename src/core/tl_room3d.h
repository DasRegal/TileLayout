/*
 * Объёмный вид комнаты — «кукольный домик»: пол, стены с проёмами, плитка пола, стен и ступеней,
 * панно, предметы и лестницы; ближние к зрителю стены не рисуются, чтобы комнату было видно изнутри.
 *
 * Ядро строит грани в объёме и само проецирует их под камеру: на выходе — многоугольники в пикселях
 * в порядке рисования (дальние раньше), с цветом заливки (с учётом света) и обводки. Программа и сайт
 * только заливают их и вращают камеру мышью — картинка у них одна и та же.
 *
 * Оси объёма: X — вправо по плану, Y — вверх по плану (на плане программы ось y — вниз), Z — вверх
 * от пола, мм.
 */
#ifndef TL_ROOM3D_H
#define TL_ROOM3D_H

#include "tl_project.h"

typedef struct TlVec3 {
    double x, y, z;
} TlVec3;

typedef struct TlCamera {
    double yaw;       /* °: откуда смотрим, вокруг вертикали (0 — с востока, 90 — с севера плана) */
    double pitch;     /* °: на сколько сверху (0 — сбоку, 90 — сверху) */
    double distance;  /* мм от точки, на которую смотрим; 0 — подобрать: смотреть в середину комнаты, вся она в кадре */
    TlVec3 target;
    double fov;       /* ° по вертикали */
} TlCamera;

#define TL_CAMERA_MIN_PITCH 5.0
#define TL_CAMERA_MAX_PITCH 89.0

typedef struct TlPaint {
    TlPolygonList contours;   /* пиксели; заливка «чёт-нечет» (дыры — проёмы) */
    char fill[8];             /* "#rrggbb" */
    char stroke[8];           /* "" — без обводки */
    double stroke_width;      /* пикселей */
} TlPaint;

TL_ARRAY_DEFINE(TlPaintList, TlPaint, tl_paint_list)
void tl_paint_list_free_all(TlPaintList *list);

/* Камера по умолчанию для комнаты: сверху-сбоку, комната целиком в кадре width × height. */
TlCamera tl_room3d_default_camera(TlProject *project, TlSpace *space, double width, double height);
/* Камера с distance = 0 получает расстояние «вся комната в кадре». */
TlPaintList tl_room3d_paint(TlProject *project, TlSpace *space, TlCamera *camera, double width, double height);

#endif

/*
 * Отверстие в покрытии — под трубу, слив, трап: круг на плане. Плитку, которую
 * оно задевает, сверлят коронкой; на чертеже подрезки — отверстие с диаметром
 * и расстояниями от краёв плитки. Площадь плитки отверстие не меняет: такая
 * плитка остаётся целой (или подрезкой, если была ею).
 */
#ifndef TL_PIPE_H
#define TL_PIPE_H

#include "tl_geometry.h"

#define TL_PIPE_DEFAULT_DIAMETER 50.0 /* мм — канализация 50 */
#define TL_PIPE_MIN_DIAMETER 5.0
#define TL_PIPE_MAX_DIAMETER 500.0

typedef struct TlCircle {
    TlVec2 center;
    double diameter;
} TlCircle;

TL_ARRAY_DEFINE(TlCircleList, TlCircle, tl_circle_list)

typedef struct TlPipe {
    TlVec2 center;
    double diameter; /* мм — диаметр коронки */
} TlPipe;

TlPipe *tl_pipe_new(TlVec2 center, double diameter);
void tl_pipe_free(TlPipe *pipe);
TlCircle tl_pipe_circle(const TlPipe *pipe);

/* Круг задевает прямоугольник (не только касается). */
bool tl_circle_touches_rect(TlCircle circle, TlRect rect);

#endif

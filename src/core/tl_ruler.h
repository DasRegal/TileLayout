/* Линейка: измерительный отрезок на плане. На стены и комнаты не влияет. У линейки
   короткое имя («Л1») — по нему её выбирают для раскладки по оси. */
#ifndef TL_RULER_H
#define TL_RULER_H

#include "tl_geometry.h"

typedef enum TlRulerEnd { TL_RULER_START = 1, TL_RULER_END = 2 } TlRulerEnd;

#define TL_NAME_LENGTH 24 /* короткое имя линейки или проёма и завершающий ноль */

typedef struct TlRuler {
    TlVec2 start, end;
    char name[TL_NAME_LENGTH];
} TlRuler;

TlRuler *tl_ruler_new(TlVec2 start, TlVec2 end);
TlRuler *tl_ruler_clone(const TlRuler *ruler);
void tl_ruler_free(TlRuler *ruler);

double tl_ruler_length(const TlRuler *ruler);
void tl_ruler_translate(TlRuler *ruler, TlVec2 offset);
TlVec2 *tl_ruler_end_point(TlRuler *ruler, TlRulerEnd which);

#endif

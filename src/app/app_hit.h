/* Что под курсором: линейка, шов, угол, конец или тело перегородки, проём, стена, комната. */
#ifndef APP_HIT_H
#define APP_HIT_H

#include "app_document.h"
#include "app_view.h"

#define APP_VERTEX_TOLERANCE_PX 9.0
#define APP_WALL_TOLERANCE_PX 6.0

/* current — выделенное сейчас: его дом проверяется первым. */
AppSelection app_hit_test(TlProject *project, const AppView *view, TlVec2 screen,
                          const AppSelection *current);

#endif

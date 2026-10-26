/* Общее для модулей веб-ядра (не для JavaScript). */
#ifndef TL_WEB_INTERNAL_H
#define TL_WEB_INTERNAL_H

#include "tl_json.h"
#include "tl_project.h"

/* Открытый проект (создаётся примером при первом обращении). */
TlProject *tl_web_project(void);
/* JSON в текст, который живёт в *slot до следующего вызова с тем же slot. */
const char *tl_web_keep(char **slot, TlJson *json);

/* Координата для JavaScript: до сотых миллиметра (короче текст сцены). */
TlJson *tl_web_number(double value);
/* Многоугольник — плоским массивом [x0, y0, x1, y1, …]. */
TlJson *tl_web_polygon(const TlPolygon *polygon);
/* Настройки плитки объектом {ключ: значение} — ключи как в файле проекта. */
TlJson *tl_web_tile_json(const TlTileSettings *tile);

#endif

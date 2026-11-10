/* Общее для модулей веб-ядра (не для JavaScript). */
#ifndef TL_WEB_INTERNAL_H
#define TL_WEB_INTERNAL_H

#include "tl_json.h"
#include "tl_project.h"

/* Открытый проект (создаётся примером при первом обращении). */
TlProject *tl_web_project(void);
/* Запомнить проект перед правкой (шаг отмены; merge_key — см. tl_history.h). */
void tl_web_checkpoint(const char *merge_key, double now_seconds);
/* Буфер текста ошибки (tl_web_error), TL_WEB_ERROR_SIZE байт. */
#define TL_WEB_ERROR_SIZE 256
char *tl_web_error_buffer(void);
/* Подложка (tl_web_underlay.c): содержимое сменилось — новая "revision"; JSON — с содержимым или без. */
void tl_web_underlay_touched(void);
TlJson *tl_web_underlay_json(bool content);
/* JSON в текст, который живёт в *slot до следующего вызова с тем же slot. */
const char *tl_web_keep(char **slot, TlJson *json);

/* Координата для JavaScript: до сотых миллиметра (короче текст сцены). */
TlJson *tl_web_number(double value);
/* Многоугольник — плоским массивом [x0, y0, x1, y1, …]. */
TlJson *tl_web_polygon(const TlPolygon *polygon);
/* Настройки плитки объектом {ключ: значение} — ключи как в файле проекта. */
TlJson *tl_web_tile_json(const TlTileSettings *tile);
/* Раскладка для отрисовки: matrix (оси → мир), floor, holes, tiles (по 6 чисел), specs, shapes, clips… */
struct TlLayout;
TlJson *tl_web_layout_json(const struct TlLayout *layout);
/* Панно комнаты для отрисовки (tl_web_walls.c); to_view — из мира или развёртки в оси вида, NULL — как есть. */
struct TlSpace;
TlJson *tl_web_panno_json(struct TlSpace *space, size_t index, const TlAffine *to_view);

#endif

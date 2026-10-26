/*
 * Ядро для веб-версии: один открытый проект, история правок и обмен с
 * JavaScript строками JSON. Функции — простые (числа и строки), чтобы их было
 * легко вызывать из WebAssembly; здесь нет ничего от Emscripten — модуль
 * собирается и проверяется обычным компилятором.
 *
 * Возвращённые строки принадлежат модулю и живут до следующего вызова той же
 * функции.
 */
#ifndef TL_WEB_H
#define TL_WEB_H

#include <stdbool.h>

/* ---- проект ---------------------------------------------------------------- */

/* Пример проекта (как при первом запуске программы). */
void tl_web_new_demo(void);
/* Проект из текста файла; false — ошибка (см. tl_web_error), проект прежний. */
bool tl_web_open(const char *json);
const char *tl_web_error(void);
/* Проект — текстом файла (*.tilelayout.json, как у настольной программы). */
const char *tl_web_save(void);

/* ---- что рисовать ------------------------------------------------------------ */

/* Сцена в мм (формат — в tl_web_scene.c). */
const char *tl_web_scene(void);
/* Комната в точке (мм) или 0. */
int tl_web_space_at(double x, double y);

/* ---- плитка комнаты ------------------------------------------------------- */

/* Настройки плитки комнаты (у общего пола — комнаты-источника):
   {"space": id, "name": "...", "source": "..." | null, "tile": {ключ: значение}}.
   Ключи — как в файле проекта (tl_tile_settings_fields). */
const char *tl_web_space_tile(int space_id);
/* Поле плитки комнаты; value — JSON-значение (число, true/false, строка).
   now_seconds — для склейки серии правок одного поля в один шаг отмены. */
bool tl_web_set_tile_field(int space_id, const char *key, const char *value, double now_seconds);
/* Несколько полей одним шагом отмены: fields — JSON-объект {ключ: значение}. */
bool tl_web_set_tile_fields(int space_id, const char *fields, double now_seconds);

/* Раскладка комнаты: смещение сетки по правилу mode — "top_left", "top_right",
   "bottom_left", "bottom_right" (целая плитка в углу), "center" (центр плитки в
   центре комнаты), "center_joint" (там — пересечение швов), "min_tiles"
   (меньше всего плиток к покупке), "min_narrow" (меньше узких подрезок).
   Ответ — {"ok": bool, "message": "что получилось"}. */
const char *tl_web_align_tiles(int space_id, const char *mode, double now_seconds);
bool tl_web_set_space_name(int space_id, const char *name, double now_seconds);

/* ---- правка плана (tl_web_edit.c) ---------------------------------------------
 * Предметы плана — {"kind": "partition" | "opening" | "house", "index": номер в
 * списке проекта}; номера живут до следующей правки. tolerance — допуск
 * попадания в мм (из масштаба вида), grid — шаг сетки, мм (0 — без сетки). */

/* Точка курсора с привязками: к углам, концам перегородок, граням стен, сетке,
   выравнивание с опорными точками и с началом start (has_start).
   refs — JSON-массив [x0, y0, …] — точки рисуемого контура. free — без привязок.
   {"point": [x, y], "on_face": bool, "face": [x0, y0, x1, y1] | null,
    "guides": [[x, y, вертикальная], …]} */
const char *tl_web_snap(double x, double y, bool has_start, double start_x, double start_y, const char *refs,
                        double tolerance, double grid, bool free);
/* Контур [x0, y0, …]: на пустом месте — дом, внутри дома — комната.
   {"result": "house" | "room" | "partitions" | "invalid", "space": id комнаты в контуре или 0} */
const char *tl_web_add_contour(const char *points, double house_thickness, double partition_thickness,
                               double now_seconds);
/* Перегородка; ответ — номер или −1 (слишком короткая). */
int tl_web_add_partition(double x0, double y0, double x1, double y1, double thickness, double now_seconds);
/* Проём шириной width на стене или перегородке у точки: предпросмотр — вырез
   {"cut": [...], "host": "wall" | "partition"} или null; добавить — номер или −1. */
const char *tl_web_opening_preview(double x, double y, double width, double tolerance, double grid);
int tl_web_add_opening(double x, double y, double width, double tolerance, double grid, double now_seconds);

/* Предмет под точкой — маркеры важнее тел: угол дома, середина узла
   перегородок, примыкание, свободный конец перегородки, проём, перегородка,
   стена дома, комната.
   {"kind": "vertex" | "node" | "tee" | "partition" | "opening" | "wall" | "space" | "none",
    "index": номер (у угла и стены — дома; у узла и примыкания — перегородки),
    "part": угол или стена дома; у перегородки 0 — тело, 1 — начало, 2 — конец,
    "end": конец перегородки в узле или примыкании (0 — начало, 1 — конец), "space": id комнаты} */
const char *tl_web_pick(double x, double y, double tolerance);
/* Свойства предмета (как от tl_web_pick) для панели и подсветки; null — нет такого.
   Общее: "outline" — контуры для подсветки, "handles" — точки маркеров, "locked";
   перегородка — "thickness", "length"; проём — "width", "host"; стена — "thickness",
   "length", "walls"; угол — "walls"; узел — "count" (перегородок). */
const char *tl_web_object(const char *kind, int index, int part, int end);
bool tl_web_set_partition_thickness(int index, double thickness, double now_seconds);
bool tl_web_set_opening_width(int index, double width, double now_seconds);
/* Толщина стены дома (вместе со смежной стеной соседнего дома). */
bool tl_web_set_wall_thickness(int house, int wall, double thickness, double now_seconds);
/* Удалить: перегородку, проём, дом (kind "wall" или "house"), угол дома (если углов
   больше трёх), узел (сломанная перегородка выпрямляется). Заблокированное — нельзя. */
bool tl_web_delete(const char *kind, int index, int part, int end, double now_seconds);

/* Перетаскивание предмета (как от tl_web_pick; комнату не тянут — это сдвиг вида).
   begin — false, если тянуть нельзя; move — шаг (первый запоминает проект для
   отмены), ответ — {"guides": [...], "selection": предмет сейчас}; end — отпустили,
   ответ — предмет после правки (перегородки могли слиться). */
bool tl_web_drag_begin(const char *kind, int index, int part, int end, double x, double y);
const char *tl_web_drag_move(double x, double y, double tolerance, double grid, bool free, double now_seconds);
const char *tl_web_drag_end(void);

/* ---- расчёт и отмена ---------------------------------------------------------- */

/* Сводка к покупке (формат — в tl_web_summary.c). */
const char *tl_web_summary(void);

bool tl_web_undo(void);
bool tl_web_redo(void);
int tl_web_undo_count(void);
int tl_web_redo_count(void);

#endif

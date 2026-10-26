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

/* ---- расчёт и отмена ---------------------------------------------------------- */

/* Сводка к покупке (формат — в tl_web_summary.c). */
const char *tl_web_summary(void);

bool tl_web_undo(void);
bool tl_web_redo(void);
int tl_web_undo_count(void);
int tl_web_redo_count(void);

#endif

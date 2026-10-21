/*
 * История правок (паттерн «Хранитель»): перед изменением проекта его
 * снимок в JSON кладётся в стек отмены. Серия одинаковых правок подряд
 * (например, прокрутка поля ширины) склеивается в один шаг.
 */
#ifndef TL_HISTORY_H
#define TL_HISTORY_H

#include "tl_project.h"

#define TL_HISTORY_LIMIT 300
#define TL_HISTORY_MERGE_SECONDS 1.5

typedef struct TlHistory TlHistory;

TlHistory *tl_history_new(void);
void tl_history_free(TlHistory *history);
void tl_history_clear(TlHistory *history);

/* Запомнить состояние перед правкой. merge_key — одинаковые ключи в
 * пределах TL_HISTORY_MERGE_SECONDS склеиваются; "" — не склеивать. */
void tl_history_checkpoint(TlHistory *history, const TlProject *project, const char *merge_key,
                           double now_seconds);
bool tl_history_undo(TlHistory *history, TlProject *project);
bool tl_history_redo(TlHistory *history, TlProject *project);
size_t tl_history_undo_count(const TlHistory *history);
size_t tl_history_redo_count(const TlHistory *history);

#endif

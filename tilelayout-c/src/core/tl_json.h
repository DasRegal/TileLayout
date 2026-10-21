/*
 * Минимальный JSON: разбор в дерево и запись. Не зависит от локали —
 * числа всегда с точкой (иначе файлы ломались бы в русской локали).
 */
#ifndef TL_JSON_H
#define TL_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum TlJsonType {
    TL_JSON_NULL,
    TL_JSON_BOOL,
    TL_JSON_NUMBER,
    TL_JSON_STRING,
    TL_JSON_ARRAY,
    TL_JSON_OBJECT
} TlJsonType;

typedef struct TlJson TlJson;

TlJson *tl_json_parse(const char *text, char *error, size_t error_size);
/* Возвращает строку, освобождать free(). pretty — с отступами. */
char *tl_json_stringify(const TlJson *value, bool pretty);
void tl_json_free(TlJson *value);

TlJson *tl_json_null(void);
TlJson *tl_json_bool(bool value);
TlJson *tl_json_number(double value);
TlJson *tl_json_string(const char *value);
TlJson *tl_json_array(void);
TlJson *tl_json_object(void);
/* Добавляемое значение переходит во владение контейнера. */
void tl_json_array_push(TlJson *array, TlJson *item);
void tl_json_object_set(TlJson *object, const char *key, TlJson *value);

TlJsonType tl_json_type(const TlJson *value);
const TlJson *tl_json_get(const TlJson *object, const char *key);
size_t tl_json_length(const TlJson *array);
const TlJson *tl_json_at(const TlJson *array, size_t index);
double tl_json_number_or(const TlJson *value, double fallback);
bool tl_json_bool_or(const TlJson *value, bool fallback);
const char *tl_json_string_or(const TlJson *value, const char *fallback);

/* Число в текст без зависимости от локали, кратчайшая точная запись. */
void tl_format_number(double value, char *text, size_t size);
/* Разбор числа без зависимости от локали; end — где остановился разбор. */
double tl_parse_number(const char *text, const char **end);

#endif

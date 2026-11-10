/*
 * Типизированные динамические массивы.
 *
 * TL_ARRAY_DEFINE(TlPolygon, TlVec2, tl_polygon) создаёт тип
 *     typedef struct { TlVec2 *items; size_t count; size_t capacity; } TlPolygon;
 * и функции tl_polygon_push / _insert_at / _remove_at / _clear / _free /
 * _reserve / _copy / _last.
 *
 * Элементы копируются побайтно: массив не знает, как освобождать ресурсы
 * внутри элементов, — для этого у составных типов есть свои функции.
 */
#ifndef TL_ARRAY_H
#define TL_ARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"

#define TL_ARRAY_DEFINE(Name, Type, prefix)                                              \
    typedef struct Name {                                                                \
        Type *items;                                                                     \
        size_t count;                                                                    \
        size_t capacity;                                                                 \
    } Name;                                                                              \
                                                                                         \
    static inline void prefix##_reserve(Name *array, size_t wanted)                      \
    {                                                                                    \
        if (wanted <= array->capacity)                                                   \
            return;                                                                      \
        size_t grown = array->capacity ? array->capacity * 2 : 8;                        \
        array->capacity = grown > wanted ? grown : wanted;                               \
        array->items = tl_realloc(array->items, array->capacity * sizeof(Type));         \
    }                                                                                    \
                                                                                         \
    static inline void prefix##_push(Name *array, Type value)                            \
    {                                                                                    \
        prefix##_reserve(array, array->count + 1);                                       \
        array->items[array->count++] = value;                                            \
    }                                                                                    \
                                                                                         \
    static inline void prefix##_insert_at(Name *array, size_t index, Type value)         \
    {                                                                                    \
        prefix##_reserve(array, array->count + 1);                                       \
        memmove(array->items + index + 1, array->items + index,                          \
                (array->count - index) * sizeof(Type));                                  \
        array->items[index] = value;                                                     \
        array->count++;                                                                  \
    }                                                                                    \
                                                                                         \
    static inline void prefix##_remove_at(Name *array, size_t index)                     \
    {                                                                                    \
        memmove(array->items + index, array->items + index + 1,                          \
                (array->count - index - 1) * sizeof(Type));                              \
        array->count--;                                                                  \
    }                                                                                    \
                                                                                         \
    static inline Type *prefix##_last(Name *array)                                       \
    {                                                                                    \
        return array->count ? &array->items[array->count - 1] : NULL;                    \
    }                                                                                    \
                                                                                         \
    static inline void prefix##_clear(Name *array) { array->count = 0; }                 \
                                                                                         \
    static inline void prefix##_free(Name *array)                                        \
    {                                                                                    \
        free(array->items);                                                              \
        array->items = NULL;                                                             \
        array->count = array->capacity = 0;                                              \
    }                                                                                    \
                                                                                         \
    static inline Name prefix##_copy(const Name *source)                                 \
    {                                                                                    \
        Name copy = {0};                                                                 \
        prefix##_reserve(&copy, source->count);                                          \
        if (source->count)                                                               \
            memcpy(copy.items, source->items, source->count * sizeof(Type));             \
        copy.count = source->count;                                                      \
        return copy;                                                                     \
    }

/* Индекс элемента по указателю (линейный поиск) или -1. */
#define TL_ARRAY_INDEX_OF(array, value, out_index)                                       \
    do {                                                                                 \
        (out_index) = -1;                                                                \
        for (size_t tl_i_ = 0; tl_i_ < (array)->count; tl_i_++)                          \
            if ((array)->items[tl_i_] == (value)) {                                      \
                (out_index) = (long)tl_i_;                                               \
                break;                                                                   \
            }                                                                            \
    } while (0)

#endif

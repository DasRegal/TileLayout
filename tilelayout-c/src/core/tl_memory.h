/*
 * Выделение памяти. Нехватка памяти в настольном приложении — фатальная
 * ошибка: функции завершают программу с сообщением, поэтому вызывающему коду
 * не нужно проверять NULL после каждого выделения.
 */
#ifndef TL_MEMORY_H
#define TL_MEMORY_H

#include <stddef.h>

void *tl_malloc(size_t size);
void *tl_calloc(size_t count, size_t size);
void *tl_realloc(void *pointer, size_t size);
char *tl_strdup(const char *text);

/* Выделяет и обнуляет одну структуру: Type *x = TL_NEW(Type); */
#define TL_NEW(Type) ((Type *)tl_calloc(1, sizeof(Type)))

#endif

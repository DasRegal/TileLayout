#include "tl_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *checked(void *pointer, size_t size)
{
    if (pointer == NULL && size != 0) {
        fprintf(stderr, "TileLayout: не хватает памяти (%zu байт)\n", size);
        abort();
    }
    return pointer;
}

void *tl_malloc(size_t size) { return checked(malloc(size), size); }

void *tl_calloc(size_t count, size_t size) { return checked(calloc(count, size), count * size); }

void *tl_realloc(void *pointer, size_t size) { return checked(realloc(pointer, size), size); }

char *tl_strdup(const char *text)
{
    if (text == NULL)
        return NULL;
    size_t length = strlen(text) + 1;
    char *copy = tl_malloc(length);
    memcpy(copy, text, length);
    return copy;
}

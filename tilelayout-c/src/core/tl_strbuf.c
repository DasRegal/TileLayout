#include "tl_strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"

static void ensure_capacity(TlStrBuf *buffer, size_t extra)
{
    size_t needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity)
        return;
    size_t grown = buffer->capacity ? buffer->capacity * 2 : 64;
    buffer->capacity = grown > needed ? grown : needed;
    buffer->text = tl_realloc(buffer->text, buffer->capacity);
}

void tl_strbuf_append_length(TlStrBuf *buffer, const char *text, size_t length)
{
    ensure_capacity(buffer, length);
    memcpy(buffer->text + buffer->length, text, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
}

void tl_strbuf_append(TlStrBuf *buffer, const char *text)
{
    tl_strbuf_append_length(buffer, text, strlen(text));
}

void tl_strbuf_append_char(TlStrBuf *buffer, char symbol) { tl_strbuf_append_length(buffer, &symbol, 1); }

void tl_strbuf_appendf(TlStrBuf *buffer, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list measuring;
    va_copy(measuring, arguments);
    int length = vsnprintf(NULL, 0, format, measuring);
    va_end(measuring);
    if (length > 0) {
        ensure_capacity(buffer, (size_t)length);
        vsnprintf(buffer->text + buffer->length, (size_t)length + 1, format, arguments);
        buffer->length += (size_t)length;
    }
    va_end(arguments);
}

char *tl_strbuf_take(TlStrBuf *buffer)
{
    char *text = buffer->text ? buffer->text : tl_strdup("");
    *buffer = (TlStrBuf){0};
    return text;
}

void tl_strbuf_free(TlStrBuf *buffer)
{
    free(buffer->text);
    *buffer = (TlStrBuf){0};
}

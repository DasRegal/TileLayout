/* Растущая строка: дописывание текста, символов и форматированного вывода. */
#ifndef TL_STRBUF_H
#define TL_STRBUF_H

#include <stdarg.h>
#include <stddef.h>

typedef struct TlStrBuf {
    char *text;      /* всегда завершена нулём (после первого дописывания) */
    size_t length;
    size_t capacity;
} TlStrBuf;

void tl_strbuf_append(TlStrBuf *buffer, const char *text);
void tl_strbuf_append_length(TlStrBuf *buffer, const char *text, size_t length);
void tl_strbuf_append_char(TlStrBuf *buffer, char symbol);
void tl_strbuf_appendf(TlStrBuf *buffer, const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
/* Забирает строку (освобождать free()); буфер становится пустым. */
char *tl_strbuf_take(TlStrBuf *buffer);
void tl_strbuf_free(TlStrBuf *buffer);

#endif

#include "tl_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"
#include "tl_strbuf.h"

#define MAX_DEPTH 64

struct TlJson {
    TlJsonType type;
    bool boolean;
    double number;
    char *string;
    TlJson **items;  /* элементы массива или значения объекта */
    char **keys;     /* ключи объекта */
    size_t count, capacity;
};

/* ---- создание ------------------------------------------------------------- */

static TlJson *new_value(TlJsonType type)
{
    TlJson *value = TL_NEW(TlJson);
    value->type = type;
    return value;
}

TlJson *tl_json_null(void) { return new_value(TL_JSON_NULL); }

TlJson *tl_json_bool(bool flag)
{
    TlJson *value = new_value(TL_JSON_BOOL);
    value->boolean = flag;
    return value;
}

TlJson *tl_json_number(double number)
{
    TlJson *value = new_value(TL_JSON_NUMBER);
    value->number = number;
    return value;
}

TlJson *tl_json_string(const char *text)
{
    TlJson *value = new_value(TL_JSON_STRING);
    value->string = tl_strdup(text ? text : "");
    return value;
}

TlJson *tl_json_array(void) { return new_value(TL_JSON_ARRAY); }
TlJson *tl_json_object(void) { return new_value(TL_JSON_OBJECT); }

static void grow(TlJson *container)
{
    if (container->count < container->capacity)
        return;
    container->capacity = container->capacity ? container->capacity * 2 : 8;
    container->items = tl_realloc(container->items, container->capacity * sizeof(TlJson *));
    if (container->type == TL_JSON_OBJECT)
        container->keys = tl_realloc(container->keys, container->capacity * sizeof(char *));
}

void tl_json_array_push(TlJson *array, TlJson *item)
{
    grow(array);
    array->items[array->count++] = item;
}

void tl_json_object_set(TlJson *object, const char *key, TlJson *value)
{
    for (size_t i = 0; i < object->count; i++)
        if (strcmp(object->keys[i], key) == 0) {
            tl_json_free(object->items[i]);
            object->items[i] = value;
            return;
        }
    grow(object);
    object->keys[object->count] = tl_strdup(key);
    object->items[object->count++] = value;
}

void tl_json_free(TlJson *value)
{
    if (!value)
        return;
    for (size_t i = 0; i < value->count; i++) {
        tl_json_free(value->items[i]);
        if (value->keys)
            free(value->keys[i]);
    }
    free(value->items);
    free(value->keys);
    free(value->string);
    free(value);
}

/* ---- чтение --------------------------------------------------------------- */

TlJsonType tl_json_type(const TlJson *value) { return value ? value->type : TL_JSON_NULL; }

const TlJson *tl_json_get(const TlJson *object, const char *key)
{
    if (!object || object->type != TL_JSON_OBJECT)
        return NULL;
    for (size_t i = 0; i < object->count; i++)
        if (strcmp(object->keys[i], key) == 0)
            return object->items[i];
    return NULL;
}

const char *tl_json_key_at(const TlJson *object, size_t index)
{
    return object && object->type == TL_JSON_OBJECT && index < object->count ? object->keys[index] : NULL;
}

size_t tl_json_length(const TlJson *array)
{
    return array && (array->type == TL_JSON_ARRAY || array->type == TL_JSON_OBJECT) ? array->count : 0;
}

const TlJson *tl_json_at(const TlJson *array, size_t index)
{
    return index < tl_json_length(array) ? array->items[index] : NULL;
}

double tl_json_number_or(const TlJson *value, double fallback)
{
    return value && value->type == TL_JSON_NUMBER ? value->number : fallback;
}

bool tl_json_bool_or(const TlJson *value, bool fallback)
{
    return value && value->type == TL_JSON_BOOL ? value->boolean : fallback;
}

const char *tl_json_string_or(const TlJson *value, const char *fallback)
{
    return value && value->type == TL_JSON_STRING ? value->string : fallback;
}

/* ---- числа без локали ----------------------------------------------------- */

double tl_parse_number(const char *text, const char **end)
{
    const char *p = text;
    double sign = 1.0;
    if (*p == '-' || *p == '+')
        sign = *p++ == '-' ? -1.0 : 1.0;
    double mantissa = 0.0;
    int exponent = 0;
    bool digits = false;
    for (; *p >= '0' && *p <= '9'; p++, digits = true)
        mantissa = mantissa * 10.0 + (*p - '0');
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++, digits = true) {
            mantissa = mantissa * 10.0 + (*p - '0');
            exponent--;
        }
    if (digits && (*p == 'e' || *p == 'E')) {
        const char *exponent_start = p++;
        int exponent_sign = 1, value = 0;
        if (*p == '-' || *p == '+')
            exponent_sign = *p++ == '-' ? -1 : 1;
        if (*p >= '0' && *p <= '9') {
            for (; *p >= '0' && *p <= '9'; p++)
                value = value < 10000 ? value * 10 + (*p - '0') : value;
            exponent += exponent_sign * value;
        } else {
            p = exponent_start;
        }
    }
    if (end)
        *end = digits ? p : text;
    if (!digits)
        return 0.0;
    double scale = pow(10.0, fabs((double)exponent));
    return sign * (exponent < 0 ? mantissa / scale : mantissa * scale);
}

static void replace_decimal_comma(char *text)
{
    for (char *p = text; *p; p++)
        if (*p == ',')
            *p = '.';
}

void tl_format_number(double value, char *text, size_t size)
{
    if (!isfinite(value)) {
        snprintf(text, size, "0");
        return;
    }
    if (value == floor(value) && fabs(value) < 1e15) {
        snprintf(text, size, "%.0f", value);
        return;
    }
    for (int precision = 15; precision <= 17; precision++) {
        snprintf(text, size, "%.*g", precision, value);
        replace_decimal_comma(text);
        if (tl_parse_number(text, NULL) == value)
            return;
    }
}

/* ---- разбор --------------------------------------------------------------- */

typedef struct Parser {
    const char *at;
    char *error;
    size_t error_size;
    int depth;
} Parser;

static void skip_spaces(Parser *parser)
{
    while (*parser->at == ' ' || *parser->at == '\t' || *parser->at == '\n' || *parser->at == '\r')
        parser->at++;
}

static TlJson *fail(Parser *parser, const char *message)
{
    if (parser->error && !parser->error[0])
        snprintf(parser->error, parser->error_size, "JSON: %s", message);
    return NULL;
}

static void append_utf8(TlStrBuf *buffer, unsigned long code)
{
    if (code < 0x80) {
        tl_strbuf_append_char(buffer, (char)code);
    } else if (code < 0x800) {
        tl_strbuf_append_char(buffer, (char)(0xC0 | (code >> 6)));
        tl_strbuf_append_char(buffer, (char)(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        tl_strbuf_append_char(buffer, (char)(0xE0 | (code >> 12)));
        tl_strbuf_append_char(buffer, (char)(0x80 | ((code >> 6) & 0x3F)));
        tl_strbuf_append_char(buffer, (char)(0x80 | (code & 0x3F)));
    } else {
        tl_strbuf_append_char(buffer, (char)(0xF0 | (code >> 18)));
        tl_strbuf_append_char(buffer, (char)(0x80 | ((code >> 12) & 0x3F)));
        tl_strbuf_append_char(buffer, (char)(0x80 | ((code >> 6) & 0x3F)));
        tl_strbuf_append_char(buffer, (char)(0x80 | (code & 0x3F)));
    }
}

static bool read_hex4(Parser *parser, unsigned long *code)
{
    *code = 0;
    for (int i = 0; i < 4; i++) {
        char c = *parser->at++;
        int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                   : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0)
            return false;
        *code = *code * 16 + (unsigned long)digit;
    }
    return true;
}

static char *parse_string_text(Parser *parser)
{
    TlStrBuf buffer = {0};
    parser->at++; /* открывающая кавычка */
    while (*parser->at && *parser->at != '"') {
        char c = *parser->at++;
        if (c != '\\') {
            tl_strbuf_append_char(&buffer, c);
            continue;
        }
        char escape = *parser->at++;
        unsigned long code;
        switch (escape) {
        case 'n': tl_strbuf_append_char(&buffer, '\n'); break;
        case 't': tl_strbuf_append_char(&buffer, '\t'); break;
        case 'r': tl_strbuf_append_char(&buffer, '\r'); break;
        case 'b': tl_strbuf_append_char(&buffer, '\b'); break;
        case 'f': tl_strbuf_append_char(&buffer, '\f'); break;
        case 'u':
            if (!read_hex4(parser, &code)) {
                tl_strbuf_free(&buffer);
                return NULL;
            }
            if (code >= 0xD800 && code <= 0xDBFF && parser->at[0] == '\\' && parser->at[1] == 'u') {
                unsigned long low;
                parser->at += 2;
                if (read_hex4(parser, &low))
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            }
            append_utf8(&buffer, code);
            break;
        case '\0':
            tl_strbuf_free(&buffer);
            return NULL;
        default: tl_strbuf_append_char(&buffer, escape); break;
        }
    }
    if (*parser->at != '"') {
        tl_strbuf_free(&buffer);
        return NULL;
    }
    parser->at++;
    return tl_strbuf_take(&buffer);
}

static TlJson *parse_value(Parser *parser);

static TlJson *parse_container(Parser *parser, bool is_object)
{
    if (++parser->depth > MAX_DEPTH)
        return fail(parser, "слишком глубокая вложенность");
    TlJson *container = is_object ? tl_json_object() : tl_json_array();
    char closing = is_object ? '}' : ']';
    parser->at++;
    skip_spaces(parser);
    if (*parser->at == closing) {
        parser->at++;
        parser->depth--;
        return container;
    }
    for (;;) {
        char *key = NULL;
        skip_spaces(parser);
        if (is_object) {
            if (*parser->at != '"' || !(key = parse_string_text(parser)))
                break;
            skip_spaces(parser);
            if (*parser->at++ != ':') {
                free(key);
                break;
            }
        }
        TlJson *item = parse_value(parser);
        if (!item) {
            free(key);
            break;
        }
        if (is_object) {
            tl_json_object_set(container, key, item);
            free(key);
        } else {
            tl_json_array_push(container, item);
        }
        skip_spaces(parser);
        if (*parser->at == ',') {
            parser->at++;
            continue;
        }
        if (*parser->at == closing) {
            parser->at++;
            parser->depth--;
            return container;
        }
        break;
    }
    tl_json_free(container);
    return fail(parser, is_object ? "ошибка в объекте" : "ошибка в массиве");
}

static bool take_word(Parser *parser, const char *word)
{
    size_t length = strlen(word);
    if (strncmp(parser->at, word, length) != 0)
        return false;
    parser->at += length;
    return true;
}

static TlJson *parse_value(Parser *parser)
{
    skip_spaces(parser);
    char c = *parser->at;
    if (c == '{' || c == '[')
        return parse_container(parser, c == '{');
    if (c == '"') {
        char *text = parse_string_text(parser);
        if (!text)
            return fail(parser, "незакрытая строка");
        TlJson *value = new_value(TL_JSON_STRING);
        value->string = text;
        return value;
    }
    if (take_word(parser, "true"))
        return tl_json_bool(true);
    if (take_word(parser, "false"))
        return tl_json_bool(false);
    if (take_word(parser, "null"))
        return tl_json_null();
    const char *end;
    double number = tl_parse_number(parser->at, &end);
    if (end == parser->at)
        return fail(parser, "неожиданный символ");
    parser->at = end;
    return tl_json_number(number);
}

TlJson *tl_json_parse(const char *text, char *error, size_t error_size)
{
    if (error && error_size)
        error[0] = '\0';
    Parser parser = {text ? text : "", error, error_size, 0};
    TlJson *root = parse_value(&parser);
    if (!root)
        return NULL;
    skip_spaces(&parser);
    if (*parser.at) {
        tl_json_free(root);
        return fail(&parser, "лишний текст после значения");
    }
    return root;
}

/* ---- запись --------------------------------------------------------------- */

static void write_string(TlStrBuf *out, const char *text)
{
    tl_strbuf_append_char(out, '"');
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '"': tl_strbuf_append(out, "\\\""); break;
        case '\\': tl_strbuf_append(out, "\\\\"); break;
        case '\n': tl_strbuf_append(out, "\\n"); break;
        case '\r': tl_strbuf_append(out, "\\r"); break;
        case '\t': tl_strbuf_append(out, "\\t"); break;
        default:
            if (*p < 0x20)
                tl_strbuf_appendf(out, "\\u%04x", *p);
            else
                tl_strbuf_append_char(out, (char)*p);
        }
    }
    tl_strbuf_append_char(out, '"');
}

static void write_indent(TlStrBuf *out, bool pretty, int depth)
{
    if (!pretty)
        return;
    tl_strbuf_append_char(out, '\n');
    for (int i = 0; i < depth; i++)
        tl_strbuf_append(out, " ");
}

static void write_value(TlStrBuf *out, const TlJson *value, bool pretty, int depth)
{
    char number[40];
    switch (tl_json_type(value)) {
    case TL_JSON_NULL: tl_strbuf_append(out, "null"); break;
    case TL_JSON_BOOL: tl_strbuf_append(out, value->boolean ? "true" : "false"); break;
    case TL_JSON_NUMBER:
        tl_format_number(value->number, number, sizeof number);
        tl_strbuf_append(out, number);
        break;
    case TL_JSON_STRING: write_string(out, value->string); break;
    case TL_JSON_ARRAY:
    case TL_JSON_OBJECT: {
        bool is_object = value->type == TL_JSON_OBJECT;
        tl_strbuf_append_char(out, is_object ? '{' : '[');
        for (size_t i = 0; i < value->count; i++) {
            if (i)
                tl_strbuf_append_char(out, ',');
            write_indent(out, pretty, depth + 1);
            if (is_object) {
                write_string(out, value->keys[i]);
                tl_strbuf_append(out, pretty ? ": " : ":");
            }
            write_value(out, value->items[i], pretty, depth + 1);
        }
        if (value->count)
            write_indent(out, pretty, depth);
        tl_strbuf_append_char(out, is_object ? '}' : ']');
        break;
    }
    }
}

char *tl_json_stringify(const TlJson *value, bool pretty)
{
    TlStrBuf out = {0};
    write_value(&out, value, pretty, 0);
    return tl_strbuf_take(&out);
}

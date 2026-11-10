#include "app_share.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_project_io.h"

#define MAX_PROJECT_BYTES (64 * 1024 * 1024)

/* Весь data через (рас)паковщик; NULL — ошибка. */
static GBytes *convert(GConverter *converter, const void *data, gsize size)
{
    GInputStream *source = g_memory_input_stream_new_from_data(data, size, NULL);
    GInputStream *stream = g_converter_input_stream_new(source, converter);
    GByteArray *out = g_byte_array_new();
    guint8 buffer[16384];
    gssize read;
    while ((read = g_input_stream_read(stream, buffer, sizeof buffer, NULL, NULL)) > 0 && out->len < MAX_PROJECT_BYTES)
        g_byte_array_append(out, buffer, (guint)read);
    g_object_unref(stream);
    g_object_unref(source);
    if (read < 0) {
        g_byte_array_unref(out);
        return NULL;
    }
    return g_byte_array_free_to_bytes(out);
}

char *app_share_link(const TlProject *project)
{
    char *text = tl_project_snapshot(project); /* без картинки и линий подложки */
    GZlibCompressor *compressor = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, 9);
    GBytes *packed = convert(G_CONVERTER(compressor), text, strlen(text));
    g_object_unref(compressor);
    free(text);
    gsize size = 0;
    const guint8 *bytes = g_bytes_get_data(packed, &size);
    gchar *base64 = g_base64_encode(bytes, size);
    g_bytes_unref(packed);
    for (char *c = base64; *c; c++) /* base64url: «+/» → «-_», без «=» */
        *c = *c == '+' ? '-' : *c == '/' ? '_' : *c;
    char *end = strchr(base64, '=');
    if (end)
        *end = '\0';
    char *link = g_strconcat(APP_SHARE_SITE "#p=", base64, NULL);
    g_free(base64);
    return link;
}

TlProject *app_share_open(const char *link, char **error)
{
    const char *start = link ? strstr(link, "#p=") : NULL;
    if (!start) {
        *error = g_strdup("это не ссылка на проект TileLayout (в ней нет «#p=»)");
        return NULL;
    }
    GString *base64 = g_string_new(NULL);
    for (const char *c = start + 3; *c; c++) { /* без пробелов и переносов; base64url → base64 */
        if (g_ascii_isspace(*c))
            continue;
        g_string_append_c(base64, *c == '-' ? '+' : *c == '_' ? '/' : *c);
    }
    while (base64->len % 4)
        g_string_append_c(base64, '=');
    gsize size = 0;
    guchar *packed = g_base64_decode(base64->str, &size);
    g_string_free(base64, TRUE);
    GZlibDecompressor *decompressor = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW);
    GBytes *unpacked = size ? convert(G_CONVERTER(decompressor), packed, size) : NULL;
    g_object_unref(decompressor);
    g_free(packed);
    if (!unpacked) {
        *error = g_strdup("ссылка повреждена или обрезана — попросите прислать её ещё раз");
        return NULL;
    }
    gsize length = 0;
    const char *data = g_bytes_get_data(unpacked, &length);
    char *text = g_strndup(data, length);
    g_bytes_unref(unpacked);
    TlProject *project = tl_project_new();
    char message[256] = "";
    if (!tl_project_from_string(project, text, message, sizeof message)) {
        tl_project_free(project);
        project = NULL;
        *error = g_strdup(message[0] ? message : "в ссылке не проект TileLayout");
    }
    g_free(text);
    return project;
}

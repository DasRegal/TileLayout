#include "tl_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_json.h"
#include "tl_memory.h"

#define MIN_SIZE 10.0   /* мм: плитка меньше — опечатка */
#define MAX_SIZE 5000.0

TlCatalog *tl_catalog_new(const char *id, const char *name)
{
    TlCatalog *catalog = TL_NEW(TlCatalog);
    snprintf(catalog->id, sizeof catalog->id, "%s", id);
    snprintf(catalog->name, sizeof catalog->name, "%s", name && *name ? name : id);
    return catalog;
}

void tl_catalog_free(TlCatalog *catalog)
{
    if (!catalog)
        return;
    tl_catalog_tile_list_free(&catalog->tiles);
    free(catalog);
}

void tl_catalog_list_free_all(TlCatalogList *catalogs)
{
    for (size_t i = 0; i < catalogs->count; i++)
        tl_catalog_free(catalogs->items[i]);
    tl_catalog_list_free(catalogs);
}

static bool valid_size(double size) { return size >= MIN_SIZE && size <= MAX_SIZE; }

static bool valid_color(const char *color)
{
    if (strlen(color) != 7 || color[0] != '#')
        return false;
    for (int i = 1; i < 7; i++)
        if (!isxdigit((unsigned char)color[i]))
            return false;
    return true;
}

/* id — без «/»: он разделяет каталог и плитку в ссылке; длинный — обрезается. */
static void set_id(char *id, size_t size, const char *text)
{
    size_t length = strlen(text) < size - 1 ? strlen(text) : size - 1;
    memcpy(id, text, length);
    id[length] = '\0';
    for (char *c = id; *c; c++)
        if (*c == '/' || *c == '\\')
            *c = '_';
}

bool tl_catalog_parse(TlCatalog *catalog, const char *text, char *error, size_t error_size)
{
    TlJson *root = tl_json_parse(text, error, error_size);
    if (!root)
        return false;
    const char *name = tl_json_string_or(tl_json_get(root, "name"), NULL);
    if (name && *name)
        snprintf(catalog->name, sizeof catalog->name, "%s", name);
    const TlJson *tiles = tl_json_get(root, "tiles");
    for (size_t i = 0; i < tl_json_length(tiles); i++) {
        const TlJson *item = tl_json_at(tiles, i);
        TlCatalogTile tile = {0};
        snprintf(tile.image, sizeof tile.image, "%s", tl_json_string_or(tl_json_get(item, "image"), ""));
        tile.width = tl_json_number_or(tl_json_get(item, "width"), 0);
        tile.height = tl_json_number_or(tl_json_get(item, "height"), 0);
        if (!tile.image[0] || !valid_size(tile.width) || !valid_size(tile.height))
            continue;
        const char *id = tl_json_string_or(tl_json_get(item, "id"), tile.image);
        set_id(tile.id, sizeof tile.id, id);
        snprintf(tile.name, sizeof tile.name, "%s", tl_json_string_or(tl_json_get(item, "name"), tile.id));
        const char *color = tl_json_string_or(tl_json_get(item, "color"), "");
        if (valid_color(color))
            snprintf(tile.color, sizeof tile.color, "%s", color);
        tl_catalog_tile_list_push(&catalog->tiles, tile);
    }
    tl_json_free(root);
    return true;
}

/* ---- размер в имени файла ---------------------------------------------------- */

/* Знак между размерами: x, X, ×, кириллическая х/Х, *. Длина в байтах (UTF-8) или 0. */
static size_t times_sign(const char *text)
{
    static const char *const SIGNS[] = {"x", "X", "*", "\xc3\x97" /* × */, "\xd1\x85" /* х */, "\xd0\xa5" /* Х */};
    for (size_t i = 0; i < sizeof SIGNS / sizeof SIGNS[0]; i++)
        if (strncmp(text, SIGNS[i], strlen(SIGNS[i])) == 0)
            return strlen(SIGNS[i]);
    return 0;
}

/* Число (с точкой или запятой) с начала text; длина в байтах или 0. */
static size_t number_at(const char *text, double *value)
{
    size_t length = 0;
    char buffer[32];
    while ((isdigit((unsigned char)text[length]) || text[length] == '.' || text[length] == ',') &&
           length < sizeof buffer - 1) {
        buffer[length] = text[length] == ',' ? '.' : text[length];
        length++;
    }
    if (length == 0 || !isdigit((unsigned char)text[0]))
        return 0;
    buffer[length] = '\0';
    *value = strtod(buffer, NULL);
    return length;
}

static size_t spaces_at(const char *text)
{
    size_t length = 0;
    while (text[length] == ' ')
        length++;
    return length;
}

/* «600x300», «600 × 300» в text: последнее вхождение — [*from, *to). */
static bool find_size(const char *text, double *width, double *height, size_t *from, size_t *to)
{
    bool found = false;
    for (size_t i = 0; text[i]; i++) {
        bool inside_number = i > 0 && (isdigit((unsigned char)text[i - 1]) || text[i - 1] == ',' || text[i - 1] == '.');
        if (!isdigit((unsigned char)text[i]) || inside_number)
            continue;
        double first, second;
        size_t at = i, length = number_at(text + at, &first);
        if (!length)
            continue;
        at += length;
        at += spaces_at(text + at);
        size_t sign = times_sign(text + at);
        if (!sign)
            continue;
        at += sign;
        at += spaces_at(text + at);
        length = number_at(text + at, &second);
        if (!length)
            continue;
        *width = first;
        *height = second;
        *from = i;
        *to = at + length;
        found = true;
    }
    return found;
}

static void trim(char *text)
{
    for (char *c = text; *c; c++)
        if (*c == '_')
            *c = ' ';
    size_t length = strlen(text);
    while (length > 0 && strchr(" -.,", text[length - 1]))
        text[--length] = '\0';
    size_t start = strspn(text, " -.,");
    memmove(text, text + start, strlen(text + start) + 1);
}

bool tl_catalog_tile_from_file(const char *file, TlCatalogTile *tile)
{
    *tile = (TlCatalogTile){0};
    char stem[TL_CATALOG_FILE_LENGTH];
    snprintf(stem, sizeof stem, "%s", file);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem)
        *dot = '\0';
    size_t from, to;
    if (!find_size(stem, &tile->width, &tile->height, &from, &to) || !valid_size(tile->width) ||
        !valid_size(tile->height))
        return false;
    snprintf(tile->image, sizeof tile->image, "%s", file);
    set_id(tile->id, sizeof tile->id, stem);
    char name[TL_CATALOG_NAME_LENGTH]; /* имя без размера: до него и после */
    size_t before = from < sizeof name - 1 ? from : sizeof name - 1;
    size_t after = strlen(stem + to);
    if (after > sizeof name - 1 - before)
        after = sizeof name - 1 - before;
    memcpy(name, stem, before);
    memcpy(name + before, stem + to, after);
    name[before + after] = '\0';
    trim(name);
    if (name[0])
        snprintf(tile->name, sizeof tile->name, "%s", name);
    else
        snprintf(tile->name, sizeof tile->name, "%g\xc3\x97%g", tile->width, tile->height);
    return true;
}

/* ---- группы ------------------------------------------------------------------- */

/* «group/leaf» в out (обрезается по размеру). */
static void join(char *out, size_t size, const char *group, const char *leaf)
{
    char joined[TL_CATALOG_FILE_LENGTH + TL_CATALOG_ID_LENGTH + 1];
    size_t group_length = strlen(group), leaf_length = strlen(leaf);
    memcpy(joined, group, group_length);
    joined[group_length] = '/';
    memcpy(joined + group_length + 1, leaf, leaf_length + 1);
    size_t length = strlen(joined) < size - 1 ? strlen(joined) : size - 1;
    memcpy(out, joined, length);
    out[length] = '\0';
}

void tl_catalog_tile_set_group(TlCatalogTile *tile, const char *group)
{
    if (!group || !group[0])
        return;
    char id[TL_CATALOG_ID_LENGTH], image[TL_CATALOG_FILE_LENGTH];
    join(id, sizeof id, group, tile->id);
    join(image, sizeof image, group, tile->image);
    memcpy(tile->id, id, sizeof id);
    memcpy(tile->image, image, sizeof image);
    join(tile->group, sizeof tile->group, group, "");
    tile->group[strlen(tile->group) - 1] = '\0'; /* без «/» в конце */
}

bool tl_catalog_tile_in_group(const TlCatalogTile *tile, const char *group)
{
    size_t length = group ? strlen(group) : 0;
    return length == 0 ||
           (strncmp(tile->group, group, length) == 0 && (tile->group[length] == '\0' || tile->group[length] == '/'));
}

/* ---- ссылки ------------------------------------------------------------------- */

void tl_catalog_reference(const TlCatalog *catalog, const TlCatalogTile *tile, char *reference, size_t size)
{
    snprintf(reference, size, "%s/%s", catalog->id, tile->id);
}

const TlCatalogTile *tl_catalog_find(const TlCatalogList *catalogs, const char *reference,
                                     const TlCatalog **catalog)
{
    const char *slash = reference ? strchr(reference, '/') : NULL;
    if (!slash)
        return NULL;
    size_t id_length = (size_t)(slash - reference);
    for (size_t c = 0; c < catalogs->count; c++) {
        const TlCatalog *candidate = catalogs->items[c];
        if (strlen(candidate->id) != id_length || strncmp(candidate->id, reference, id_length) != 0)
            continue;
        for (size_t t = 0; t < candidate->tiles.count; t++)
            if (strcmp(candidate->tiles.items[t].id, slash + 1) == 0) {
                if (catalog)
                    *catalog = candidate;
                return &candidate->tiles.items[t];
            }
    }
    return NULL;
}

void tl_catalog_apply(const TlCatalog *catalog, const TlCatalogTile *tile, TlTileSettings *settings)
{
    settings->tile_width = tile->width;
    settings->tile_height = tile->height;
    tl_catalog_reference(catalog, tile, settings->texture, sizeof settings->texture);
    if (tile->color[0])
        snprintf(settings->color, sizeof settings->color, "%s", tile->color);
}

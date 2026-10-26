/*
 * Обход папок каталога: одна логика для программы (диск через GLib), сборки
 * сайта (диск через POSIX) и чего угодно ещё — чтение даёт TlCatalogSource.
 */
#include "tl_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"

bool tl_catalog_is_image(const char *file)
{
    static const char *const EXTENSIONS[] = {".jpg", ".jpeg", ".png", ".webp", ".bmp"};
    size_t length = strlen(file);
    for (size_t i = 0; i < sizeof EXTENSIONS / sizeof EXTENSIONS[0]; i++) {
        size_t extension = strlen(EXTENSIONS[i]);
        if (length <= extension)
            continue;
        bool same = true;
        for (size_t c = 0; c < extension && same; c++)
            same = tolower((unsigned char)file[length - extension + c]) == EXTENSIONS[i][c];
        if (same)
            return true;
    }
    return false;
}

/* «path/name» (или name у корня) — новая строка. */
static char *child_path(const char *path, const char *name)
{
    size_t path_length = strlen(path), name_length = strlen(name);
    char *child = tl_malloc(path_length + name_length + 2);
    if (path_length) {
        memcpy(child, path, path_length);
        child[path_length] = '/';
        memcpy(child + path_length + 1, name, name_length + 1);
    } else {
        memcpy(child, name, name_length + 1);
    }
    return child;
}

static void free_names(char **names, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* Плитки одной папки — с группой path. */
static void load_tiles(TlCatalog *catalog, const TlCatalogSource *source, const char *path, char **names,
                       size_t count)
{
    TlCatalog *part = tl_catalog_new(catalog->id, catalog->name);
    char *description_path = child_path(path, TL_CATALOG_FILE);
    char *text = source->read_text(source->data, description_path);
    if (text) {
        char error[160] = "";
        if (!tl_catalog_parse(part, text, error, sizeof error) && source->warn)
            source->warn(source->data, description_path, error);
        if (!path[0])
            snprintf(catalog->name, sizeof catalog->name, "%s", part->name);
    } else {
        for (size_t i = 0; i < count; i++) {
            TlCatalogTile tile;
            if (tl_catalog_is_image(names[i]) && tl_catalog_tile_from_file(names[i], &tile))
                tl_catalog_tile_list_push(&part->tiles, tile);
        }
    }
    for (size_t i = 0; i < part->tiles.count; i++) {
        tl_catalog_tile_set_group(&part->tiles.items[i], path);
        tl_catalog_tile_list_push(&catalog->tiles, part->tiles.items[i]);
    }
    free(text);
    free(description_path);
    tl_catalog_free(part);
}

static void load_folder(TlCatalog *catalog, const TlCatalogSource *source, const char *path, int depth)
{
    size_t count = 0;
    char **names = source->list(source->data, path, &count);
    if (!names)
        return;
    load_tiles(catalog, source, path, names, count);
    for (size_t i = 0; i < count && depth < TL_CATALOG_MAX_DEPTH; i++) {
        if (names[i][0] == '.')
            continue;
        char *child = child_path(path, names[i]);
        if (source->is_folder(source->data, child))
            load_folder(catalog, source, child, depth + 1);
        free(child);
    }
    free_names(names, count);
}

void tl_catalog_load(TlCatalog *catalog, const TlCatalogSource *source)
{
    load_folder(catalog, source, "", 0);
}

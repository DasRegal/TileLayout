/*
 * Список каталогов для сайта: браузер не может обойти папки на сервере, поэтому
 * при сборке сайта каталоги читаются здесь (тем же загрузчиком, что у
 * программы) и записываются одним файлом:
 *
 *   tl_catalog_index <папка каталогов> <catalogs.json>
 *
 * [{"id": "samples", "name": "Образцы TileLayout",
 *   "tiles": [{"id", "group", "name", "width", "height", "image", "color"}]}]
 * image — от папки каталога: «<папка каталогов>/<id>/<image>».
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tl_catalog.h"
#include "tl_json.h"
#include "tl_memory.h"

static char *join(const char *root, const char *path)
{
    size_t root_length = strlen(root), path_length = strlen(path);
    char *full = tl_malloc(root_length + path_length + 2);
    memcpy(full, root, root_length);
    full[root_length] = '/';
    memcpy(full + root_length + 1, path, path_length + 1);
    return full;
}

static int compare_names(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static char **list_folder(void *root, const char *path, size_t *count)
{
    char *folder = join(root, path);
    DIR *dir = opendir(folder);
    free(folder);
    if (!dir)
        return NULL;
    size_t capacity = 16;
    char **names = tl_malloc(capacity * sizeof *names);
    *count = 0;
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (*count == capacity)
            names = tl_realloc(names, (capacity *= 2) * sizeof *names);
        names[(*count)++] = tl_strdup(entry->d_name);
    }
    closedir(dir);
    qsort(names, *count, sizeof *names, compare_names);
    return names;
}

static bool is_folder(void *root, const char *path)
{
    char *full = join(root, path);
    struct stat info;
    bool folder = stat(full, &info) == 0 && S_ISDIR(info.st_mode);
    free(full);
    return folder;
}

static char *read_text(void *root, const char *path)
{
    char *full = join(root, path);
    FILE *file = fopen(full, "rb");
    free(full);
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = NULL;
    if (size >= 0) {
        text = tl_malloc((size_t)size + 1);
        text[fread(text, 1, (size_t)size, file)] = '\0';
    }
    fclose(file);
    return text;
}

static void warn(void *root, const char *path, const char *error)
{
    fprintf(stderr, "Каталог %s/%s: %s\n", (const char *)root, path, error);
}

static TlJson *tile_json(const TlCatalogTile *tile)
{
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "id", tl_json_string(tile->id));
    tl_json_object_set(json, "group", tl_json_string(tile->group));
    tl_json_object_set(json, "name", tl_json_string(tile->name));
    tl_json_object_set(json, "width", tl_json_number(tile->width));
    tl_json_object_set(json, "height", tl_json_number(tile->height));
    tl_json_object_set(json, "image", tl_json_string(tile->image));
    tl_json_object_set(json, "color", tl_json_string(tile->color));
    return json;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Использование: %s <папка каталогов> <catalogs.json>\n", argv[0]);
        return 2;
    }
    size_t count = 0;
    char **ids = list_folder(argv[1], "", &count);
    if (!ids) {
        fprintf(stderr, "Нет папки %s\n", argv[1]);
        return 1;
    }
    TlJson *catalogs = tl_json_array();
    size_t tiles = 0;
    for (size_t i = 0; i < count; i++) {
        char *folder = join(argv[1], ids[i]);
        if (ids[i][0] != '.' && is_folder(argv[1], ids[i])) {
            TlCatalog *catalog = tl_catalog_new(ids[i], ids[i]);
            TlCatalogSource source = {folder, list_folder, is_folder, read_text, warn};
            tl_catalog_load(catalog, &source);
            if (catalog->tiles.count) {
                TlJson *json = tl_json_object();
                tl_json_object_set(json, "id", tl_json_string(catalog->id));
                tl_json_object_set(json, "name", tl_json_string(catalog->name));
                TlJson *list = tl_json_array();
                for (size_t t = 0; t < catalog->tiles.count; t++)
                    tl_json_array_push(list, tile_json(&catalog->tiles.items[t]));
                tl_json_object_set(json, "tiles", list);
                tl_json_array_push(catalogs, json);
                tiles += catalog->tiles.count;
            }
            tl_catalog_free(catalog);
        }
        free(folder);
        free(ids[i]);
    }
    free(ids);
    char *text = tl_json_stringify(catalogs, true);
    tl_json_free(catalogs);
    FILE *out = fopen(argv[2], "wb");
    bool ok = out && fputs(text, out) >= 0 && fputc('\n', out) != EOF;
    if (out)
        ok = fclose(out) == 0 && ok;
    free(text);
    if (!ok) {
        fprintf(stderr, "Не записать %s\n", argv[2]);
        return 1;
    }
    printf("%zu плиток в %s\n", tiles, argv[2]);
    return 0;
}

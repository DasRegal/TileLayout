#include "spec.h"
#include "specs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_catalog.h"
#include "tl_project.h"
#include "tl_project_io.h"

static const char *const DESCRIPTION =
    "{\"name\": \"Образцы\", \"tiles\": ["
    " {\"id\": \"marble\", \"name\": \"Мрамор\", \"width\": 600, \"height\": 300, \"image\": \"marble.jpg\","
    "  \"color\": \"#e8e6e1\"},"
    " {\"name\": \"Без размера\", \"image\": \"broken.jpg\"},"
    " {\"name\": \"Без рисунка\", \"width\": 300, \"height\": 300},"
    " {\"width\": 1200, \"height\": 200, \"image\": \"plank.png\"}"
    "]}";

/* Каталог в памяти: пути файлов и папок (папки — с «/» в конце), тексты файлов. */
typedef struct MemoryFile {
    const char *path, *text;
} MemoryFile;

static const MemoryFile MEMORY_CATALOG[] = {
    {"catalog.json", "{\"name\": \"Образцы\", \"tiles\": [{\"id\": \"marble\", \"width\": 600, \"height\": 300,"
                     " \"image\": \"marble.jpg\"}]}"},
    {"marble.jpg", ""},
    {"Фабрика/", NULL},
    {"Фабрика/60×120/", NULL},
    {"Фабрика/60×120/Бетон 1200x600.jpg", ""},
    {"Фабрика/Без размера.jpg", ""},
    {"Фабрика/Дуб 1200x200.png", ""},
    {"Фабрика/notes.txt", ""},
    {".скрытая/", NULL},
    {".скрытая/Мрамор 600x600.jpg", ""},
};

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    return memcpy(malloc(size), text, size);
}

/* Прямые потомки папки path: «path/имя» без дальнейших «/» (кроме конечного у папки). */
static char **memory_list(void *data, const char *path, size_t *count)
{
    (void)data;
    size_t total = sizeof MEMORY_CATALOG / sizeof MEMORY_CATALOG[0], prefix = strlen(path) + (path[0] ? 1 : 0);
    char **names = malloc(total * sizeof *names);
    *count = 0;
    for (size_t i = 0; i < total; i++) {
        const char *file = MEMORY_CATALOG[i].path;
        if (path[0] && (strncmp(file, path, strlen(path)) != 0 || file[strlen(path)] != '/'))
            continue;
        const char *name = file + prefix;
        const char *slash = strchr(name, '/');
        if (!name[0] || (slash && slash[1]))
            continue;
        names[*count] = copy_text(name);
        if (slash)
            names[*count][slash - name] = '\0';
        (*count)++;
    }
    return names;
}

static const MemoryFile *memory_find(const char *path, bool folder)
{
    for (size_t i = 0; i < sizeof MEMORY_CATALOG / sizeof MEMORY_CATALOG[0]; i++) {
        const char *file = MEMORY_CATALOG[i].path;
        size_t length = strlen(path);
        if (strncmp(file, path, length) == 0 && (folder ? strcmp(file + length, "/") == 0 : !file[length]))
            return &MEMORY_CATALOG[i];
    }
    return NULL;
}

static bool memory_is_folder(void *data, const char *path) { (void)data; return memory_find(path, true) != NULL; }

static char *memory_read(void *data, const char *path)
{
    (void)data;
    const MemoryFile *file = memory_find(path, false);
    return file && file->text && file->text[0] ? copy_text(file->text) : NULL;
}

void spec_catalog(void)
{
    DESCRIBE("Каталог плитки")
    {
        IT("читает описание: название, плитки с размером и рисунком; неполные пропускает")
        {
            TlCatalog *catalog = tl_catalog_new("samples", NULL);
            char error[128] = "";
            EXPECT(tl_catalog_parse(catalog, DESCRIPTION, error, sizeof error));
            EXPECT(strcmp(catalog->name, "Образцы") == 0);
            EXPECT_INT(catalog->tiles.count, 2);
            const TlCatalogTile *marble = &catalog->tiles.items[0];
            EXPECT(strcmp(marble->id, "marble") == 0 && strcmp(marble->name, "Мрамор") == 0);
            EXPECT_NEAR(marble->width, 600, 1e-9);
            EXPECT(strcmp(marble->color, "#e8e6e1") == 0);
            /* без id и названия — по файлу рисунка */
            EXPECT(strcmp(catalog->tiles.items[1].id, "plank.png") == 0);
            tl_catalog_free(catalog);
        }
        IT("плитка из имени файла: размер в имени через x, х или ×")
        {
            TlCatalogTile tile;
            EXPECT(tl_catalog_tile_from_file("Мрамор Каррара 600x300.jpg", &tile));
            EXPECT(strcmp(tile.name, "Мрамор Каррара") == 0);
            EXPECT_NEAR(tile.width, 600, 1e-9);
            EXPECT_NEAR(tile.height, 300, 1e-9);
            EXPECT(strcmp(tile.image, "Мрамор Каррара 600x300.jpg") == 0);
            EXPECT(tl_catalog_tile_from_file("dub_1200\xd1\x85" "200.png", &tile)); /* кириллическая х */
            EXPECT(strcmp(tile.name, "dub") == 0 && tile.width == 1200 && tile.height == 200);
            EXPECT(tl_catalog_tile_from_file("Мозаика 22,5 \xc3\x97 22,5.jpeg", &tile)); /* ×, дробные мм */
            EXPECT_NEAR(tile.width, 22.5, 1e-9);
            EXPECT(!tl_catalog_tile_from_file("Опечатка 7x15.jpg", &tile)); /* меньше 10 мм */
            EXPECT(tl_catalog_tile_from_file("300x300.jpg", &tile));
            EXPECT(strcmp(tile.name, "300\xc3\x97" "300") == 0);
            EXPECT(!tl_catalog_tile_from_file("Без размера.jpg", &tile));
            EXPECT(!tl_catalog_tile_from_file("Огромная 90000x300.jpg", &tile));
        }
        IT("ссылка «каталог/плитка» находит плитку; плитка задаёт комнате размер, рисунок и цвет")
        {
            TlCatalog *catalog = tl_catalog_new("samples", NULL);
            char error[128] = "";
            tl_catalog_parse(catalog, DESCRIPTION, error, sizeof error);
            TlCatalogList catalogs = {0};
            tl_catalog_list_push(&catalogs, catalog);
            const TlCatalog *owner = NULL;
            const TlCatalogTile *found = tl_catalog_find(&catalogs, "samples/marble", &owner);
            EXPECT(found && owner == catalog);
            EXPECT(!tl_catalog_find(&catalogs, "samples/none", NULL));
            EXPECT(!tl_catalog_find(&catalogs, "other/marble", NULL));
            EXPECT(!tl_catalog_find(&catalogs, "marble", NULL));
            TlTileSettings settings = tl_tile_settings_factory();
            tl_catalog_apply(owner, found, &settings);
            EXPECT(strcmp(settings.texture, "samples/marble") == 0);
            EXPECT(strcmp(settings.color, "#e8e6e1") == 0);
            EXPECT_NEAR(settings.tile_width, 600, 1e-9);
            tl_catalog_list_free_all(&catalogs);
        }
        IT("вложенные папки — группы: группа впереди id и пути рисунка, ссылка с группой находит плитку")
        {
            TlCatalog *catalog = tl_catalog_new("samples", NULL);
            TlCatalogTile tile, root;
            EXPECT(tl_catalog_tile_from_file("Мрамор 600x300.jpg", &tile));
            tl_catalog_tile_set_group(&tile, "Фабрика Север/60×120");
            EXPECT(strcmp(tile.group, "Фабрика Север/60×120") == 0);
            EXPECT(strcmp(tile.id, "Фабрика Север/60×120/Мрамор 600x300") == 0);
            EXPECT(strcmp(tile.image, "Фабрика Север/60×120/Мрамор 600x300.jpg") == 0);
            EXPECT(tl_catalog_tile_from_file("Бетон 600x600.jpg", &root));
            tl_catalog_tile_set_group(&root, "");
            EXPECT(strcmp(root.id, "Бетон 600x600") == 0 && !root.group[0]);
            tl_catalog_tile_list_push(&catalog->tiles, tile);
            tl_catalog_tile_list_push(&catalog->tiles, root);

            EXPECT(tl_catalog_tile_in_group(&tile, ""));
            EXPECT(tl_catalog_tile_in_group(&tile, "Фабрика Север"));
            EXPECT(tl_catalog_tile_in_group(&tile, "Фабрика Север/60×120"));
            EXPECT(!tl_catalog_tile_in_group(&tile, "Фабрика"));          /* только целые папки */
            EXPECT(!tl_catalog_tile_in_group(&root, "Фабрика Север"));

            TlCatalogList catalogs = {0};
            tl_catalog_list_push(&catalogs, catalog);
            const TlCatalogTile *found = tl_catalog_find(&catalogs, "samples/Фабрика Север/60×120/Мрамор 600x300", NULL);
            EXPECT(found && strcmp(found->name, "Мрамор") == 0);
            TlTileSettings settings = tl_tile_settings_factory();
            tl_catalog_apply(catalog, found, &settings);
            EXPECT(strcmp(settings.texture, "samples/Фабрика Север/60×120/Мрамор 600x300") == 0);
            tl_catalog_list_free_all(&catalogs);
        }
        IT("загрузка папки: описание корня, картинки вложенных папок — группы; скрытые и без размера пропускаются")
        {
            TlCatalog *catalog = tl_catalog_new("samples", NULL);
            TlCatalogSource source = {NULL, memory_list, memory_is_folder, memory_read, NULL};
            tl_catalog_load(catalog, &source);
            EXPECT(strcmp(catalog->name, "Образцы") == 0);
            EXPECT_INT((int)catalog->tiles.count, 3);
            TlCatalogList catalogs = {0};
            tl_catalog_list_push(&catalogs, catalog);
            EXPECT(tl_catalog_find(&catalogs, "samples/marble", NULL));
            const TlCatalogTile *oak = tl_catalog_find(&catalogs, "samples/Фабрика/Дуб 1200x200", NULL);
            EXPECT(oak && strcmp(oak->group, "Фабрика") == 0 && strcmp(oak->image, "Фабрика/Дуб 1200x200.png") == 0);
            const TlCatalogTile *concrete = tl_catalog_find(&catalogs, "samples/Фабрика/60×120/Бетон 1200x600", NULL);
            EXPECT(concrete && strcmp(concrete->group, "Фабрика/60×120") == 0);
            EXPECT(tl_catalog_is_image("a.JPEG") && !tl_catalog_is_image("notes.txt") && !tl_catalog_is_image(".jpg"));
            tl_catalog_list_free_all(&catalogs);
        }
        IT("рисунок плитки сохраняется в файле проекта; без рисунка — только цвет, как раньше")
        {
            TlProject *project = tl_project_demo();
            snprintf(project->spaces.items[0]->tile.texture, TL_TEXTURE_LENGTH, "%s", "samples/marble");
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT(strcmp(loaded->spaces.items[0]->tile.texture, "samples/marble") == 0);
            EXPECT(loaded->spaces.items[1]->tile.texture[0] == '\0');
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

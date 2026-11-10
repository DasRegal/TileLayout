/*
 * Каталоги плитки: у каждой плитки — название, размер и рисунок.
 *
 * Каталог — папка. Описание — catalog.json в ней:
 *   {"name": "Образцы",
 *    "tiles": [{"id": "marble", "name": "Мрамор", "width": 600, "height": 300,
 *               "image": "marble.jpg", "color": "#e8e6e1"}]}
 * («id» и «color» необязательны). Без catalog.json плитки — картинки папки,
 * размер — в имени файла: «Мрамор 600x300.jpg» (x, х или ×).
 *
 * Внутри каталога — вложенные папки любой глубины (фирма, формат, …), каждая
 * описана так же — своим catalog.json или картинками. Путь папки внутри
 * каталога — группа плитки: «Фабрика Север/60×120».
 *
 * Плитка в настройках комнаты — ссылка «каталог/плитка» (TlTileSettings.texture):
 * каталог — имя его папки, плитка — её id; у плитки во вложенной папке id
 * начинается с группы: «samples/Фабрика Север/60×120/marble».
 *
 * Здесь — только данные и разбор текста; чтение папок и картинок — у
 * приложения (так ядро годится и для веб-версии).
 */
#ifndef TL_CATALOG_H
#define TL_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#include "tl_array.h"
#include "tl_tile_settings.h"

#define TL_CATALOG_ID_LENGTH 128
#define TL_CATALOG_NAME_LENGTH 128
#define TL_CATALOG_FILE_LENGTH 256

typedef struct TlCatalogTile {
    char id[TL_CATALOG_ID_LENGTH];       /* с группой впереди: «группа/плитка» */
    char group[TL_CATALOG_ID_LENGTH];    /* папка внутри каталога; пусто — сам каталог */
    char name[TL_CATALOG_NAME_LENGTH];
    double width, height;                /* мм */
    char image[TL_CATALOG_FILE_LENGTH]; /* файл рисунка от папки каталога */
    char color[TL_COLOR_LENGTH];         /* средний цвет; пусто — неизвестен */
} TlCatalogTile;

TL_ARRAY_DEFINE(TlCatalogTileList, TlCatalogTile, tl_catalog_tile_list)

typedef struct TlCatalog {
    char id[TL_CATALOG_ID_LENGTH];
    char name[TL_CATALOG_NAME_LENGTH];
    TlCatalogTileList tiles;
} TlCatalog;

TL_ARRAY_DEFINE(TlCatalogList, TlCatalog *, tl_catalog_list)

TlCatalog *tl_catalog_new(const char *id, const char *name);
void tl_catalog_free(TlCatalog *catalog);
void tl_catalog_list_free_all(TlCatalogList *catalogs);

/* Описание из catalog.json. Плитки без размера или рисунка пропускаются. */
bool tl_catalog_parse(TlCatalog *catalog, const char *text, char *error, size_t error_size);
/* Плитка по имени файла рисунка: «Название 600x300.jpg». false — размера в имени нет. */
bool tl_catalog_tile_from_file(const char *file, TlCatalogTile *tile);
/* Плитка из вложенной папки group: группа — впереди id и пути рисунка. */
void tl_catalog_tile_set_group(TlCatalogTile *tile, const char *group);
/* Плитка в группе group или глубже; пустая группа — весь каталог. */
bool tl_catalog_tile_in_group(const TlCatalogTile *tile, const char *group);

/* ---- загрузка папки каталога (tl_catalog_load.c) ---------------------------- */

/* Откуда читается каталог: пути — от его корня («» — корень, вложенные — через
 * «/»). Чтение папок и файлов — у вызывающего (диск, сеть, память); память
 * результатов — tl_malloc / tl_strdup, освобождает загрузчик (free). */
typedef struct TlCatalogSource {
    void *data;
    /* Имена в папке path (файлы и папки) в порядке показа; *count — сколько.
       NULL — папки нет. */
    char **(*list)(void *data, const char *path, size_t *count);
    bool (*is_folder)(void *data, const char *path);
    /* Содержимое файла строкой; NULL — файла нет. */
    char *(*read_text)(void *data, const char *path);
    /* Ошибка в описании path (может быть NULL — молча). */
    void (*warn)(void *data, const char *path, const char *error);
} TlCatalogSource;

#define TL_CATALOG_FILE "catalog.json"
#define TL_CATALOG_MAX_DEPTH 8 /* вложенность папок (и защита от петель ссылок) */

/* Картинка ли файл (по расширению). */
bool tl_catalog_is_image(const char *file);
/* Плитки каталога: в каждой папке — из её catalog.json или из картинок с
   размером в имени; вложенные папки — группы. Название каталога — из
   catalog.json корня. */
void tl_catalog_load(TlCatalog *catalog, const TlCatalogSource *source);

/* Ссылка «каталог/плитка» и обратно. */
void tl_catalog_reference(const TlCatalog *catalog, const TlCatalogTile *tile, char *reference, size_t size);
const TlCatalogTile *tl_catalog_find(const TlCatalogList *catalogs, const char *reference,
                                     const TlCatalog **catalog);

/* Плитку из каталога — в настройки комнаты: размер, рисунок, цвет. */
void tl_catalog_apply(const TlCatalog *catalog, const TlCatalogTile *tile, TlTileSettings *settings);

#endif

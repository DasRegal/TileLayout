/*
 * Каталоги плитки на диске (формат — tl_catalog.h).
 *
 * Где лежат: встроенные — share/tilelayout/catalogs рядом с программой (при
 * запуске из папки сборки — data/catalogs исходников); свои — в папке данных
 * пользователя: Linux ~/.local/share/TileLayout/catalogs, Windows
 * %LOCALAPPDATA%\TileLayout\catalogs. Каждая подпапка — каталог.
 *
 * Рисунки загружаются при первом обращении и запоминаются.
 */
#ifndef APP_CATALOGS_H
#define APP_CATALOGS_H

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "tl_catalog.h"

/* Перечитать все каталоги (и забыть загруженные рисунки). */
void app_catalogs_reload(void);
const TlCatalogList *app_catalogs(void);
/* Папка своих каталогов (создаётся при первом обращении). */
const char *app_catalogs_user_directory(void);

/* Рисунок плитки по ссылке «каталог/плитка»; NULL — нет такой плитки или файла. */
cairo_surface_t *app_catalogs_image(const char *reference);
/* Уменьшенный рисунок для выбора, не больше size по длинной стороне (новая ссылка). */
GdkPixbuf *app_catalogs_thumbnail(const TlCatalog *catalog, const TlCatalogTile *tile, int size);
/* Плитку — в настройки комнаты; цвета нет в описании — средний по рисунку. */
void app_catalogs_apply(const TlCatalog *catalog, const TlCatalogTile *tile, TlTileSettings *settings);

#endif

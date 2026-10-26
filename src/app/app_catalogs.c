#include "app_catalogs.h"

#include <gdk/gdk.h>
#include <string.h>

#include "app_config.h"

#define CATALOG_FILE "catalog.json"
#define MAX_IMAGE_SIDE 1024 /* px: крупные фото уменьшаются — плитка на плане мельче */
#define MAX_DEPTH 8         /* вложенность папок в каталоге (и защита от петель ссылок) */

static struct {
    bool loaded;
    TlCatalogList list;
    GPtrArray *folders;  /* папка каждого каталога — параллельно list */
    GHashTable *images;  /* ссылка → cairo_surface_t (или NULL — не загрузился) */
    char *user_directory;
} catalogs;

/* ---- где лежат -------------------------------------------------------------- */

const char *app_catalogs_user_directory(void)
{
    if (!catalogs.user_directory) {
        catalogs.user_directory = g_build_filename(g_get_user_data_dir(), "TileLayout", "catalogs", NULL);
        g_mkdir_with_parents(catalogs.user_directory, 0755);
    }
    return catalogs.user_directory;
}

/* Встроенные каталоги: рядом с программой (переносная сборка — share/ рядом с
   exe, установленная — ../share/), иначе — исходники (запуск из папки сборки). */
static char *builtin_directory(void)
{
    char *executable = app_executable_directory();
    char *candidates[3] = {
        executable ? g_build_filename(executable, "share", "tilelayout", "catalogs", NULL) : NULL,
        executable ? g_build_filename(executable, "..", "share", "tilelayout", "catalogs", NULL) : NULL,
#ifdef TL_SOURCE_CATALOGS
        g_strdup(TL_SOURCE_CATALOGS),
#else
        NULL,
#endif
    };
    g_free(executable);
    char *found = NULL;
    for (int i = 0; i < 3; i++) {
        if (!found && candidates[i] && g_file_test(candidates[i], G_FILE_TEST_IS_DIR))
            found = candidates[i];
        else
            g_free(candidates[i]);
    }
    return found;
}

/* ---- загрузка ------------------------------------------------------------------ */

static bool is_image(const char *file)
{
    static const char *const EXTENSIONS[] = {".jpg", ".jpeg", ".png", ".webp", ".bmp"};
    char *lower = g_ascii_strdown(file, -1);
    bool image = false;
    for (size_t i = 0; i < G_N_ELEMENTS(EXTENSIONS) && !image; i++)
        image = g_str_has_suffix(lower, EXTENSIONS[i]);
    g_free(lower);
    return image;
}

static int compare_names(gconstpointer left, gconstpointer right)
{
    return g_utf8_collate(*(char *const *)left, *(char *const *)right);
}

/* Имена в папке по алфавиту (новый массив строк). */
static GPtrArray *sorted_entries(const char *folder)
{
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir)
        return names;
    for (const char *name; (name = g_dir_read_name(dir));)
        g_ptr_array_add(names, g_strdup(name));
    g_dir_close(dir);
    g_ptr_array_sort(names, compare_names);
    return names;
}

/* Плитки одной папки — из её catalog.json или из картинок с размером в имени —
   в каталог, с группой group. У корня каталога его название берётся из описания. */
static void load_tiles(TlCatalog *catalog, const char *folder, const char *group, GPtrArray *files)
{
    TlCatalog *part = tl_catalog_new(catalog->id, catalog->name);
    char *description = g_build_filename(folder, CATALOG_FILE, NULL);
    char *text = NULL;
    if (g_file_get_contents(description, &text, NULL, NULL)) {
        char error[160] = "";
        if (!tl_catalog_parse(part, text, error, sizeof error))
            g_warning("Каталог %s: %s", description, error);
        if (!group[0])
            g_strlcpy(catalog->name, part->name, sizeof catalog->name);
    } else {
        for (guint i = 0; i < files->len; i++) {
            TlCatalogTile tile;
            if (is_image(files->pdata[i]) && tl_catalog_tile_from_file(files->pdata[i], &tile))
                tl_catalog_tile_list_push(&part->tiles, tile);
        }
    }
    for (size_t i = 0; i < part->tiles.count; i++) {
        tl_catalog_tile_set_group(&part->tiles.items[i], group);
        tl_catalog_tile_list_push(&catalog->tiles, part->tiles.items[i]);
    }
    g_free(text);
    g_free(description);
    tl_catalog_free(part);
}

/* Папка и вложенные в неё (группы: «фирма/формат/…»). */
static void load_folder(TlCatalog *catalog, const char *folder, const char *group, int depth)
{
    GPtrArray *entries = sorted_entries(folder);
    load_tiles(catalog, folder, group, entries);
    for (guint i = 0; i < entries->len && depth < MAX_DEPTH; i++) {
        const char *name = entries->pdata[i];
        char *path = g_build_filename(folder, name, NULL);
        if (name[0] != '.' && g_file_test(path, G_FILE_TEST_IS_DIR)) {
            char *subgroup = group[0] ? g_strconcat(group, "/", name, NULL) : g_strdup(name);
            load_folder(catalog, path, subgroup, depth + 1);
            g_free(subgroup);
        }
        g_free(path);
    }
    g_ptr_array_free(entries, TRUE);
}

/* Каталог из папки; NULL — плиток в ней нет. */
static TlCatalog *load_catalog(const char *folder, const char *id)
{
    TlCatalog *catalog = tl_catalog_new(id, id);
    load_folder(catalog, folder, "", 0);
    if (catalog->tiles.count == 0) {
        tl_catalog_free(catalog);
        return NULL;
    }
    return catalog;
}

static bool has_catalog(const char *id)
{
    for (size_t i = 0; i < catalogs.list.count; i++)
        if (strcmp(catalogs.list.items[i]->id, id) == 0)
            return true;
    return false;
}

static void load_root(const char *root)
{
    if (!root)
        return;
    GPtrArray *entries = sorted_entries(root);
    for (guint i = 0; i < entries->len; i++) {
        const char *id = entries->pdata[i];
        char *folder = g_build_filename(root, id, NULL);
        TlCatalog *catalog = g_file_test(folder, G_FILE_TEST_IS_DIR) && !has_catalog(id) ? load_catalog(folder, id) : NULL;
        if (catalog) {
            tl_catalog_list_push(&catalogs.list, catalog);
            g_ptr_array_add(catalogs.folders, folder);
        } else {
            g_free(folder);
        }
    }
    g_ptr_array_free(entries, TRUE);
}

static void free_surface(gpointer surface)
{
    if (surface)
        cairo_surface_destroy(surface);
}

void app_catalogs_reload(void)
{
    tl_catalog_list_free_all(&catalogs.list);
    if (catalogs.folders)
        g_ptr_array_free(catalogs.folders, TRUE);
    if (catalogs.images)
        g_hash_table_destroy(catalogs.images);
    catalogs.folders = g_ptr_array_new_with_free_func(g_free);
    catalogs.images = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_surface);
    char *builtin = builtin_directory();
    load_root(builtin);
    g_free(builtin);
    load_root(app_catalogs_user_directory());
    catalogs.loaded = true;
}

const TlCatalogList *app_catalogs(void)
{
    if (!catalogs.loaded)
        app_catalogs_reload();
    return &catalogs.list;
}

/* ---- рисунки ------------------------------------------------------------------- */

static const char *folder_of(const TlCatalog *catalog)
{
    for (size_t i = 0; i < catalogs.list.count; i++)
        if (catalogs.list.items[i] == catalog)
            return catalogs.folders->pdata[i];
    return NULL;
}

/* Рисунок с диска, не больше max_side по длинной стороне (новая ссылка или NULL). */
static GdkPixbuf *load_pixbuf(const TlCatalog *catalog, const TlCatalogTile *tile, int max_side)
{
    const char *folder = folder_of(catalog);
    if (!folder)
        return NULL;
    char *path = g_build_filename(folder, tile->image, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, max_side, max_side, TRUE, NULL);
    g_free(path);
    return pixbuf;
}

static cairo_surface_t *surface_from(GdkPixbuf *pixbuf)
{
    int width = gdk_pixbuf_get_width(pixbuf), height = gdk_pixbuf_get_height(pixbuf);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cairo_t *cr = cairo_create(surface);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return surface;
}

cairo_surface_t *app_catalogs_image(const char *reference)
{
    if (!reference || !reference[0])
        return NULL;
    const TlCatalogList *list = app_catalogs();
    gpointer cached;
    if (g_hash_table_lookup_extended(catalogs.images, reference, NULL, &cached))
        return cached;
    const TlCatalog *catalog;
    const TlCatalogTile *tile = tl_catalog_find(list, reference, &catalog);
    GdkPixbuf *pixbuf = tile ? load_pixbuf(catalog, tile, MAX_IMAGE_SIDE) : NULL;
    cairo_surface_t *surface = pixbuf ? surface_from(pixbuf) : NULL;
    if (pixbuf)
        g_object_unref(pixbuf);
    g_hash_table_insert(catalogs.images, g_strdup(reference), surface); /* и NULL — чтобы не грузить снова */
    return surface;
}

GdkPixbuf *app_catalogs_thumbnail(const TlCatalog *catalog, const TlCatalogTile *tile, int size)
{
    return load_pixbuf(catalog, tile, size);
}

/* Средний цвет рисунка: по уменьшенной копии. */
static bool average_color(const TlCatalog *catalog, const TlCatalogTile *tile, char *color)
{
    GdkPixbuf *small = load_pixbuf(catalog, tile, 32);
    if (!small)
        return false;
    int width = gdk_pixbuf_get_width(small), height = gdk_pixbuf_get_height(small);
    int stride = gdk_pixbuf_get_rowstride(small), channels = gdk_pixbuf_get_n_channels(small);
    const guchar *pixels = gdk_pixbuf_read_pixels(small);
    double sum[3] = {0, 0, 0};
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            for (int c = 0; c < 3; c++)
                sum[c] += pixels[y * stride + x * channels + c];
    double count = (double)width * height;
    g_snprintf(color, TL_COLOR_LENGTH, "#%02x%02x%02x", (int)(sum[0] / count), (int)(sum[1] / count),
               (int)(sum[2] / count));
    g_object_unref(small);
    return true;
}

void app_catalogs_apply(const TlCatalog *catalog, const TlCatalogTile *tile, TlTileSettings *settings)
{
    TlCatalogTile with_color = *tile;
    if (!with_color.color[0])
        average_color(catalog, tile, with_color.color);
    tl_catalog_apply(catalog, &with_color, settings);
}

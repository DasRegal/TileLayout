#include "app_catalogs.h"

#include <gdk/gdk.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "tl_memory.h"

#define MAX_IMAGE_SIDE 1024 /* px: крупные фото уменьшаются — плитка на плане мельче */

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

/* ---- загрузка: обход — в ядре (tl_catalog_load), здесь — чтение диска ---------- */

static int compare_names(const void *left, const void *right)
{
    return g_utf8_collate(*(char *const *)left, *(char *const *)right);
}

static char **list_folder(void *root, const char *path, size_t *count)
{
    char *folder = g_build_filename(root, path, NULL);
    GDir *dir = g_dir_open(folder, 0, NULL);
    g_free(folder);
    if (!dir)
        return NULL;
    GPtrArray *names = g_ptr_array_new();
    for (const char *name; (name = g_dir_read_name(dir));)
        g_ptr_array_add(names, tl_strdup(name)); /* освобождает ядро — free */
    g_dir_close(dir);
    g_ptr_array_sort(names, compare_names);
    *count = names->len;
    char **result = tl_malloc((names->len + 1) * sizeof *result);
    memcpy(result, names->pdata, names->len * sizeof *result);
    g_ptr_array_free(names, TRUE);
    return result;
}

static bool is_folder(void *root, const char *path)
{
    char *full = g_build_filename(root, path, NULL);
    bool folder = g_file_test(full, G_FILE_TEST_IS_DIR);
    g_free(full);
    return folder;
}

static char *read_text(void *root, const char *path)
{
    char *full = g_build_filename(root, path, NULL), *text = NULL;
    char *result = g_file_get_contents(full, &text, NULL, NULL) ? tl_strdup(text) : NULL;
    g_free(text);
    g_free(full);
    return result;
}

static void warn(void *root, const char *path, const char *error)
{
    g_warning("Каталог %s/%s: %s", (const char *)root, path, error);
}

/* Каталог из папки; NULL — плиток в ней нет. */
static TlCatalog *load_catalog(const char *folder, const char *id)
{
    TlCatalog *catalog = tl_catalog_new(id, id);
    TlCatalogSource source = {(void *)folder, list_folder, is_folder, read_text, warn};
    tl_catalog_load(catalog, &source);
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
    size_t count = 0;
    char **entries = root ? list_folder((void *)root, "", &count) : NULL;
    for (size_t i = 0; i < count; i++) {
        const char *id = entries[i];
        char *folder = g_build_filename(root, id, NULL);
        TlCatalog *catalog = id[0] != '.' && g_file_test(folder, G_FILE_TEST_IS_DIR) && !has_catalog(id)
                                 ? load_catalog(folder, id)
                                 : NULL;
        if (catalog) {
            tl_catalog_list_push(&catalogs.list, catalog);
            g_ptr_array_add(catalogs.folders, folder);
        } else {
            g_free(folder);
        }
        free(entries[i]);
    }
    free(entries);
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

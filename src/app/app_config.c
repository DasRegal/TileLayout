#include "app_config.h"

#include <stddef.h>
#include <string.h>

#include "tl_tile_settings.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#define CONFIG_FOLDER "TileLayout"
#define CONFIG_FILE "tilelayout.ini"

/* ---- чтение и запись с умолчаниями ------------------------------------------ */

static double read_double(GKeyFile *file, const char *group, const char *key, double fallback)
{
    GError *error = NULL;
    double value = g_key_file_get_double(file, group, key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

static bool read_bool(GKeyFile *file, const char *group, const char *key, bool fallback)
{
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(file, group, key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

static int read_int(GKeyFile *file, const char *group, const char *key, int fallback)
{
    GError *error = NULL;
    int value = g_key_file_get_integer(file, group, key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

/* ---- файл ------------------------------------------------------------------------ */

char *app_executable_directory(void)
{
#ifdef G_OS_WIN32
    wchar_t module[4096];
    DWORD length = GetModuleFileNameW(NULL, module, G_N_ELEMENTS(module));
    char *executable = length ? g_utf16_to_utf8(module, length, NULL, NULL, NULL) : NULL;
#else
    char *executable = g_file_read_link("/proc/self/exe", NULL);
#endif
    char *directory = executable ? g_path_get_dirname(executable) : NULL;
    g_free(executable);
    return directory;
}

static char *config_path(void)
{
    char *directory = app_executable_directory();
    char *portable = directory ? g_build_filename(directory, CONFIG_FILE, NULL) : NULL;
    g_free(directory);
    if (portable && g_file_test(portable, G_FILE_TEST_IS_REGULAR))
        return portable;
    g_free(portable);
    return g_build_filename(g_get_user_config_dir(), CONFIG_FOLDER, CONFIG_FILE, NULL);
}

AppConfig *app_config_load(void)
{
    AppConfig *config = g_new0(AppConfig, 1);
    config->file = g_key_file_new();
    config->path = config_path();
    g_key_file_load_from_file(config->file, config->path, G_KEY_FILE_NONE, NULL);
    return config;
}

void app_config_save(AppConfig *config)
{
    char *folder = g_path_get_dirname(config->path);
    g_mkdir_with_parents(folder, 0700);
    g_free(folder);
    g_key_file_save_to_file(config->file, config->path, NULL);
}

void app_config_free(AppConfig *config)
{
    if (!config)
        return;
    g_key_file_free(config->file);
    g_free(config->path);
    g_free(config);
}

/* ---- документ: правка, вид, плитка для новых комнат ------------------------------------- */

typedef struct BoolSetting {
    const char *group, *key;
    size_t offset;  /* поле AppDocument */
} BoolSetting;

typedef struct DoubleSetting {
    const char *group, *key;
    size_t offset;
} DoubleSetting;

#define DOCUMENT_FIELD(member) offsetof(AppDocument, member)

static const BoolSetting BOOL_SETTINGS[] = {
    {"edit", "link_walls", DOCUMENT_FIELD(preferences.link_walls)},
    {"edit", "keep_angles", DOCUMENT_FIELD(preferences.keep_angles)},
    {"edit", "ruler_magnet", DOCUMENT_FIELD(preferences.measure_magnet)},
    {"edit", "auto_align", DOCUMENT_FIELD(preferences.auto_align)},
    {"view", "show_tiles", DOCUMENT_FIELD(view.tiles)},
    {"view", "show_clips", DOCUMENT_FIELD(view.clips)},
    {"view", "show_numbers", DOCUMENT_FIELD(view.numbers)},
    {"view", "show_dims", DOCUMENT_FIELD(view.dims)},
    {"view", "show_grid", DOCUMENT_FIELD(view.grid)},
    {"view", "show_expansion_joints", DOCUMENT_FIELD(view.expansion_joints)},
};

static const DoubleSetting DOUBLE_SETTINGS[] = {
    {"edit", "snap", DOCUMENT_FIELD(preferences.snap_step)},
    {"edit", "partition_thick", DOCUMENT_FIELD(preferences.partition_thickness)},
    {"edit", "opening_width", DOCUMENT_FIELD(preferences.opening_width)},
    {"edit", "expansion_joint_gap", DOCUMENT_FIELD(preferences.expansion_joint_gap)},
    {"edit", "outline_thick", DOCUMENT_FIELD(preferences.outline_thickness)},
    {"house", "w", DOCUMENT_FIELD(preferences.house_width)},
    {"house", "h", DOCUMENT_FIELD(preferences.house_height)},
    {"house", "thick", DOCUMENT_FIELD(preferences.house_thickness)},
};

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

static void read_tile_defaults(GKeyFile *file)
{
    if (!g_key_file_has_group(file, "tile"))
        return;
    TlTileSettings tile = tl_tile_settings_for_new_room();
    char *base = (char *)&tile;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++) {
        const TlTileField *field = &fields[i];
        void *member = base + field->offset;
        if (!field->remembered_for_new_rooms)
            continue;
        switch (field->type) {
        case TL_FIELD_BOOL: *(bool *)member = read_bool(file, "tile", field->key, *(bool *)member); break;
        case TL_FIELD_DOUBLE: *(double *)member = read_double(file, "tile", field->key, *(double *)member); break;
        case TL_FIELD_INT: *(int *)member = read_int(file, "tile", field->key, *(int *)member); break;
        case TL_FIELD_COLOR: {
            char *color = g_key_file_get_string(file, "tile", field->key, NULL);
            if (color && color[0] == '#' && strlen(color) == 7)
                g_strlcpy((char *)member, color, TL_COLOR_LENGTH);
            g_free(color);
            break;
        }
        case TL_FIELD_TEXT: {
            char *text = g_key_file_get_string(file, "tile", field->key, NULL);
            if (text)
                g_strlcpy((char *)member, text, TL_TEXTURE_LENGTH);
            g_free(text);
            break;
        }
        }
    }
    tl_tile_settings_remember_as_default(&tile);
}

static void write_tile_defaults(GKeyFile *file)
{
    TlTileSettings tile = tl_tile_settings_for_new_room();
    const char *base = (const char *)&tile;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++) {
        const TlTileField *field = &fields[i];
        const void *member = base + field->offset;
        if (!field->remembered_for_new_rooms)
            continue;
        switch (field->type) {
        case TL_FIELD_BOOL: g_key_file_set_boolean(file, "tile", field->key, *(const bool *)member); break;
        case TL_FIELD_DOUBLE: g_key_file_set_double(file, "tile", field->key, *(const double *)member); break;
        case TL_FIELD_INT: g_key_file_set_integer(file, "tile", field->key, *(const int *)member); break;
        case TL_FIELD_COLOR:
        case TL_FIELD_TEXT: g_key_file_set_string(file, "tile", field->key, (const char *)member); break;
        }
    }
}

void app_config_read_document(AppConfig *config, AppDocument *document)
{
    char *base = (char *)document;
    for (size_t i = 0; i < COUNT_OF(BOOL_SETTINGS); i++) {
        bool *field = (bool *)(base + BOOL_SETTINGS[i].offset);
        *field = read_bool(config->file, BOOL_SETTINGS[i].group, BOOL_SETTINGS[i].key, *field);
    }
    for (size_t i = 0; i < COUNT_OF(DOUBLE_SETTINGS); i++) {
        double *field = (double *)(base + DOUBLE_SETTINGS[i].offset);
        *field = read_double(config->file, DOUBLE_SETTINGS[i].group, DOUBLE_SETTINGS[i].key, *field);
    }
    read_tile_defaults(config->file);
}

void app_config_write_document(AppConfig *config, const AppDocument *document)
{
    const char *base = (const char *)document;
    for (size_t i = 0; i < COUNT_OF(BOOL_SETTINGS); i++)
        g_key_file_set_boolean(config->file, BOOL_SETTINGS[i].group, BOOL_SETTINGS[i].key,
                               *(const bool *)(base + BOOL_SETTINGS[i].offset));
    for (size_t i = 0; i < COUNT_OF(DOUBLE_SETTINGS); i++)
        g_key_file_set_double(config->file, DOUBLE_SETTINGS[i].group, DOUBLE_SETTINGS[i].key,
                              *(const double *)(base + DOUBLE_SETTINGS[i].offset));
    write_tile_defaults(config->file);
}

/* ---- печать и окно ------------------------------------------------------------- */

AppPrintPreferences app_config_read_print(AppConfig *config)
{
    GKeyFile *f = config->file;
    return (AppPrintPreferences){
        .tiles = read_bool(f, "print", "tiles", true), .clips = read_bool(f, "print", "svp", false),
        .dims = read_bool(f, "print", "dims", true), .cuts = read_bool(f, "print", "cuts", false),
        .calc = read_bool(f, "print", "calc", true), .mono = read_bool(f, "print", "mono", false),
        .expansion_joints = read_bool(f, "print", "expansion_joints", true),
        .landscape = read_bool(f, "print", "landscape", true), .scale = read_int(f, "print", "scale", 0)};
}

void app_config_write_print(AppConfig *config, const AppPrintPreferences *print)
{
    GKeyFile *f = config->file;
    g_key_file_set_boolean(f, "print", "tiles", print->tiles);
    g_key_file_set_boolean(f, "print", "svp", print->clips);
    g_key_file_set_boolean(f, "print", "dims", print->dims);
    g_key_file_set_boolean(f, "print", "cuts", print->cuts);
    g_key_file_set_boolean(f, "print", "calc", print->calc);
    g_key_file_set_boolean(f, "print", "mono", print->mono);
    g_key_file_set_boolean(f, "print", "expansion_joints", print->expansion_joints);
    g_key_file_set_boolean(f, "print", "landscape", print->landscape);
    g_key_file_set_integer(f, "print", "scale", print->scale);
}

void app_config_read_window(AppConfig *config, int *width, int *height, bool *maximized)
{
    *width = read_int(config->file, "window", "width", 0);
    *height = read_int(config->file, "window", "height", 0);
    *maximized = read_bool(config->file, "window", "maximized", false);
}

void app_config_write_window(AppConfig *config, int width, int height, bool maximized)
{
    g_key_file_set_integer(config->file, "window", "width", width);
    g_key_file_set_integer(config->file, "window", "height", height);
    g_key_file_set_boolean(config->file, "window", "maximized", maximized);
}

/* ---- недавние файлы ------------------------------------------------------------- */

char **app_config_recent_files(AppConfig *config)
{
    char **files = g_key_file_get_string_list(config->file, "files", "recent", NULL, NULL);
    return files ? files : g_new0(char *, 1);
}

static void write_recent(AppConfig *config, GPtrArray *files)
{
    g_key_file_set_string_list(config->file, "files", "recent", (const char *const *)files->pdata, files->len);
}

void app_config_add_recent(AppConfig *config, const char *path)
{
    char **old = app_config_recent_files(config);
    GPtrArray *files = g_ptr_array_new();
    g_ptr_array_add(files, (gpointer)path);
    for (char **p = old; *p && files->len < APP_RECENT_LIMIT; p++)
        if (strcmp(*p, path) != 0)
            g_ptr_array_add(files, *p);
    write_recent(config, files);
    char *folder = g_path_get_dirname(path);
    g_key_file_set_string(config->file, "files", "last_dir", folder);
    g_free(folder);
    g_ptr_array_free(files, TRUE);
    g_strfreev(old);
    app_config_save(config);
}

void app_config_remove_recent(AppConfig *config, const char *path)
{
    char **old = app_config_recent_files(config);
    GPtrArray *files = g_ptr_array_new();
    for (char **p = old; *p; p++)
        if (strcmp(*p, path) != 0)
            g_ptr_array_add(files, *p);
    write_recent(config, files);
    g_ptr_array_free(files, TRUE);
    g_strfreev(old);
    app_config_save(config);
}

void app_config_clear_recent(AppConfig *config)
{
    g_key_file_remove_key(config->file, "files", "recent", NULL);
    app_config_save(config);
}

char *app_config_last_directory(AppConfig *config)
{
    return g_key_file_get_string(config->file, "files", "last_dir", NULL);
}

/*
 * Настройки между запусками: файл tilelayout.ini в папке настроек
 * пользователя (Linux: ~/.config/TileLayout, Windows: %LOCALAPPDATA%\TileLayout).
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <glib.h>
#include <stdbool.h>

#include "app_document.h"

#define APP_RECENT_LIMIT 10

/* Настройки окна печати. */
typedef struct AppPrintPreferences {
    bool tiles, clips, dims, cuts, calc, mono;
    bool landscape;
    int scale;          /* 0 — по размеру листа, иначе 1:N */
} AppPrintPreferences;

typedef struct AppConfig {
    GKeyFile *file;
    char *path;
} AppConfig;

AppConfig *app_config_load(void);
void app_config_save(AppConfig *config);
void app_config_free(AppConfig *config);

void app_config_read_document(AppConfig *config, AppDocument *document);
void app_config_write_document(AppConfig *config, const AppDocument *document);

AppPrintPreferences app_config_read_print(AppConfig *config);
void app_config_write_print(AppConfig *config, const AppPrintPreferences *print);

/* Размер окна (0 — нет сохранённого). */
void app_config_read_window(AppConfig *config, int *width, int *height, bool *maximized);
void app_config_write_window(AppConfig *config, int width, int height, bool maximized);

/* Недавние файлы: новый — первым; возвращает список строк (g_strfreev). */
char **app_config_recent_files(AppConfig *config);
void app_config_add_recent(AppConfig *config, const char *path);
void app_config_remove_recent(AppConfig *config, const char *path);
void app_config_clear_recent(AppConfig *config);
char *app_config_last_directory(AppConfig *config);

#endif

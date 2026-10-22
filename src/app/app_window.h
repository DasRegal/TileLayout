/* Главное окно: меню, панель инструментов, канва, панель свойств, строка состояния. */
#ifndef APP_WINDOW_H
#define APP_WINDOW_H

#include <gtk/gtk.h>

#include "app_config.h"

typedef struct AppWindow AppWindow;

/* path — проект для открытия (может быть NULL — демо-проект). */
AppWindow *app_window_new(AppConfig *config, const char *path);
GtkWidget *app_window_widget(AppWindow *window);

/* Снимок всего окна в PNG (для документации и проверок). */
bool app_window_snapshot(AppWindow *window, const char *path);

#endif

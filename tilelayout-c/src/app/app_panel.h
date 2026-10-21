/* Панель свойств: комнаты, дом, стены, перегородки, проёмы, плитка, клей, СВП, расчёт. */
#ifndef APP_PANEL_H
#define APP_PANEL_H

#include <gtk/gtk.h>

#include "app_canvas.h"
#include "app_document.h"

/* Действия, которые выполняет окно (диалоги). */
typedef struct AppPanelActions {
    void (*add_house)(void *user_data);
    void (*print)(void *user_data);
    void *user_data;
} AppPanelActions;

typedef struct AppPanel AppPanel;

AppPanel *app_panel_new(AppDocument *document, AppCanvas *canvas, AppPanelActions actions);
GtkWidget *app_panel_widget(AppPanel *panel);

#endif

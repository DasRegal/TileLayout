/*
 * Вкладки окна: «План» (всегда) и вкладки помещений — развёртка стен комнаты. На развёртке:
 * щелчок — стена (её плитку двигают Ctrl + перетаскиванием), отверстие или линейка; инструменты
 * «Линейка» и «Труба» ставят их на стену; под стенами — пол у них.
 * Справа вверху вкладки помещения — 2D | 3D (3D — просмотр, позже). Вкладка
 * помещения выделяет свою комнату — панель справа показывает её настройки и стены.
 * Комната узнаётся по id: после правки и отмены объекты комнат — новые.
 */
#ifndef APP_ROOM_TABS_H
#define APP_ROOM_TABS_H

#include <gtk/gtk.h>

#include "app_canvas.h"
#include "app_document.h"

typedef struct AppRoomTabs AppRoomTabs;

/* canvas — инструменты: «Линейка» и «Труба» работают и на развёртке, остальные переключают на план. */
AppRoomTabs *app_room_tabs_new(AppDocument *document, GtkWidget *plan, AppCanvas *canvas);
GtkWidget *app_room_tabs_widget(AppRoomTabs *tabs);
/* Открыть вкладку комнаты (или перейти на открытую). */
void app_room_tabs_open(AppRoomTabs *tabs, TlSpace *space);
/* Открыта вкладка помещения — его комната; иначе NULL. */
TlSpace *app_room_tabs_current(AppRoomTabs *tabs);
/* Сменился инструмент (вызывает окно). */
void app_room_tabs_tool_changed(AppRoomTabs *tabs, AppToolId tool);

#endif

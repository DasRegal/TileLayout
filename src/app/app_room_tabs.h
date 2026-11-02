/*
 * Страницы окна без ярлыков: план и одна открытая комната — развёртка её стен. На развёртке:
 * щелчок — стена (её плитку двигают Ctrl + перетаскиванием), отверстие или линейка; инструменты
 * «Линейка» и «Труба» ставят их на стену; под стенами — пол у них.
 * Справа вверху комнаты — плашка «Комната: План | Стены | 3D» (как на плане): 3D — комната в объёме
 * (tl_room3d.h), её вращают мышью. Открытая комната выделена — панель справа показывает её настройки и стены.
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
/* Открыть комнату (прежняя закрывается; та же — просто перейти к ней). */
void app_room_tabs_open(AppRoomTabs *tabs, TlSpace *space);
/* Вернуться на план. */
void app_room_tabs_show_plan(AppRoomTabs *tabs);
/* Открыта комната — она; на плане — NULL. */
TlSpace *app_room_tabs_current(AppRoomTabs *tabs);
/* Открытая комната — в 3D (volume) или развёрткой стен. */
void app_room_tabs_set_volume(AppRoomTabs *tabs, bool volume);
/* Сменился инструмент (вызывает окно). */
void app_room_tabs_tool_changed(AppRoomTabs *tabs, AppToolId tool);

#endif

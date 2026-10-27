/*
 * Выбор плитки из каталогов: список каталогов, плитки с рисунками, поиск по
 * названию и размеру; «Только цвет» — убрать рисунок.
 */
#ifndef APP_CATALOG_DIALOG_H
#define APP_CATALOG_DIALOG_H

#include <gtk/gtk.h>

#include "app_document.h"

/* Окно выбора для выделенной комнаты (у общего пола — для комнаты-источника):
   выбранная плитка задаёт ей размер, рисунок и цвет. */
void app_catalog_dialog_for_selection(GtkWindow *parent, AppDocument *document);
/* То же для зоны пола (бордюр, ковёр) выделенных комнат. */
void app_catalog_dialog_for_zone(GtkWindow *parent, AppDocument *document, TlZoneKind zone);

#endif

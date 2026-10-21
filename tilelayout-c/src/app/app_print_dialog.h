/* Окно предпросмотра печати: настройки, листы A4, сохранение в PDF, печать. */
#ifndef APP_PRINT_DIALOG_H
#define APP_PRINT_DIALOG_H

#include <gtk/gtk.h>

#include "app_config.h"
#include "app_document.h"

void app_print_dialog_run(GtkWindow *parent, AppDocument *document, AppConfig *config, const char *title);

#endif

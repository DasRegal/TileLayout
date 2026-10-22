/*
 * Выбор файла для открытия и сохранения. Диалоги системные: на Windows —
 * стандартное окно проводника, на Linux — окно GTK или портала рабочего стола.
 */
#ifndef APP_FILE_DIALOG_H
#define APP_FILE_DIALOG_H

#include <gtk/gtk.h>

/* Тип файлов в фильтре диалога. */
typedef struct AppFileType {
    const char *name;            /* «Проект TileLayout (*.tilelayout.json)» */
    const char *pattern;         /* «*.json» */
    const char *required_suffix; /* имя без этого окончания (без учёта регистра) ... */
    const char *added_suffix;    /* ... дополняется этим */
} AppFileType;

/* Путь выбранного файла (g_free) или NULL, если пользователь отказался.
   folder — начальная папка, может быть NULL. */
char *app_file_dialog_open(GtkWindow *parent, const char *title, const char *folder, const AppFileType *type);
char *app_file_dialog_save(GtkWindow *parent, const char *title, const char *folder, const char *suggested_name,
                           const AppFileType *type);

#endif

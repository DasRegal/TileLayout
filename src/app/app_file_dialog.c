#include "app_file_dialog.h"

#include <stdbool.h>

static GtkFileChooserNative *new_chooser(GtkWindow *parent, const char *title, GtkFileChooserAction action,
                                         const char *accept, const char *folder, const AppFileType *type)
{
    GtkFileChooserNative *chooser = gtk_file_chooser_native_new(title, parent, action, accept, "Отмена");
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, type->name);
    gchar **patterns = g_strsplit(type->pattern, ";", -1);
    for (gchar **pattern = patterns; *pattern; pattern++)
        gtk_file_filter_add_pattern(filter, *pattern);
    g_strfreev(patterns);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    if (folder)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), folder);
    return chooser;
}

static char *run_chooser(GtkFileChooserNative *chooser)
{
    char *path = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    g_object_unref(chooser);
    return path;
}

static char *with_suffix(char *path, const AppFileType *type)
{
    char *lower = g_ascii_strdown(path, -1);
    bool has_suffix = g_str_has_suffix(lower, type->required_suffix);
    g_free(lower);
    if (has_suffix)
        return path;
    char *completed = g_strconcat(path, type->added_suffix, NULL);
    g_free(path);
    return completed;
}

char *app_file_dialog_open(GtkWindow *parent, const char *title, const char *folder, const AppFileType *type)
{
    return run_chooser(new_chooser(parent, title, GTK_FILE_CHOOSER_ACTION_OPEN, "Открыть", folder, type));
}

char *app_file_dialog_save(GtkWindow *parent, const char *title, const char *folder, const char *suggested_name,
                           const AppFileType *type)
{
    GtkFileChooserNative *chooser = new_chooser(parent, title, GTK_FILE_CHOOSER_ACTION_SAVE, "Сохранить", folder, type);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), suggested_name);
    char *path = run_chooser(chooser);
    return path ? with_suffix(path, type) : NULL;
}

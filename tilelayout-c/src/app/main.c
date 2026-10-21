/*
 * TileLayout — раскладка плитки.
 *
 *   tilelayout [проект.tilelayout.json]
 *   tilelayout --export-png картинка.png [проект] [--numbers]
 *   tilelayout --export-pdf листы.pdf [проект] [--mono]
 *   tilelayout --screenshot окно.png [проект]      (снимок окна для проверки)
 */
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "app_config.h"
#include "app_export.h"
#include "app_print.h"
#include "app_window.h"

typedef struct CommandLine {
    const char *command;    /* NULL — обычный запуск */
    const char *output;
    const char *project;
    bool numbers, mono;
} CommandLine;

static CommandLine parse_command_line(int argc, char **argv)
{
    CommandLine line = {0};
    int index = 1;
    if (argc > 2 && g_str_has_prefix(argv[1], "--")) {
        line.command = argv[1];
        line.output = argv[2];
        index = 3;
    }
    for (; index < argc; index++) {
        if (strcmp(argv[index], "--numbers") == 0)
            line.numbers = true;
        else if (strcmp(argv[index], "--mono") == 0)
            line.mono = true;
        else
            line.project = argv[index];
    }
    return line;
}

/* Документ с проектом из файла (или демо-проектом); NULL — ошибка. */
static AppDocument *load_document(const char *path)
{
    AppDocument *document = app_document_new();
    char *error = NULL;
    if (path && !app_document_open(document, path, &error)) {
        fprintf(stderr, "Не удалось открыть %s: %s\n", path, error);
        g_free(error);
        app_document_free(document);
        return NULL;
    }
    return document;
}

static int export_png(const CommandLine *line)
{
    AppDocument *document = load_document(line->project);
    if (!document)
        return 1;
    AppRenderOptions options = app_render_screen_options();
    options.show_numbers = line->numbers;
    bool ok = app_export_png(document->project, line->output, 1400, 900, 1.0, &options);
    app_document_free(document);
    return ok ? 0 : 1;
}

static int export_pdf(const CommandLine *line)
{
    AppDocument *document = load_document(line->project);
    if (!document)
        return 1;
    AppPrintPreferences print = {true, true, true, true, true, line->mono, true, 0};
    AppPrintJob *job = app_print_job_new(document->project, "TileLayout", &print, 0, 0);
    printf("Страниц: %d\n", app_print_job_page_count(job));
    bool ok = app_print_job_save_pdf(job, line->output);
    app_print_job_free(job);
    app_document_free(document);
    return ok ? 0 : 1;
}

typedef struct Screenshot {
    AppWindow *window;
    const char *path;
} Screenshot;

static gboolean take_screenshot(gpointer data)
{
    Screenshot *shot = data;
    if (!app_window_snapshot(shot->window, shot->path))
        fprintf(stderr, "Не удалось сохранить снимок %s\n", shot->path);
    gtk_widget_destroy(app_window_widget(shot->window));
    return G_SOURCE_REMOVE;
}

static int run_gui(int argc, char **argv, const CommandLine *line)
{
    gtk_init(&argc, &argv);
    g_set_application_name("TileLayout");
    AppConfig *config = app_config_load();
    AppWindow *window = app_window_new(config, line->project);
    Screenshot shot = {window, line->output};
    if (line->command && strcmp(line->command, "--screenshot") == 0)
        g_timeout_add(1500, take_screenshot, &shot);
    gtk_main();
    app_config_free(config);
    return 0;
}

int main(int argc, char **argv)
{
    CommandLine line = parse_command_line(argc, argv);
    if (line.command && strcmp(line.command, "--export-png") == 0)
        return export_png(&line);
    if (line.command && strcmp(line.command, "--export-pdf") == 0)
        return export_pdf(&line);
    if (line.command && strcmp(line.command, "--screenshot") != 0) {
        fprintf(stderr, "Неизвестный ключ: %s\n", line.command);
        return 2;
    }
    return run_gui(argc, argv, &line);
}

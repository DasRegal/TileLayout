#include "app_print_dialog.h"

#include <math.h>

#include "app_icons.h"
#include "app_print.h"
#include "app_widgets.h"

#define PAGE_GAP 20.0

typedef struct PrintDialog {
    GtkWidget *dialog;
    GtkWidget *preview;
    AppDocument *document;
    AppConfig *config;
    char title[256];
    AppPrintPreferences options;
    double offset_x, offset_y;
    double zoom;
    AppPrintJob *job;
    bool updating;

    GtkWidget *tiles, *clips, *dims, *cuts, *calc, *mono;
    GtkWidget *orientation, *scale, *offset_x_spin, *offset_y_spin, *info;
} PrintDialog;

/* ---- задание и предпросмотр ------------------------------------------------- */

static void page_size_px(PrintDialog *self, double *width, double *height)
{
    double width_mm, height_mm;
    app_print_job_paper_mm(self->job, &width_mm, &height_mm);
    *width = width_mm * APP_PX_PER_MM;
    *height = height_mm * APP_PX_PER_MM;
}

static void resize_preview(PrintDialog *self)
{
    double width, height;
    page_size_px(self, &width, &height);
    int pages = app_print_job_page_count(self->job);
    gtk_widget_set_size_request(self->preview, (int)(width * self->zoom + 2 * PAGE_GAP),
                                (int)(pages * (height * self->zoom + PAGE_GAP) + PAGE_GAP));
}

static void update_info(PrintDialog *self)
{
    int scale, columns, rows;
    app_print_job_plan_info(self->job, &scale, &columns, &rows);
    char *text = columns ? g_strdup_printf("Масштаб 1:%d. Листов с планом: %d (%d × %d). Всего листов: %d.", scale,
                                           columns * rows, columns, rows, app_print_job_page_count(self->job))
                         : g_strdup("План пуст.");
    gtk_label_set_text(GTK_LABEL(self->info), text);
    g_free(text);
}

static void rebuild_job(PrintDialog *self)
{
    app_print_job_free(self->job);
    self->job = app_print_job_new(self->document->project, self->title, &self->options, self->offset_x, self->offset_y);
    update_info(self);
    resize_preview(self);
    gtk_widget_queue_draw(self->preview);
}

static gboolean on_preview_draw(GtkWidget *widget, cairo_t *cr, PrintDialog *self)
{
    double width, height;
    page_size_px(self, &width, &height);
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.63);
    cairo_paint(cr);
    double clip_top, clip_bottom, unused;
    cairo_clip_extents(cr, &unused, &clip_top, &unused, &clip_bottom);
    double x = (gtk_widget_get_allocated_width(widget) - width * self->zoom) / 2;
    for (int page = 0; page < app_print_job_page_count(self->job); page++) {
        double y = PAGE_GAP + page * (height * self->zoom + PAGE_GAP);
        if (y > clip_bottom || y + height * self->zoom < clip_top)
            continue; /* невидимые листы не рисуем */
        cairo_save(cr);
        cairo_rectangle(cr, x + 4, y + 4, width * self->zoom, height * self->zoom); /* тень */
        cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
        cairo_fill(cr);
        cairo_rectangle(cr, x, y, width * self->zoom, height * self->zoom);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill_preserve(cr);
        cairo_clip(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, self->zoom, self->zoom);
        app_print_job_render(self->job, cr, page);
        cairo_restore(cr);
    }
    return FALSE;
}

/* ---- настройки ---------------------------------------------------------------- */

static void read_controls(PrintDialog *self)
{
    self->options.tiles = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->tiles));
    self->options.clips = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->clips));
    self->options.dims = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->dims));
    self->options.cuts = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->cuts));
    self->options.calc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->calc));
    self->options.mono = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->mono));
    self->options.landscape = gtk_combo_box_get_active(GTK_COMBO_BOX(self->orientation)) == 0;
    int scale_index = gtk_combo_box_get_active(GTK_COMBO_BOX(self->scale));
    self->options.scale = scale_index <= 0 ? 0 : APP_PRINT_SCALES[scale_index - 1];
    self->offset_x = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->offset_x_spin));
    self->offset_y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->offset_y_spin));
}

static void on_control_changed(GtkWidget *widget, PrintDialog *self)
{
    if (self->updating)
        return;
    read_controls(self);
    rebuild_job(self);
}

static void step_scale(PrintDialog *self, int direction)
{
    int index = gtk_combo_box_get_active(GTK_COMBO_BOX(self->scale));
    if (index == 0) { /* из «по размеру листа» — к ближайшему стандартному */
        int scale, columns, rows;
        app_print_job_plan_info(self->job, &scale, &columns, &rows);
        index = APP_PRINT_SCALE_COUNT;
        for (int i = 0; i < APP_PRINT_SCALE_COUNT; i++)
            if (APP_PRINT_SCALES[i] >= scale) {
                index = i + 1;
                break;
            }
        index += direction > 0 ? 0 : -1;
    } else {
        index += direction;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->scale), CLAMP(index, 1, APP_PRINT_SCALE_COUNT));
}

static void on_smaller(GtkButton *button, PrintDialog *self) { step_scale(self, 1); }
static void on_larger(GtkButton *button, PrintDialog *self) { step_scale(self, -1); }

static void on_center(GtkButton *button, PrintDialog *self)
{
    self->updating = true;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->offset_x_spin), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->offset_y_spin), 0);
    self->updating = false;
    on_control_changed(NULL, self);
}

static void set_zoom(PrintDialog *self, double zoom)
{
    self->zoom = CLAMP(zoom, 0.1, 4.0);
    resize_preview(self);
    gtk_widget_queue_draw(self->preview);
}

static void on_zoom_in(GtkButton *button, PrintDialog *self) { set_zoom(self, self->zoom * 1.25); }
static void on_zoom_out(GtkButton *button, PrintDialog *self) { set_zoom(self, self->zoom / 1.25); }

static void on_zoom_fit(GtkButton *button, PrintDialog *self)
{
    double width, height;
    page_size_px(self, &width, &height);
    GtkWidget *scrolled = gtk_widget_get_parent(gtk_widget_get_parent(self->preview));
    double available_width = gtk_widget_get_allocated_width(scrolled) - 2 * PAGE_GAP;
    double available_height = gtk_widget_get_allocated_height(scrolled) - 2 * PAGE_GAP;
    set_zoom(self, fmin(available_width / width, available_height / height));
}

/* При открытии — лист целиком, когда размеры окна уже известны. */
static gboolean fit_on_open(gpointer data)
{
    on_zoom_fit(NULL, data);
    return G_SOURCE_REMOVE;
}

/* ---- вывод -------------------------------------------------------------------- */

static void on_save_pdf(GtkButton *button, PrintDialog *self)
{
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Сохранить PDF", GTK_WINDOW(self->dialog), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                     "Отмена", GTK_RESPONSE_CANCEL, "Сохранить", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "plan.pdf");
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (!g_str_has_suffix(path, ".pdf") && !g_str_has_suffix(path, ".PDF")) {
            char *with_extension = g_strconcat(path, ".pdf", NULL);
            g_free(path);
            path = with_extension;
        }
        bool ok = app_print_job_save_pdf(self->job, path);
        GtkWidget *message = gtk_message_dialog_new(GTK_WINDOW(self->dialog), GTK_DIALOG_MODAL,
                                                    ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                    ok ? "Сохранено:\n%s" : "Не удалось сохранить:\n%s", path);
        gtk_dialog_run(GTK_DIALOG(message));
        gtk_widget_destroy(message);
        g_free(path);
    }
    gtk_widget_destroy(chooser);
}

static void on_begin_print(GtkPrintOperation *operation, GtkPrintContext *context, PrintDialog *self)
{
    gtk_print_operation_set_n_pages(operation, app_print_job_page_count(self->job));
}

static void on_draw_page(GtkPrintOperation *operation, GtkPrintContext *context, int page, PrintDialog *self)
{
    cairo_t *cr = gtk_print_context_get_cairo_context(context);
    cairo_scale(cr, 72.0 / 96.0, 72.0 / 96.0); /* единицы печати — пункты */
    app_print_job_render(self->job, cr, page);
}

static void on_print(GtkButton *button, PrintDialog *self)
{
    GtkPrintOperation *operation = gtk_print_operation_new();
    GtkPageSetup *setup = gtk_page_setup_new();
    GtkPaperSize *a4 = gtk_paper_size_new(GTK_PAPER_NAME_A4);
    gtk_page_setup_set_paper_size(setup, a4);
    gtk_page_setup_set_orientation(setup, self->options.landscape ? GTK_PAGE_ORIENTATION_LANDSCAPE
                                                                  : GTK_PAGE_ORIENTATION_PORTRAIT);
    gtk_print_operation_set_default_page_setup(operation, setup);
    gtk_print_operation_set_use_full_page(operation, TRUE);
    gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
    gtk_print_operation_set_job_name(operation, self->title);
    g_signal_connect(operation, "begin-print", G_CALLBACK(on_begin_print), self);
    g_signal_connect(operation, "draw-page", G_CALLBACK(on_draw_page), self);
    gtk_print_operation_run(operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, GTK_WINDOW(self->dialog), NULL);
    gtk_paper_size_free(a4);
    g_object_unref(setup);
    g_object_unref(operation);
}

/* ---- окно --------------------------------------------------------------------- */

static GtkWidget *option_check(PrintDialog *self, GtkWidget *box, const char *text, bool active, const char *tip)
{
    GtkWidget *check = gtk_check_button_new_with_label(text);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), active);
    if (tip)
        gtk_widget_set_tooltip_text(check, tip);
    g_signal_connect(check, "toggled", G_CALLBACK(on_control_changed), self);
    gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
    return check;
}

static GtkWidget *build_controls(PrintDialog *self)
{
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(side, 330, -1);
    gtk_widget_set_hexpand(side, FALSE); /* иначе растяжение полей заберёт ширину у листов */
    AppSection shown = app_section_new(side, "Показать на плане");
    GtkWidget *checks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    app_section_wide(&shown, checks);
    self->tiles = option_check(self, checks, "Плитка", self->options.tiles, NULL);
    self->clips = option_check(self, checks, "СВП", self->options.clips, NULL);
    self->dims = option_check(self, checks, "Размеры", self->options.dims, NULL);
    self->cuts = option_check(self, checks, "Подрезки", self->options.cuts,
                              "С нового листа — план с номерами плиток, затем чертёж каждой подрезки со\n"
                              "всеми размерами. Плитка на две комнаты и две подрезки из одной плитки —\n"
                              "на одном чертеже");
    self->calc = option_check(self, checks, "Расчёты на отдельном листе", self->options.calc, NULL);
    self->mono = option_check(self, checks, "Чёрно-белая (без серого)", self->options.mono,
                              "Только чёрный и белый: стены и линии — чёрные, плитка — белая\n"
                              "с чёрными швами. Для лазерных принтеров и ксерокопий.");

    AppSection paper = app_section_new(side, "Лист A4 и масштаб");
    self->orientation = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->orientation), "Альбомная");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->orientation), "Книжная");
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->orientation), self->options.landscape ? 0 : 1);
    g_signal_connect(self->orientation, "changed", G_CALLBACK(on_control_changed), self);
    app_section_row(&paper, "Ориентация", self->orientation);
    self->scale = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->scale), "По размеру листа");
    int active_scale = 0;
    for (int i = 0; i < APP_PRINT_SCALE_COUNT; i++) {
        char text[16];
        g_snprintf(text, sizeof text, "1:%d", APP_PRINT_SCALES[i]);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(self->scale), text);
        if (APP_PRINT_SCALES[i] == self->options.scale)
            active_scale = i + 1;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->scale), active_scale);
    g_signal_connect(self->scale, "changed", G_CALLBACK(on_control_changed), self);
    app_section_row(&paper, "Масштаб", self->scale);
    GtkWidget *steps = app_hbox(4);
    GtkWidget *smaller = gtk_button_new_with_label("−");
    gtk_widget_set_tooltip_text(smaller, "Мельче (следующий масштаб)");
    g_signal_connect(smaller, "clicked", G_CALLBACK(on_smaller), self);
    GtkWidget *larger = gtk_button_new_with_label("+");
    gtk_widget_set_tooltip_text(larger, "Крупнее (предыдущий масштаб)");
    g_signal_connect(larger, "clicked", G_CALLBACK(on_larger), self);
    gtk_box_pack_start(GTK_BOX(steps), smaller, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(steps), larger, TRUE, TRUE, 0);
    app_section_row(&paper, "", steps);
    self->offset_x_spin = app_unit_spin_new(-500, 500, 5, 0, "мм");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->offset_x_spin), 0);
    g_signal_connect(self->offset_x_spin, "value-changed", G_CALLBACK(on_control_changed), self);
    app_section_row(&paper, "Сдвиг вправо", self->offset_x_spin);
    self->offset_y_spin = app_unit_spin_new(-500, 500, 5, 0, "мм");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->offset_y_spin), 0);
    g_signal_connect(self->offset_y_spin, "value-changed", G_CALLBACK(on_control_changed), self);
    app_section_row(&paper, "Сдвиг вниз", self->offset_y_spin);
    GtkWidget *center = app_icon_button_new("fit", "Отцентровать план");
    g_signal_connect(center, "clicked", G_CALLBACK(on_center), self);
    app_section_wide(&paper, center);
    self->info = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(self->info), TRUE);
    gtk_label_set_xalign(GTK_LABEL(self->info), 0);
    app_section_wide(&paper, self->info);

    AppSection view = app_section_new(side, "Просмотр");
    GtkWidget *zoom = app_hbox(4);
    const struct {
        const char *text;
        GCallback callback;
    } ZOOM_BUTTONS[] = {{"+", G_CALLBACK(on_zoom_in)}, {"−", G_CALLBACK(on_zoom_out)}, {"Лист целиком", G_CALLBACK(on_zoom_fit)}};
    for (size_t i = 0; i < G_N_ELEMENTS(ZOOM_BUTTONS); i++) {
        GtkWidget *button = gtk_button_new_with_label(ZOOM_BUTTONS[i].text);
        g_signal_connect(button, "clicked", ZOOM_BUTTONS[i].callback, self);
        gtk_box_pack_start(GTK_BOX(zoom), button, TRUE, TRUE, 0);
    }
    app_section_wide(&view, zoom);

    GtkWidget *save = gtk_button_new_with_label("Сохранить PDF…");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_pdf), self);
    GtkWidget *print = app_icon_button_new("print", "Печать…");
    g_signal_connect(print, "clicked", G_CALLBACK(on_print), self);
    gtk_box_pack_end(GTK_BOX(side), print, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(side), save, FALSE, FALSE, 0);
    return side;
}

void app_print_dialog_run(GtkWindow *parent, AppDocument *document, AppConfig *config, const char *title)
{
    PrintDialog self = {.document = document, .config = config, .zoom = 0.6};
    g_strlcpy(self.title, title, sizeof self.title);
    self.options = app_config_read_print(config);
    self.dialog = gtk_dialog_new_with_buttons("Печать и PDF", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                              "Закрыть", GTK_RESPONSE_CLOSE, NULL);
    /* Обычное окно, а не «диалог»: Cinnamon/Mutter прикрепляют модальные диалоги
       к родителю, и их нельзя ни растянуть, ни развернуть. */
    gtk_window_set_type_hint(GTK_WINDOW(self.dialog), GDK_WINDOW_TYPE_HINT_NORMAL);
    gtk_window_set_resizable(GTK_WINDOW(self.dialog), TRUE);
    int parent_width = 1200, parent_height = 840;
    if (parent)
        gtk_window_get_size(parent, &parent_width, &parent_height);
    gtk_window_set_default_size(GTK_WINDOW(self.dialog), MAX(900, parent_width * 9 / 10), MAX(640, parent_height * 9 / 10));
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(self.dialog));
    GtkWidget *row = app_hbox(8);
    gtk_container_set_border_width(GTK_CONTAINER(row), 6);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    self.preview = gtk_drawing_area_new();
    g_signal_connect(self.preview, "draw", G_CALLBACK(on_preview_draw), &self);
    gtk_container_add(GTK_CONTAINER(scrolled), self.preview);
    gtk_box_pack_start(GTK_BOX(row), scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), build_controls(&self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), row, TRUE, TRUE, 0);
    rebuild_job(&self);
    gtk_widget_show_all(self.dialog);
    g_idle_add(fit_on_open, &self);
    gtk_dialog_run(GTK_DIALOG(self.dialog));
    read_controls(&self);
    app_config_write_print(config, &self.options);
    app_config_save(config);
    gtk_widget_destroy(self.dialog);
    app_print_job_free(self.job);
}

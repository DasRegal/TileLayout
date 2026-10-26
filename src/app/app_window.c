#include "app_window.h"

#include <string.h>

#include "app_canvas.h"
#include "app_commands.h"
#include "app_export.h"
#include "app_file_dialog.h"
#include "app_icons.h"
#include "app_panel.h"
#include "app_print_dialog.h"
#include "app_widgets.h"

#define APP_NAME "TileLayout"
#define PROJECT_EXTENSION ".tilelayout.json"

/* Справка по разделам: одна строка длиннее 4095 символов не гарантируется стандартом C99. */
static const char *const HELP_SECTIONS[] = {
    "<b>Как устроен план</b>\n"
    "• <b>Контур</b> — дом или квартира: замкнутая цепочка наружных стен. Рисуется прямоугольником (R) "
    "или по точкам (P) <i>на пустом месте</i>.\n"
    "• <b>Перегородки</b> (W) ставятся внутри контура. Как только они замыкают область, появляется "
    "<b>комната</b> со своим полом, плиткой и расчётом. Удалили перегородку — комнаты сливаются.\n"
    "• Прямоугольник или контур <i>внутри</i> дома сразу становится комнатой из перегородок.\n"
    "• Перегородки привязаны к стенам; концы, соединённые в узел, движутся вместе.\n\n",
    "<b>Мышь</b>\n"
    "• Клик — выделить; тянуть угол, стену, перегородку (за тело — поперёк, за конец — вдоль или с поворотом), проём.\n"
    "• Тянуть комнату — переместить весь дом. Двойной клик по наружной стене — добавить угол.\n"
    "• Ctrl + тянуть (или «Сдвиг плитки», T) — сдвиг сетки плитки. Shift — без привязок.\n"
    "• Колесо — масштаб; правая/средняя кнопка или пустое место — панорама.\n"
    "• Стрелки — сдвиг выделенного на шаг привязки (Shift — 1 мм, Ctrl — плитка). Delete — удалить.\n\n",
    "<b>Рисование</b>: длину можно набрать на клавиатуре и нажать Enter.\n"
    "<b>Линейка</b> (M): клик — клик или протянуть мышью. Концы притягиваются к углам (□), серединам (△) "
    "и краям стен (○), к швам плитки и их пересечениям (◇); Shift или флажок «Примагничивать» на панели — "
    "без притягивания. Выделенную линейку можно тянуть за концы, двигать и удалить (Delete).\n"
    "<b>Автовыравнивание</b>: почти горизонтальные и вертикальные линейки, стены и перегородки выпрямляются, "
    "точка встаёт на одну линию с углами других стен — видна пунктирная линия. Отключается флажком на панели, "
    "Shift — на время.\n"
    "<b>Перегородка</b>: конец тянется вдоль оси, а если увести в сторону — перегородка поворачивается; "
    "угол можно задать на панели. У выделенной перегородки квадратные маркеры на углах тела: их можно "
    "тянуть поперёк оси или задать отступ на панели — так задаётся точная форма для раскроя. Торец, "
    "упирающийся в стену, срезается точно по ней.\n",
    "<b>Узлы перегородок</b>: перегородки сходятся концами — до четырёх в одной точке; тела сходятся "
    "без щелей, «на ус». Середина узла (круг) двигает стены узла; углы (квадраты, видны у выделенного "
    "узла или его перегородки) меняют углы тел у узла, дальние остаются на месте. Если тянуть саму "
    "перегородку, узел идёт за ней. Delete на узле убирает его: самая прямая пара перегородок "
    "выпрямляется в одну, остальные упираются в её бок.\n"
    "<b>Примыкание</b>: перегородка, упёршаяся в бок другой, — та остаётся целой, в месте примыкания "
    "точка. Её тянут вдоль — конец скользит по боку; уводят за грань — та перегородка ломается в узле. "
    "Узел, вернувшийся на прямую, сам исчезает.\n"
    "<b>Координаты</b>: выделите точку узла или угол дома — на панели её X и Y.\n"
    "<b>Размеры от проёма</b> идут до ближайшего, что прерывает стену: грани примкнувшей перегородки, "
    "соседнего проёма или конца стены (у перегородки — с обеих сторон).\n",
    "<b>Пол под дверью</b>: проём в перегородке или в стене между квартирами делит между комнатами стык — "
    "коричневый пунктир. В наружной стене пол комнаты идёт до стыка, дальше улица; у нового проёма там "
    "пола нет — стык на внутренней грани стены, его можно вытянуть мышью. Щелчок по стыку выделяет его "
    "(тянется поперёк стены, притягивается к граням и середине), по остальному проёму — дверь. Точное "
    "положение стыка и шов под дверью (зазор у плитки) — на панели проёма; отступ плитки от стен у стыка "
    "не действует. Стык может идти наискосок: у выделенного стыка тяните его конец у косяка или задайте "
    "«Наклон» на панели.\n"
    "<b>Деформационный шов</b> (J): ставится, как линейка, концы притягиваются к поверхностям стен. "
    "Плитка в шов не заходит: шов режет её на куски с зазором между ними — они видны на плане с номерами и "
    "в чертежах подрезок. Зазор задаётся на панели. Кнопка «Швы» прячет и показывает швы на плане.\n",
    "<b>Esc</b> — отменить построение, повторный Esc — режим «Выбор».\n"
    "<b>Блокировка</b>: флажок у стены, перегородки и проёма — объект нельзя сдвинуть или удалить.\n\n"
    "<b>Пол</b>: своя раскладка или «как в комнате X» — общая сетка, плитка на стыке — одна.\n"
    "<b>Клей</b>: расход на 1 мм × толщина слоя × площадь. <b>СВП</b> — только в швах между плитками.\n"
    "<b>Мин. ширина подрезки</b>: более узкие куски подсвечиваются красным.",
    NULL};

typedef struct ToolEntry {
    AppToolId tool;
    const char *icon, *label, *tip;
} ToolEntry;

static const ToolEntry TOOL_ENTRIES[] = {
    {APP_TOOL_SELECT, "select", "Выбор", "Выбор и правка (V)"},
    {APP_TOOL_RECT, "rect", "Прямоугольник", "Дом прямоугольником, а внутри дома — комната (R)"},
    {APP_TOOL_CONTOUR, "contour", "Контур", "Дом по точкам, а внутри дома — комната (P)"},
    {APP_TOOL_PARTITION, "partition", "Перегородка", "Перегородка любой длины (W)"},
    {APP_TOOL_OPENING, "opening", "Проём", "Проём в стене или перегородке (O)"},
    {APP_TOOL_TILES, "tile_move", "Сдвиг плитки", "Сдвинуть сетку плитки мышью (T)"},
    {APP_TOOL_EXPANSION_JOINT, "expansion_joint", "Шов", "Деформационный шов в покрытии (J)"},
    {APP_TOOL_RULER, "ruler", "Линейка", "Линейка: измерить расстояние (M)"},
};

typedef struct ViewToggle {
    const char *icon, *label, *accelerator;
    size_t offset;       /* поле AppViewFlags */
    bool on_toolbar;
} ViewToggle;

static const ViewToggle VIEW_TOGGLES[] = {
    {"covering", "Покрытие", "<Control>1", offsetof(AppViewFlags, tiles), true},
    {"svp", "СВП", "<Control>2", offsetof(AppViewFlags, clips), true},
    {"numbers", "Номера", "<Control>3", offsetof(AppViewFlags, numbers), true},
    {"expansion_joint", "Швы", "<Control>4", offsetof(AppViewFlags, expansion_joints), true},
    {NULL, "Размеры", NULL, offsetof(AppViewFlags, dims), false},
    {NULL, "Сетка", NULL, offsetof(AppViewFlags, grid), false},
};

struct AppWindow {
    GtkWidget *window;
    AppDocument *document;
    AppConfig *config;
    AppCanvas *canvas;
    AppPanel *panel;
    GtkAccelGroup *accelerators;
    GtkWidget *status, *coordinates, *snap;
    GtkWidget *recent_menu;
    GtkToolItem *tool_buttons[APP_TOOL_COUNT];
    GtkWidget *tool_items[APP_TOOL_COUNT];
    GtkWidget *view_items[G_N_ELEMENTS(VIEW_TOGGLES)];
    GtkToolItem *view_buttons[G_N_ELEMENTS(VIEW_TOGGLES)];
    bool syncing;
};

/* ---- заголовок, состояние ------------------------------------------------- */

static void update_title(AppWindow *self)
{
    const char *name = self->document->path ? self->document->path : "Без имени";
    char *title = g_strdup_printf("%s%s — %s", self->document->dirty ? "*" : "", name, APP_NAME);
    gtk_window_set_title(GTK_WINDOW(self->window), title);
    g_free(title);
}

static void on_document_changed(AppDocument *document, unsigned changes, void *data)
{
    AppWindow *self = data;
    if (changes & APP_CHANGED_FILE)
        update_title(self);
    if (changes & APP_CHANGED_STATUS)
        gtk_label_set_text(GTK_LABEL(self->status), document->status);
}

static void on_pointer(TlVec2 world, void *data)
{
    AppWindow *self = data;
    char text[64];
    g_snprintf(text, sizeof text, "X: %.0f   Y: %.0f мм", world.x, world.y);
    gtk_label_set_text(GTK_LABEL(self->coordinates), text);
}

static void message(AppWindow *self, GtkMessageType type, const char *text)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(self->window), GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", text);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* ---- файлы ------------------------------------------------------------------ */

static char *project_title(AppWindow *self)
{
    if (!self->document->path)
        return g_strdup(APP_NAME);
    char *name = g_path_get_basename(self->document->path);
    const char *extensions[] = {PROJECT_EXTENSION, ".tileplan.json", ".json"};
    for (size_t i = 0; i < G_N_ELEMENTS(extensions); i++)
        if (g_str_has_suffix(name, extensions[i])) {
            name[strlen(name) - strlen(extensions[i])] = '\0';
            break;
        }
    char *title = g_strdup_printf("%s — %s", APP_NAME, name);
    g_free(name);
    return title;
}

static const AppFileType PROJECT_FILES = {"Проект TileLayout (*" PROJECT_EXTENSION ", *.tileplan.json, *.json)",
                                           "*.json", ".json", PROJECT_EXTENSION};
static const AppFileType PNG_FILES = {"Изображение PNG (*.png)", "*.png", ".png", ".png"};

static bool save_to(AppWindow *self, const char *path)
{
    char *error = NULL;
    if (!app_document_save(self->document, path, &error)) {
        char *text = g_strdup_printf("Не удалось сохранить:\n%s", error);
        message(self, GTK_MESSAGE_ERROR, text);
        g_free(text);
        g_free(error);
        return false;
    }
    app_config_add_recent(self->config, path);
    app_document_status(self->document, "Сохранено: %s", path);
    return true;
}

static bool save_as(AppWindow *self)
{
    char *folder = app_config_last_directory(self->config);
    char *path = app_file_dialog_save(GTK_WINDOW(self->window), "Сохранить проект", folder, "plan" PROJECT_EXTENSION,
                                      &PROJECT_FILES);
    bool saved = path && save_to(self, path);
    g_free(path);
    g_free(folder);
    return saved;
}

static bool save(AppWindow *self)
{
    return self->document->path ? save_to(self, self->document->path) : save_as(self);
}

/* Несохранённые изменения: true — можно продолжать. */
static bool confirm_discard(AppWindow *self)
{
    if (!self->document->dirty)
        return true;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(self->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE, "Сохранить изменения?");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Не сохранять", GTK_RESPONSE_NO, "Отмена", GTK_RESPONSE_CANCEL,
                           "Сохранить", GTK_RESPONSE_YES, NULL);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_YES)
        return save(self);
    return response == GTK_RESPONSE_NO;
}

static void rebuild_recent_menu(AppWindow *self);

static void open_path(AppWindow *self, const char *path)
{
    char *error = NULL;
    if (!app_document_open(self->document, path, &error)) {
        char *text = g_strdup_printf("Не удалось открыть файл:\n%s", error);
        message(self, GTK_MESSAGE_ERROR, text);
        g_free(text);
        g_free(error);
        return;
    }
    app_config_add_recent(self->config, path);
    rebuild_recent_menu(self);
    app_canvas_fit(self->canvas);
}

static void on_new(GtkMenuItem *item, AppWindow *self)
{
    if (confirm_discard(self))
        app_document_reset(self->document, tl_project_new(), NULL);
}

static void on_open(GtkMenuItem *item, AppWindow *self)
{
    if (!confirm_discard(self))
        return;
    char *folder = app_config_last_directory(self->config);
    char *path = app_file_dialog_open(GTK_WINDOW(self->window), "Открыть проект", folder, &PROJECT_FILES);
    if (path)
        open_path(self, path);
    g_free(path);
    g_free(folder);
}

static void on_save(GtkMenuItem *item, AppWindow *self)
{
    save(self);
    rebuild_recent_menu(self);
}

static void on_save_as(GtkMenuItem *item, AppWindow *self)
{
    save_as(self);
    rebuild_recent_menu(self);
}

static void on_recent(GtkMenuItem *item, AppWindow *self)
{
    const char *path = g_object_get_data(G_OBJECT(item), "path");
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        char *text = g_strdup_printf("Файл не найден:\n%s", path);
        message(self, GTK_MESSAGE_WARNING, text);
        g_free(text);
        app_config_remove_recent(self->config, path);
        rebuild_recent_menu(self);
        return;
    }
    if (confirm_discard(self))
        open_path(self, path);
}

static void on_clear_recent(GtkMenuItem *item, AppWindow *self)
{
    app_config_clear_recent(self->config);
    rebuild_recent_menu(self);
}

static void rebuild_recent_menu(AppWindow *self)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(self->recent_menu));
    for (GList *child = children; child; child = child->next)
        gtk_widget_destroy(child->data);
    g_list_free(children);
    char **files = app_config_recent_files(self->config);
    if (!files[0]) {
        GtkWidget *empty = gtk_menu_item_new_with_label("(список пуст)");
        gtk_widget_set_sensitive(empty, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(self->recent_menu), empty);
    }
    for (int i = 0; files[i]; i++) {
        char *name = g_path_get_basename(files[i]);
        char *folder = g_path_get_dirname(files[i]);
        char *label = g_strdup_printf("_%d. %s   — %s", (i + 1) % 10, name, folder);
        GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
        g_object_set_data_full(G_OBJECT(item), "path", g_strdup(files[i]), g_free);
        gtk_widget_set_sensitive(item, g_file_test(files[i], G_FILE_TEST_EXISTS));
        g_signal_connect(item, "activate", G_CALLBACK(on_recent), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(self->recent_menu), item);
        g_free(name);
        g_free(folder);
        g_free(label);
    }
    if (files[0]) {
        gtk_menu_shell_append(GTK_MENU_SHELL(self->recent_menu), gtk_separator_menu_item_new());
        GtkWidget *clear = gtk_menu_item_new_with_label("Очистить список");
        g_signal_connect(clear, "activate", G_CALLBACK(on_clear_recent), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(self->recent_menu), clear);
    }
    g_strfreev(files);
    gtk_widget_show_all(self->recent_menu);
}

static void on_export_png(GtkMenuItem *item, AppWindow *self)
{
    char *path = app_file_dialog_save(GTK_WINDOW(self->window), "Экспорт в PNG", NULL, "plan.png", &PNG_FILES);
    if (!path)
        return;
    GtkWidget *area = app_canvas_widget(self->canvas);
    AppRenderOptions options = app_render_screen_options();
    options.show_tiles = self->document->view.tiles;
    options.show_clips = self->document->view.clips;
    options.show_numbers = self->document->view.numbers;
    options.show_dims = self->document->view.dims;
    options.show_grid = false;
    if (app_export_png(self->document->project, path, gtk_widget_get_allocated_width(area),
                       gtk_widget_get_allocated_height(area), 2.0, &options))
        app_document_status(self->document, "Сохранено изображение: %s", path);
    g_free(path);
}

static void open_print_dialog(void *data)
{
    AppWindow *self = data;
    char *title = project_title(self);
    app_print_dialog_run(GTK_WINDOW(self->window), self->document, self->config, title);
    g_free(title);
}

static void on_print(GtkMenuItem *item, AppWindow *self) { open_print_dialog(self); }
static void on_quit(GtkMenuItem *item, AppWindow *self) { gtk_window_close(GTK_WINDOW(self->window)); }

/* ---- правка ------------------------------------------------------------------ */

static void on_undo(GtkWidget *widget, AppWindow *self) { app_document_undo(self->document); }
static void on_redo(GtkWidget *widget, AppWindow *self) { app_document_redo(self->document); }
static void on_delete(GtkMenuItem *item, AppWindow *self) { app_command_delete_selection(self->document); }
static void on_delete_all_rulers(GtkMenuItem *item, AppWindow *self) { app_command_delete_all_rulers(self->document); }

static GtkWidget *add_spin_row(GtkWidget *grid, int row, const char *label, double value)
{
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    GtkWidget *spin = app_unit_spin_new(0, 100000, 100, 0, "мм");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_grid_attach(GTK_GRID(grid), caption, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, row, 1, 1);
    return spin;
}

/* Новый дом справа от существующих. */
static void add_house_dialog(void *data)
{
    AppWindow *self = data;
    AppEditPreferences *preferences = &self->document->preferences;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Новый дом / квартира", GTK_WINDOW(self->window), GTK_DIALOG_MODAL,
                                                    "Отмена", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, NULL);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    GtkWidget *width = add_spin_row(grid, 0, "Ширина (внутр.)", preferences->house_width);
    GtkWidget *height = add_spin_row(grid, 1, "Длина (внутр.)", preferences->house_height);
    GtkWidget *thickness = add_spin_row(grid, 2, "Толщина стен", preferences->house_thickness);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        preferences->house_width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(width));
        preferences->house_height = gtk_spin_button_get_value(GTK_SPIN_BUTTON(height));
        preferences->house_thickness = gtk_spin_button_get_value(GTK_SPIN_BUTTON(thickness));
        double x = 0;
        TlRect bounds;
        if (app_render_plan_bounds(self->document->project, &bounds))
            x = bounds.right + 1500;
        app_command_add_house(self->document, tl_vec2(x, 0), preferences->house_width, preferences->house_height,
                              preferences->house_thickness);
        app_canvas_fit(self->canvas);
    }
    gtk_widget_destroy(dialog);
}

static void on_add_house(GtkMenuItem *item, AppWindow *self) { add_house_dialog(self); }

/* ---- инструменты и вид -------------------------------------------------------- */

static void sync_tool(AppToolId tool, void *data)
{
    AppWindow *self = data;
    self->syncing = true;
    for (int i = 0; i < APP_TOOL_COUNT; i++) {
        gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(self->tool_buttons[i]), i == (int)tool);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->tool_items[i]), i == (int)tool);
    }
    self->syncing = false;
}

/* Кнопка панели или пункт меню нажаты. */
static bool toggle_active(GtkWidget *widget)
{
    return GTK_IS_TOGGLE_TOOL_BUTTON(widget) ? gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget))
                                             : gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
}

static void on_tool_toggled(GtkWidget *widget, AppWindow *self)
{
    /* Радиогруппа шлёт «toggled» и отжатой кнопке — её пропускаем. */
    if (self->syncing || !toggle_active(widget))
        return;
    AppToolId tool = (AppToolId)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tool"));
    app_canvas_set_tool(self->canvas, tool);
}

static bool *view_flag(AppWindow *self, size_t index)
{
    return (bool *)((char *)&self->document->view + VIEW_TOGGLES[index].offset);
}

static void on_view_toggled(GtkWidget *widget, AppWindow *self)
{
    if (self->syncing)
        return;
    size_t index = (size_t)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "view"));
    bool active = toggle_active(widget);
    *view_flag(self, index) = active;
    self->syncing = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->view_items[index]), active);
    if (self->view_buttons[index])
        gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(self->view_buttons[index]), active);
    self->syncing = false;
    app_document_notify(self->document, APP_CHANGED_VIEW);
}

static void on_fit(GtkWidget *widget, AppWindow *self) { app_canvas_fit(self->canvas); }
static void on_zoom_in(GtkMenuItem *item, AppWindow *self) { app_canvas_zoom(self->canvas, 1.25); }
static void on_zoom_out(GtkMenuItem *item, AppWindow *self) { app_canvas_zoom(self->canvas, 1 / 1.25); }
static void on_escape(GtkMenuItem *item, AppWindow *self) { app_canvas_escape(self->canvas); }

static void on_snap_changed(GtkComboBox *combo, AppWindow *self)
{
    const char *id = gtk_combo_box_get_active_id(combo);
    if (id)
        self->document->preferences.snap_step = g_ascii_strtod(id, NULL);
}

/* ---- справка ------------------------------------------------------------------- */

#define HELP_WIDTH 640
#define HELP_HEIGHT 560

/* Справка — с разметкой (жирные заголовки) и прокруткой: целиком она выше экрана. */
static void on_help(GtkMenuItem *item, AppWindow *self)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Управление", GTK_WINDOW(self->window), GTK_DIALOG_MODAL,
                                                    "Закрыть", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), HELP_WIDTH, HELP_HEIGHT);
    char *help = g_strjoinv("", (char **)HELP_SECTIONS);
    GtkWidget *text = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(text), help);
    gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
    gtk_label_set_xalign(GTK_LABEL(text), 0);
    gtk_label_set_yalign(GTK_LABEL(text), 0);
    g_object_set(text, "margin", 16, NULL);
    g_free(help);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), text);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_about(GtkMenuItem *item, AppWindow *self)
{
    char *runtime = g_strdup_printf("Раскладка плитки: план дома, стены, перегородки, проёмы, раскладка и расчёт "
                                    "плитки, клея и СВП.\n\nФайлы проектов: *%s\nGTK %d.%d.%d",
                                    PROJECT_EXTENSION, gtk_get_major_version(), gtk_get_minor_version(),
                                    gtk_get_micro_version());
    gtk_show_about_dialog(GTK_WINDOW(self->window), "program-name", APP_NAME, "version", TL_VERSION, "comments",
                          runtime, "logo", app_icon_pixbuf("app", 64), "title", "О программе", NULL);
    g_free(runtime);
}

/* ---- построение ------------------------------------------------------------------ */

static GtkWidget *menu_item(AppWindow *self, GtkWidget *menu, const char *label, const char *accelerator,
                            GCallback callback)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    if (accelerator) {
        guint key;
        GdkModifierType modifiers;
        gtk_accelerator_parse(accelerator, &key, &modifiers);
        gtk_widget_add_accelerator(item, "activate", self->accelerators, key, modifiers, GTK_ACCEL_VISIBLE);
    }
    g_signal_connect(item, "activate", callback, self);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

static GtkWidget *submenu(GtkWidget *bar, const char *label)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    return menu;
}

static void separator(GtkWidget *menu) { gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new()); }

static GtkWidget *build_menu(AppWindow *self)
{
    GtkWidget *bar = gtk_menu_bar_new();
    GtkWidget *file = submenu(bar, "_Файл");
    menu_item(self, file, "Новый проект", "<Control>n", G_CALLBACK(on_new));
    menu_item(self, file, "Открыть…", "<Control>o", G_CALLBACK(on_open));
    GtkWidget *recent = gtk_menu_item_new_with_label("Недавние файлы");
    self->recent_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), self->recent_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(file), recent);
    menu_item(self, file, "Сохранить", "<Control>s", G_CALLBACK(on_save));
    menu_item(self, file, "Сохранить как…", "<Control><Shift>s", G_CALLBACK(on_save_as));
    separator(file);
    menu_item(self, file, "Печать / PDF…", "<Control>p", G_CALLBACK(on_print));
    menu_item(self, file, "Экспорт в PNG…", "<Control>e", G_CALLBACK(on_export_png));
    separator(file);
    menu_item(self, file, "Выход", "<Control>q", G_CALLBACK(on_quit));

    GtkWidget *edit = submenu(bar, "_Правка");
    menu_item(self, edit, "Отменить", "<Control>z", G_CALLBACK(on_undo));
    GtkWidget *redo = menu_item(self, edit, "Повторить", "<Control><Shift>z", G_CALLBACK(on_redo));
    guint key;
    GdkModifierType modifiers;
    gtk_accelerator_parse("<Control>y", &key, &modifiers);
    gtk_widget_add_accelerator(redo, "activate", self->accelerators, key, modifiers, 0);
    separator(edit);
    menu_item(self, edit, "Удалить выделенное", NULL, G_CALLBACK(on_delete));
    menu_item(self, edit, "Новый дом / квартира по размерам…", NULL, G_CALLBACK(on_add_house));
    menu_item(self, edit, "Удалить все линейки", NULL, G_CALLBACK(on_delete_all_rulers));

    GtkWidget *tools = submenu(bar, "_Инструменты");
    GSList *group = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(TOOL_ENTRIES); i++) {
        GtkWidget *item = gtk_radio_menu_item_new_with_label(group, TOOL_ENTRIES[i].tip);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        g_object_set_data(G_OBJECT(item), "tool", GINT_TO_POINTER(TOOL_ENTRIES[i].tool));
        g_signal_connect(item, "toggled", G_CALLBACK(on_tool_toggled), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(tools), item);
        self->tool_items[TOOL_ENTRIES[i].tool] = item;
    }
    separator(tools);
    menu_item(self, tools, "Отмена / выход из инструмента (Esc)", NULL, G_CALLBACK(on_escape));

    GtkWidget *view = submenu(bar, "_Вид");
    menu_item(self, view, "Вписать (F)", NULL, G_CALLBACK(on_fit));
    menu_item(self, view, "Приблизить", "<Control>equal", G_CALLBACK(on_zoom_in));
    menu_item(self, view, "Отдалить", "<Control>minus", G_CALLBACK(on_zoom_out));
    separator(view);
    for (size_t i = 0; i < G_N_ELEMENTS(VIEW_TOGGLES); i++) {
        GtkWidget *item = gtk_check_menu_item_new_with_label(VIEW_TOGGLES[i].label);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), *view_flag(self, i));
        g_object_set_data(G_OBJECT(item), "view", GINT_TO_POINTER(i));
        if (VIEW_TOGGLES[i].accelerator) {
            gtk_accelerator_parse(VIEW_TOGGLES[i].accelerator, &key, &modifiers);
            gtk_widget_add_accelerator(item, "activate", self->accelerators, key, modifiers, GTK_ACCEL_VISIBLE);
        }
        g_signal_connect(item, "toggled", G_CALLBACK(on_view_toggled), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(view), item);
        self->view_items[i] = item;
    }

    GtkWidget *help = submenu(bar, "_Справка");
    menu_item(self, help, "Управление", "F1", G_CALLBACK(on_help));
    separator(help);
    menu_item(self, help, "О программе " APP_NAME, NULL, G_CALLBACK(on_about));
    return bar;
}

static GtkToolItem *tool_button(GtkToolItem *item, const char *icon, const char *label, const char *tip)
{
    gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(item), gtk_image_new_from_pixbuf(app_icon_pixbuf(icon, 26)));
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), label);
    gtk_widget_set_tooltip_text(GTK_WIDGET(item), tip);
    gtk_tool_item_set_homogeneous(item, FALSE); /* ширина — по своей подписи, а не по самой длинной */
    return item;
}

/* Кнопки панели инструментов — с узкими полями по бокам, чтобы все уместились. */
static void compact_toolbar_buttons(GtkWidget *toolbar)
{
    static const char CSS[] = "#main-toolbar toolbutton > button { padding-left: 5px; padding-right: 5px; }";
    gtk_widget_set_name(toolbar, "main-toolbar");
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(toolbar), GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static GtkWidget *build_toolbar(AppWindow *self)
{
    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH);
    compact_toolbar_buttons(toolbar);
    GtkToolItem *undo = tool_button(gtk_tool_button_new(NULL, NULL), "undo", "Отменить", "Отменить (Ctrl+Z)");
    g_signal_connect(undo, "clicked", G_CALLBACK(on_undo), self);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), undo, -1);
    GtkToolItem *redo = tool_button(gtk_tool_button_new(NULL, NULL), "redo", "Повторить", "Повторить (Ctrl+Shift+Z)");
    g_signal_connect(redo, "clicked", G_CALLBACK(on_redo), self);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), redo, -1);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);

    GSList *group = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(TOOL_ENTRIES); i++) {
        GtkToolItem *item = gtk_radio_tool_button_new(group);
        group = gtk_radio_tool_button_get_group(GTK_RADIO_TOOL_BUTTON(item));
        tool_button(item, TOOL_ENTRIES[i].icon, TOOL_ENTRIES[i].label, TOOL_ENTRIES[i].tip);
        g_object_set_data(G_OBJECT(item), "tool", GINT_TO_POINTER(TOOL_ENTRIES[i].tool));
        g_signal_connect(item, "toggled", G_CALLBACK(on_tool_toggled), self);
        gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
        self->tool_buttons[TOOL_ENTRIES[i].tool] = item;
    }
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);
    GtkToolItem *fit = tool_button(gtk_tool_button_new(NULL, NULL), "fit", "Вписать", "Показать весь план (F)");
    g_signal_connect(fit, "clicked", G_CALLBACK(on_fit), self);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), fit, -1);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);
    for (size_t i = 0; i < G_N_ELEMENTS(VIEW_TOGGLES); i++) {
        if (!VIEW_TOGGLES[i].on_toolbar)
            continue;
        GtkToolItem *item = gtk_toggle_tool_button_new();
        char *tip = g_strdup_printf("Показать / скрыть: %s", VIEW_TOGGLES[i].label);
        tool_button(item, VIEW_TOGGLES[i].icon, VIEW_TOGGLES[i].label, tip);
        g_free(tip);
        gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(item), *view_flag(self, i));
        g_object_set_data(G_OBJECT(item), "view", GINT_TO_POINTER(i));
        g_signal_connect(item, "toggled", G_CALLBACK(on_view_toggled), self);
        gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
        self->view_buttons[i] = item;
    }
    return toolbar;
}

static GtkWidget *build_statusbar(AppWindow *self)
{
    GtkWidget *bar = app_hbox(12);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 3);
    self->status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(self->status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(self->status), PANGO_ELLIPSIZE_END);
    self->coordinates = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bar), self->status, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), self->coordinates, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Привязка:"), FALSE, FALSE, 0);
    self->snap = gtk_combo_box_text_new();
    const int steps[] = {0, 1, 5, 10, 50, 100};
    for (size_t i = 0; i < G_N_ELEMENTS(steps); i++) {
        char id[8], text[16];
        g_snprintf(id, sizeof id, "%d", steps[i]);
        if (steps[i])
            g_snprintf(text, sizeof text, "%d мм", steps[i]);
        else
            g_strlcpy(text, "нет", sizeof text);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->snap), id, text);
    }
    char active[8];
    g_snprintf(active, sizeof active, "%.0f", self->document->preferences.snap_step);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->snap), active))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->snap), "10");
    gtk_widget_set_can_focus(self->snap, FALSE);
    g_signal_connect(self->snap, "changed", G_CALLBACK(on_snap_changed), self);
    gtk_box_pack_start(GTK_BOX(bar), self->snap, FALSE, FALSE, 0);
    return bar;
}

/* Esc — для всего окна (фокус может быть на кнопке панели). */
static gboolean on_window_key(GtkWidget *widget, GdkEventKey *event, AppWindow *self)
{
    if (event->keyval == GDK_KEY_Escape) {
        app_canvas_escape(self->canvas);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, AppWindow *self)
{
    if (!confirm_discard(self))
        return TRUE;
    int width, height;
    gtk_window_get_size(GTK_WINDOW(self->window), &width, &height);
    app_config_write_window(self->config, width, height, gtk_window_is_maximized(GTK_WINDOW(self->window)));
    app_config_write_document(self->config, self->document);
    app_config_save(self->config);
    return FALSE;
}

static void on_destroy(GtkWidget *widget, AppWindow *self)
{
    app_document_free(self->document);
    g_free(self);
    gtk_main_quit();
}

AppWindow *app_window_new(AppConfig *config, const char *path)
{
    AppWindow *self = g_new0(AppWindow, 1);
    self->config = config;
    self->document = app_document_new();
    app_config_read_document(config, self->document);
    self->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GList *icons = NULL;
    for (int size = 16; size <= 128; size *= 2)
        icons = g_list_append(icons, app_icon_pixbuf("app", size));
    gtk_window_set_icon_list(GTK_WINDOW(self->window), icons);
    g_list_free(icons);
    int width, height;
    bool maximized;
    app_config_read_window(config, &width, &height, &maximized);
    gtk_window_set_default_size(GTK_WINDOW(self->window), width > 0 ? width : 1400, height > 0 ? height : 860);
    if (maximized)
        gtk_window_maximize(GTK_WINDOW(self->window));
    self->accelerators = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(self->window), self->accelerators);

    self->canvas = app_canvas_new(self->document);
    AppPanelActions actions = {add_house_dialog, open_print_dialog, self};
    self->panel = app_panel_new(self->document, self->canvas, actions);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(layout), build_menu(self), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout), build_toolbar(self), FALSE, FALSE, 0);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(paned), app_canvas_widget(self->canvas), TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), app_panel_widget(self->panel), FALSE, FALSE);
    gtk_box_pack_start(GTK_BOX(layout), paned, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), build_statusbar(self), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(self->window), layout);

    app_document_add_listener(self->document, on_document_changed, self);
    app_canvas_on_tool_changed(self->canvas, sync_tool, self);
    app_canvas_on_pointer(self->canvas, on_pointer, self);
    g_signal_connect(self->window, "key-press-event", G_CALLBACK(on_window_key), self);
    g_signal_connect(self->window, "delete-event", G_CALLBACK(on_delete_event), self);
    g_signal_connect(self->window, "destroy", G_CALLBACK(on_destroy), self);
    rebuild_recent_menu(self);
    update_title(self);
    gtk_widget_show_all(self->window);
    app_document_notify(self->document, APP_CHANGED_MODEL | APP_CHANGED_SELECTION);
    if (path)
        open_path(self, path);
    return self;
}

GtkWidget *app_window_widget(AppWindow *window) { return window->window; }

bool app_window_snapshot(AppWindow *window, const char *path)
{
    GtkWidget *widget = window->window;
    int width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    gtk_widget_draw(widget, cr);
    cairo_destroy(cr);
    bool ok = cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surface);
    return ok;
}

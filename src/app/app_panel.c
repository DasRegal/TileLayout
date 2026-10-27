#include "app_panel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_calc_text.h"
#include "app_catalog_dialog.h"
#include "app_catalogs.h"
#include "app_commands.h"
#include "app_icons.h"
#include "app_widgets.h"
#include "tl_layout.h"
#include "tl_pattern.h"
#include "tl_report.h"

#define MAX_TILE_BINDINGS 48

#define MAX_ZONE_BINDINGS 32

/* Поле зоны пола: геометрия зоны ("enabled", "width", "height", "dx", "dy") или поле её плитки. */
typedef struct ZoneBinding {
    struct AppPanel *panel;
    TlZoneKind kind;
    const char *key;
    GtkWidget *widget;
} ZoneBinding;

/* Поле ввода, привязанное к полю настроек плитки (см. tl_tile_settings_fields). */
typedef struct TileBinding {
    struct AppPanel *panel;
    GtkWidget *widget;
    const TlTileField *field;
} TileBinding;

static const struct {
    const char *title;
    double value;
} ROW_SHIFTS[] = {{"Без сдвига", 0.0}, {"Вразбежку 1/2", 0.5}, {"Вразбежку 1/3", 1.0 / 3.0},
                  {"Вразбежку 1/4", 0.25}};

struct AppPanel {
    AppDocument *document;
    AppCanvas *canvas;
    AppPanelActions actions;
    GtkWidget *root;
    bool updating;

    GtkWidget *room_list;
    char *room_names;            /* для проверки, нужно ли перестраивать список */
    GtkWidget *partition_thickness, *opening_width, *expansion_joint_gap, *pipe_diameter;
    GtkWidget *link_walls, *measure_magnet, *auto_align;
    GtkWidget *hint;

    AppSection space_section;
    GtkWidget *space_name, *space_area, *space_perimeter, *floor_source;

    AppSection outline_section;
    GtkWidget *outline_area, *outline_thickness;

    AppSection wall_section;
    GtkWidget *wall_number, *wall_inner, *wall_outer, *wall_axis, *wall_thickness, *wall_angle;
    GtkWidget *keep_angles, *wall_lock;

    AppSection partition_section;
    GtkWidget *partition_length, *partition_own_thickness, *partition_angle, *partition_lock;
    GtkWidget *partition_corner_hint, *partition_corner_box;
    GtkWidget *partition_corner_name, *partition_corner_offset, *partition_corner_position;

    AppSection node_section;
    GtkWidget *node_point_name, *node_members, *node_x, *node_y;

    AppSection vertex_section;
    GtkWidget *vertex_x, *vertex_y;

    AppSection ruler_section;
    GtkWidget *ruler_name, *ruler_length, *ruler_count;

    AppSection expansion_joint_section;
    GtkWidget *expansion_joint_own_gap, *expansion_joint_length;

    AppSection pipe_section;
    GtkWidget *pipe_own_diameter, *pipe_x, *pipe_y;

    AppSection opening_section;
    GtkWidget *opening_name, *opening_host, *opening_own_width, *opening_from_start, *opening_to_end, *opening_lock;
    GtkWidget *threshold_box, *threshold_title, *threshold_depth_caption, *threshold_depth;
    GtkWidget *threshold_to_left, *threshold_middle, *threshold_to_right, *threshold_gap, *threshold_skew;

    AppSection tile_section, glue_section, clip_section, grout_section, price_section;
    GtkWidget *shared_note, *pattern, *pattern_note, *row_shift, *color_button, *color2_button, *texture_name;
    GtkWidget *corners, *centers, *lines; /* раскладка от угла, по центру, от линии — у каждой комнаты своя */
    GtkWidget *door_choice, *ruler_choice; /* двери и линейки комнаты по именам; id — номер в проекте */
    TileBinding tile_bindings[MAX_TILE_BINDINGS];
    size_t tile_binding_count;

    AppSection zone_sections[TL_ZONE_COUNT];
    GtkWidget *zone_texture[TL_ZONE_COUNT], *zone_size[TL_ZONE_COUNT]; /* рисунок из каталога; размер, как кладут */
    ZoneBinding zone_bindings[MAX_ZONE_BINDINGS];
    size_t zone_binding_count;

    GtkWidget *calc;
};

/* ---- общее ------------------------------------------------------------------ */

static TlSpace *selected_space(AppPanel *panel)
{
    return panel->document->selection.kind == APP_SELECT_SPACE ? panel->document->selection.space : NULL;
}

/* Выделенные комнаты (Shift+щелчок — несколько); поля плитки применяются ко всем. */
static size_t selected_spaces(AppPanel *panel, TlSpace **rooms)
{
    return app_selection_spaces(&panel->document->selection, rooms);
}

static double spin_value(GtkWidget *spin) { return gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)); }

static void set_spin(GtkWidget *spin, double value) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value); }

static void set_label(GtkWidget *label, const char *format, double value)
{
    char text[64];
    g_snprintf(text, sizeof text, format, value);
    gtk_label_set_text(GTK_LABEL(label), text);
}

static void set_check(GtkWidget *check, bool active)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), active);
}

static bool check_active(GtkWidget *check) { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)); }

static GtkWidget *lock_check(void)
{
    GtkWidget *check = gtk_check_button_new_with_label("Заблокировать от перемещения");
    gtk_button_set_image(GTK_BUTTON(check), gtk_image_new_from_pixbuf(app_icon_pixbuf("lock", 16)));
    gtk_button_set_always_show_image(GTK_BUTTON(check), TRUE);
    return check;
}

static GtkWidget *connect_button(GtkWidget *button, GCallback callback, AppPanel *panel)
{
    g_signal_connect(button, "clicked", callback, panel);
    return button;
}

/* ---- плитка: привязки полей --------------------------------------------------- */

static const TlTileField *find_field(const char *key)
{
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++)
        if (strcmp(fields[i].key, key) == 0)
            return &fields[i];
    return NULL;
}

static void commit_tile_field(AppPanel *panel, const TlTileField *field, const void *value)
{
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = selected_spaces(panel, rooms);
    if (panel->updating || count == 0)
        return;
    char key[48];
    g_snprintf(key, sizeof key, "tile_%s", field->key);
    app_document_checkpoint(panel->document, key);
    for (size_t i = 0; i < count; i++) {
        char *member = (char *)tl_space_effective_tile(rooms[i]) + field->offset;
        switch (field->type) {
        case TL_FIELD_BOOL: *(bool *)member = *(const bool *)value; break;
        case TL_FIELD_DOUBLE: *(double *)member = *(const double *)value; break;
        case TL_FIELD_INT: *(int *)member = (int)*(const double *)value; break;
        case TL_FIELD_COLOR:
        case TL_FIELD_TEXT: g_strlcpy(member, value, field->size); break;
        }
    }
    if (field->remembered_for_new_rooms)
        tl_tile_settings_remember_as_default(tl_space_effective_tile(rooms[0]));
    app_document_commit(panel->document);
}

static void on_tile_spin_changed(GtkSpinButton *spin, TileBinding *binding)
{
    double value = gtk_spin_button_get_value(spin);
    commit_tile_field(binding->panel, binding->field, &value);
}

static void on_tile_check_toggled(GtkToggleButton *check, TileBinding *binding)
{
    gtk_toggle_button_set_inconsistent(check, FALSE);
    bool value = gtk_toggle_button_get_active(check);
    commit_tile_field(binding->panel, binding->field, &value);
}

static TileBinding *bind_tile(AppPanel *panel, const char *key, GtkWidget *widget)
{
    TileBinding *binding = &panel->tile_bindings[panel->tile_binding_count++];
    *binding = (TileBinding){panel, widget, find_field(key)};
    if (GTK_IS_SPIN_BUTTON(widget))
        g_signal_connect(widget, "value-changed", G_CALLBACK(on_tile_spin_changed), binding);
    else
        g_signal_connect(widget, "toggled", G_CALLBACK(on_tile_check_toggled), binding);
    return binding;
}

static GtkWidget *tile_spin(AppPanel *panel, const char *key, double minimum, double maximum, double step,
                            int digits, const char *unit)
{
    GtkWidget *spin = app_unit_spin_new(minimum, maximum, step, digits, unit);
    bind_tile(panel, key, spin);
    return spin;
}

/* Поле одинаково у всех выделенных комнат? */
static bool field_same(const TlTileField *field, TlSpace *const *rooms, size_t count)
{
    bool text = field->type == TL_FIELD_COLOR || field->type == TL_FIELD_TEXT;
    const char *first = (const char *)tl_space_effective_tile(rooms[0]) + field->offset;
    for (size_t i = 1; i < count; i++) {
        const char *other = (const char *)tl_space_effective_tile(rooms[i]) + field->offset;
        if (text ? strcmp(first, other) != 0 : memcmp(first, other, field->size) != 0)
            return false;
    }
    return true;
}

/* Значения первой комнаты; у разных в выделенных комнатах поле пустое с подсказкой «разные»
   (флажок — в среднем положении): введённое значение получат все. */
static void refresh_tile_bindings(AppPanel *panel, TlSpace *const *rooms, size_t count)
{
    const TlTileSettings *tile = tl_space_effective_tile(rooms[0]);
    for (size_t i = 0; i < panel->tile_binding_count; i++) {
        const TileBinding *binding = &panel->tile_bindings[i];
        const char *member = (const char *)tile + binding->field->offset;
        bool mixed = !field_same(binding->field, rooms, count);
        switch (binding->field->type) {
        case TL_FIELD_BOOL:
            set_check(binding->widget, *(const bool *)member);
            gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(binding->widget), mixed);
            break;
        case TL_FIELD_DOUBLE:
        case TL_FIELD_INT:
            set_spin(binding->widget, binding->field->type == TL_FIELD_INT ? *(const int *)member
                                                                            : *(const double *)member);
            gtk_entry_set_placeholder_text(GTK_ENTRY(binding->widget), mixed ? "разные" : NULL);
            if (mixed)
                gtk_entry_set_text(GTK_ENTRY(binding->widget), "");
            break;
        default: break;
        }
    }
}

/* ---- плитка: действия ------------------------------------------------------------ */

static void set_offsets(AppPanel *panel, double x, double y)
{
    TlSpace *space = selected_space(panel);
    if (!space)
        return;
    app_document_checkpoint(panel->document, "");
    TlTileSettings *tile = tl_space_effective_tile(space);
    tl_layout_normalize_offsets(tile, &x, &y);
    tile->offset_x = round(x * 10) / 10;
    tile->offset_y = round(y * 10) / 10;
    app_document_commit(panel->document);
}

static void on_corner(GtkButton *button, AppPanel *panel)
{
    static const char *const NAMES[] = {"левый верхний", "правый верхний", "левый нижний", "правый нижний"};
    TlSpace *space = selected_space(panel);
    if (!space)
        return;
    TlLayoutCorner corner = (TlLayoutCorner)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "corner"));
    double x, y;
    tl_layout_corner_offsets(space, corner, &x, &y);
    set_offsets(panel, x, y);
    app_document_status(panel->document, "Раскладка от угла: %s", NAMES[corner]);
}

static void on_center(GtkButton *button, AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    if (!space)
        return;
    double x, y;
    tl_layout_center_offsets(space, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "joint")), &x, &y);
    set_offsets(panel, x, y);
}

static void on_optimize_narrow(GtkButton *button, AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    double x, y;
    if (!space)
        return;
    if (!tl_layout_optimize_narrow(space, &x, &y)) {
        app_document_status(panel->document, "Слишком много плиток для перебора");
        return;
    }
    set_offsets(panel, x, y);
    app_document_status(panel->document, "Смещение подобрано");
}

static void on_optimize_count(GtkButton *button, AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    TlTileCountResult result;
    if (!space)
        return;
    if (!tl_report_optimize_count(panel->document->project, space, &result)) {
        app_document_status(panel->document, "Слишком много плиток для перебора");
        return;
    }
    set_offsets(panel, result.offset_x, result.offset_y);
    if (result.after.tiles < result.before.tiles)
        app_document_status(panel->document, "Минимум плиток: было %d, стало %d (без запаса)",
                            result.before.tiles, result.after.tiles);
    else if (result.after.narrow < result.before.narrow)
        app_document_status(panel->document, "Плиток меньше не получится (%d), зато узких подрезок стало %d вместо %d",
                            result.after.tiles, result.after.narrow, result.before.narrow);
    else
        app_document_status(panel->document, "Текущая раскладка уже оптимальна: %d плиток (без запаса)",
                            result.after.tiles);
}

static void on_swap_tile(GtkButton *button, AppPanel *panel)
{
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = selected_spaces(panel, rooms);
    if (count == 0)
        return;
    app_document_checkpoint(panel->document, "");
    for (size_t i = 0; i < count; i++) {
        TlTileSettings *tile = tl_space_effective_tile(rooms[i]);
        if (i > 0 && tile == tl_space_effective_tile(rooms[0]))
            continue; /* общий пол — уже повёрнут */
        double width = tile->tile_width;
        tile->tile_width = tile->tile_height;
        tile->tile_height = width;
    }
    tl_tile_settings_remember_as_default(tl_space_effective_tile(rooms[0]));
    app_document_commit(panel->document);
}

static void on_row_shift(GtkComboBox *combo, AppPanel *panel)
{
    int index = gtk_combo_box_get_active(combo);
    if (index >= 0 && (size_t)index < G_N_ELEMENTS(ROW_SHIFTS))
        commit_tile_field(panel, find_field("shift"), &ROW_SHIFTS[index].value);
}

static void on_rotation_preset(GtkButton *button, AppPanel *panel)
{
    for (size_t i = 0; i < panel->tile_binding_count; i++)
        if (strcmp(panel->tile_bindings[i].field->key, "angle") == 0)
            set_spin(panel->tile_bindings[i].widget, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "angle")));
}

static void on_choose_texture(GtkButton *button, AppPanel *panel)
{
    app_catalog_dialog_for_selection(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))), panel->document);
}

/* Название плитки из каталога: «Мрамор белый, 600 × 300»; без рисунка — «только цвет». */
static void refresh_texture(AppPanel *panel, const TlTileSettings *tile, bool mixed)
{
    const TlCatalogTile *found = tile->texture[0] ? tl_catalog_find(app_catalogs(), tile->texture, NULL) : NULL;
    char *text = mixed           ? g_strdup("разные")
                 : !tile->texture[0] ? g_strdup("только цвет")
                 : found         ? g_strdup(found->name)
                                 : g_strdup_printf("нет в каталогах: %s", tile->texture);
    gtk_label_set_text(GTK_LABEL(panel->texture_name), text);
    gtk_widget_set_tooltip_text(panel->texture_name, text);
    g_free(text);
}

/* Цвет кнопки — в поле key ("color" или "color2"). */
static void on_color(GtkColorButton *button, AppPanel *panel)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    char hex[TL_COLOR_LENGTH];
    g_snprintf(hex, sizeof hex, "#%02x%02x%02x", (int)round(rgba.red * 255), (int)round(rgba.green * 255),
               (int)round(rgba.blue * 255));
    commit_tile_field(panel, find_field(g_object_get_data(G_OBJECT(button), "key")), hex);
}

static void on_pattern(GtkComboBox *combo, AppPanel *panel)
{
    size_t count;
    const TlPatternInfo *patterns = tl_patterns(&count);
    int index = gtk_combo_box_get_active(combo);
    if (index >= 0 && (size_t)index < count)
        commit_tile_field(panel, find_field("pattern"), patterns[index].id);
}

/* ---- плитка: раскладка от двери и от линейки -------------------------------------- */

/* Номер выбранного в списке предмета (id строки — номер в проекте); −1 — ничего. */
static long chosen_index(GtkWidget *choice, size_t count)
{
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(choice));
    long index = id ? strtol(id, NULL, 10) : -1;
    return index >= 0 && (size_t)index < count ? index : -1;
}

static void on_door(GtkButton *button, AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    TlProject *project = panel->document->project;
    long index = chosen_index(panel->door_choice, project->openings.count);
    if (!space)
        return;
    if (index < 0) {
        app_document_status(panel->document, "У комнаты нет проёма с полом — поставьте проём (O)");
        return;
    }
    const TlOpening *door = project->openings.items[index];
    TlVec2 from, to;
    double x, y;
    if (tl_project_space_door_edge(project, space, door, &from, &to) && tl_layout_door_offsets(space, from, to, &x, &y)) {
        set_offsets(panel, x, y);
        app_document_status(panel->document, "От двери «%s»: от порога целый ряд, проём — посередине", door->name);
    } else {
        app_document_status(panel->document, "Проём наискосок к плитке — поверните раскладку вдоль стены");
    }
}

/* Линейка в комнате — ось (ниши, унитаза, окна): по ней шов или середина плитки. */
static void on_ruler_line(GtkButton *button, AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    TlProject *project = panel->document->project;
    long index = chosen_index(panel->ruler_choice, project->rulers.count);
    if (!space)
        return;
    if (index < 0) {
        app_document_status(panel->document, "Поставьте в комнате линейку (M) по оси — ниши, унитаза, окна");
        return;
    }
    const TlRuler *ruler = project->rulers.items[index];
    TlLineAlign mode = (TlLineAlign)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "mode"));
    double x, y;
    if (tl_layout_line_offsets(space, ruler->start, ruler->end, mode, tl_space_label_point(space), &x, &y)) {
        set_offsets(panel, x, y);
        app_document_status(panel->document, "По линейке «%s»: %s", ruler->name,
                            mode == TL_LINE_JOINT ? "шов по оси" : "середина плитки по оси");
    } else {
        app_document_status(panel->document, "Линейка «%s» должна идти вдоль рядов плитки", ruler->name);
    }
}

/* Имя линейки или проёма с панели: Enter или уход из поля. */
static void rename_selected(GtkEntry *entry, AppPanel *panel)
{
    const AppSelection *s = &panel->document->selection;
    char *name = s->kind == APP_SELECT_RULER ? s->ruler->name : s->kind == APP_SELECT_OPENING ? s->opening->name : NULL;
    const char *text = gtk_entry_get_text(entry);
    if (panel->updating || !name || !text[0] || strcmp(text, name) == 0)
        return;
    app_document_checkpoint(panel->document, "name");
    g_strlcpy(name, text, TL_NAME_LENGTH);
    app_document_commit(panel->document);
}

static gboolean on_name_entry_focus_out(GtkWidget *entry, GdkEvent *event, AppPanel *panel)
{
    rename_selected(GTK_ENTRY(entry), panel);
    return FALSE;
}

static GtkWidget *name_entry(AppPanel *panel, const char *tooltip)
{
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), TL_NAME_LENGTH / 2 - 1); /* кириллица — два байта на букву */
    gtk_widget_set_tooltip_text(entry, tooltip);
    g_signal_connect(entry, "activate", G_CALLBACK(rename_selected), panel);
    g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_name_entry_focus_out), panel);
    return entry;
}

static GtkWidget *small_button(AppPanel *panel, const char *text, GCallback callback, const char *key, int value);

/* ---- зоны пола: бордюр и ковёр ------------------------------------------------------- */

static double *zone_number(TlZone *zone, const char *key)
{
    return strcmp(key, "width") == 0    ? &zone->width
           : strcmp(key, "height") == 0 ? &zone->height
           : strcmp(key, "dx") == 0     ? &zone->dx
           : strcmp(key, "dy") == 0     ? &zone->dy
                                        : NULL;
}

/* Значение поля зоны — всем выделенным комнатам. value: bool*, double*, или строка (цвет, рисунок). */
static void commit_zone_field(ZoneBinding *binding, const void *value)
{
    AppPanel *panel = binding->panel;
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = selected_spaces(panel, rooms);
    if (panel->updating || count == 0)
        return;
    char merge[48];
    g_snprintf(merge, sizeof merge, "zone_%d_%s", binding->kind, binding->key);
    app_document_checkpoint(panel->document, merge);
    const TlTileField *field = find_field(binding->key);
    for (size_t i = 0; i < count; i++) {
        TlZone *zone = &rooms[i]->zones[binding->kind];
        double *number = zone_number(zone, binding->key);
        if (strcmp(binding->key, "enabled") == 0)
            zone->enabled = *(const bool *)value;
        else if (strcmp(binding->key, "fit") == 0)
            zone->fit = *(const bool *)value;
        else if (number)
            *number = *(const double *)value;
        else if (field) {
            char *member = (char *)&zone->tile + field->offset;
            if (field->type == TL_FIELD_DOUBLE)
                *(double *)member = *(const double *)value;
            else if (field->type == TL_FIELD_COLOR || field->type == TL_FIELD_TEXT)
                g_strlcpy(member, value, field->size);
        }
    }
    app_document_commit(panel->document);
}

static void on_zone_spin(GtkSpinButton *spin, ZoneBinding *binding)
{
    double value = gtk_spin_button_get_value(spin);
    commit_zone_field(binding, &value);
}

static void on_zone_check(GtkToggleButton *check, ZoneBinding *binding)
{
    bool value = gtk_toggle_button_get_active(check);
    commit_zone_field(binding, &value);
}

static void on_zone_color(GtkColorButton *button, ZoneBinding *binding)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    char hex[TL_COLOR_LENGTH];
    g_snprintf(hex, sizeof hex, "#%02x%02x%02x", (int)round(rgba.red * 255), (int)round(rgba.green * 255),
               (int)round(rgba.blue * 255));
    commit_zone_field(binding, hex);
}

static void on_zone_pattern(GtkComboBox *combo, ZoneBinding *binding)
{
    size_t count;
    const TlPatternInfo *patterns = tl_patterns(&count);
    int index = gtk_combo_box_get_active(combo);
    if (index >= 0 && (size_t)index < count)
        commit_zone_field(binding, patterns[index].id);
}

static GtkWidget *bind_zone(AppPanel *panel, TlZoneKind kind, const char *key, GtkWidget *widget)
{
    ZoneBinding *binding = &panel->zone_bindings[panel->zone_binding_count++];
    *binding = (ZoneBinding){panel, kind, key, widget};
    if (GTK_IS_SPIN_BUTTON(widget))
        g_signal_connect(widget, "value-changed", G_CALLBACK(on_zone_spin), binding);
    else if (GTK_IS_COLOR_BUTTON(widget))
        g_signal_connect(widget, "color-set", G_CALLBACK(on_zone_color), binding);
    else if (GTK_IS_COMBO_BOX(widget))
        g_signal_connect(widget, "changed", G_CALLBACK(on_zone_pattern), binding);
    else
        g_signal_connect(widget, "toggled", G_CALLBACK(on_zone_check), binding);
    return widget;
}

static GtkWidget *zone_spin(AppPanel *panel, TlZoneKind kind, const char *key, double minimum, double maximum,
                            double step, const char *unit)
{
    return bind_zone(panel, kind, key, app_unit_spin_new(minimum, maximum, step, step < 1 ? 1 : 0, unit));
}

static GtkWidget *pattern_combo(void)
{
    GtkWidget *combo = gtk_combo_box_text_new();
    size_t count;
    const TlPatternInfo *patterns = tl_patterns(&count);
    for (size_t i = 0; i < count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), patterns[i].name);
    return combo;
}

static void on_zone_choose(GtkButton *button, AppPanel *panel)
{
    TlZoneKind kind = (TlZoneKind)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "zone"));
    app_catalog_dialog_for_zone(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))), panel->document, kind);
}

static void on_zone_color_only(GtkButton *button, AppPanel *panel)
{
    app_command_set_zone_texture(panel->document, (TlZoneKind)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "zone")),
                                 NULL, NULL);
}

/* Бордюр — полоса вдоль стен; ковёр — прямоугольник в центре. У зоны своя плитка, рисунок, цена. */
static void build_zone(AppPanel *panel, GtkWidget *box, TlZoneKind kind)
{
    AppSection *s = &panel->zone_sections[kind];
    bool border = kind == TL_ZONE_BORDER;
    *s = app_section_new(box, border ? "Бордюр вдоль стен" : "Ковёр (панно) в центре");
    GtkWidget *enabled = gtk_check_button_new_with_label(border ? "Бордюр по периметру комнаты"
                                                                : "Ковёр другой плиткой в центре");
    gtk_widget_set_tooltip_text(enabled, border ? "Полоса вдоль стен: у каждой стены первый ряд — от стены, углы — на ус"
                                                : "Прямоугольник в осях раскладки комнаты; основной пол его обходит");
    app_section_wide(s, bind_zone(panel, kind, "enabled", enabled));
    if (border) {
        app_section_row(s, "Ширина полосы", zone_spin(panel, kind, "width", 10, 5000, 10, "мм"));
    } else {
        app_section_row(s, "Размер по X", zone_spin(panel, kind, "width", 10, 50000, 10, "мм"));
        app_section_row(s, "Размер по Y", zone_spin(panel, kind, "height", 10, 50000, 10, "мм"));
        app_section_row(s, "Сдвиг X от центра", zone_spin(panel, kind, "dx", -50000, 50000, 10, "мм"));
        app_section_row(s, "Сдвиг Y от центра", zone_spin(panel, kind, "dy", -50000, 50000, 10, "мм"));
    }
    GtkWidget *fit = gtk_check_button_new_with_label("По целой плитке");
    gtk_widget_set_tooltip_text(fit, border ? "Ширина полосы — целое число рядов плитки со швами (и отступ от стены)"
                                            : "Размер ковра — целое число плиток со швами, ближайшее к заданному");
    app_section_wide(s, bind_zone(panel, kind, "fit", fit));
    panel->zone_size[kind] = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(panel->zone_size[kind]), 0);
    gtk_widget_set_opacity(panel->zone_size[kind], 0.75);
    app_section_wide(s, panel->zone_size[kind]);
    GtkWidget *texture = app_hbox(4);
    panel->zone_texture[kind] = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(panel->zone_texture[kind]), 0);
    gtk_label_set_ellipsize(GTK_LABEL(panel->zone_texture[kind]), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(texture), panel->zone_texture[kind], TRUE, TRUE, 0);
    GtkWidget *choose = small_button(panel, "Выбрать…", G_CALLBACK(on_zone_choose), "zone", kind);
    gtk_widget_set_tooltip_text(choose, "Плитка зоны из каталога: размер и рисунок");
    gtk_box_pack_start(GTK_BOX(texture), choose, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(texture), small_button(panel, "Цвет", G_CALLBACK(on_zone_color_only), "zone", kind), FALSE,
                       FALSE, 0);
    app_section_row(s, "Рисунок", texture);
    app_section_row(s, "Укладка", bind_zone(panel, kind, "pattern", pattern_combo()));
    app_section_row(s, "Длина плитки", zone_spin(panel, kind, "w", 10, 5000, 10, "мм"));
    app_section_row(s, "Ширина плитки", zone_spin(panel, kind, "h", 10, 5000, 10, "мм"));
    app_section_row(s, "Шов", zone_spin(panel, kind, "gap", 0, 50, 0.5, "мм"));
    app_section_row(s, "Поворот", zone_spin(panel, kind, "angle", -180, 180, 5, "°"));
    app_section_row(s, "Цвет", bind_zone(panel, kind, "color", gtk_color_button_new()));
    app_section_row(s, "Цвет второй плитки", bind_zone(panel, kind, "color2", gtk_color_button_new()));
    app_section_row(s, "Цена плитки", zone_spin(panel, kind, "price_tile", 0, 1e6, 50, "₽/м²"));
    app_section_row(s, "Цена укладки", zone_spin(panel, kind, "price_work", 0, 1e6, 50, "₽/м²"));
}

/* Поля зон — по первой выделенной комнате; без включённой зоны — недоступны (кроме флажка). */
static void refresh_zones(AppPanel *panel, TlSpace *space)
{
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++) {
        const TlZone *zone = &space->zones[kind];
        TlZone laid = tl_zone_effective(zone, (TlZoneKind)kind);
        char *size = kind == TL_ZONE_BORDER
                         ? g_strdup_printf("Кладётся полосой %.0f мм", laid.width)
                         : g_strdup_printf("Кладётся ковром %.0f × %.0f мм", laid.width, laid.height);
        gtk_label_set_text(GTK_LABEL(panel->zone_size[kind]), size);
        g_free(size);
        const TlCatalogTile *found = zone->tile.texture[0] ? tl_catalog_find(app_catalogs(), zone->tile.texture, NULL) : NULL;
        gtk_label_set_text(GTK_LABEL(panel->zone_texture[kind]), !zone->tile.texture[0] ? "только цвет"
                                                                  : found              ? found->name
                                                                                       : zone->tile.texture);
        gtk_widget_set_sensitive(gtk_widget_get_parent(panel->zone_texture[kind]), zone->enabled);
    }
    size_t count;
    const TlPatternInfo *patterns = tl_patterns(&count);
    for (size_t i = 0; i < panel->zone_binding_count; i++) {
        ZoneBinding *binding = &panel->zone_bindings[i];
        TlZone *zone = &space->zones[binding->kind];
        double *number = zone_number(zone, binding->key);
        const TlTileField *field = find_field(binding->key);
        bool enabled_key = strcmp(binding->key, "enabled") == 0;
        gtk_widget_set_sensitive(binding->widget, enabled_key || zone->enabled);
        if (enabled_key) {
            set_check(binding->widget, zone->enabled);
        } else if (strcmp(binding->key, "fit") == 0) {
            set_check(binding->widget, zone->fit);
        } else if (number) {
            set_spin(binding->widget, *number);
        } else if (strcmp(binding->key, "pattern") == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(binding->widget), (int)(tl_pattern_of(&zone->tile) - patterns));
        } else if (field && field->type == TL_FIELD_COLOR) {
            GdkRGBA rgba;
            if (gdk_rgba_parse(&rgba, (const char *)&zone->tile + field->offset))
                gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(binding->widget), &rgba);
        } else if (field && field->type == TL_FIELD_DOUBLE) {
            set_spin(binding->widget, *(const double *)((const char *)&zone->tile + field->offset));
        }
    }
}

/* ---- комната, дом, стена ------------------------------------------------------- */

/* Список комнат: щелчок — одна комната, Ctrl/Shift+щелчок — несколько. */
static void on_rooms_selected(GtkListBox *list, AppPanel *panel)
{
    if (panel->updating)
        return;
    TlProject *project = panel->document->project;
    TlSpace *current = selected_space(panel);
    AppSelection selection = {0};
    GList *rows = gtk_list_box_get_selected_rows(list);
    for (GList *row = rows; row; row = row->next) {
        int index = gtk_list_box_row_get_index(row->data);
        if (index < 0 || (size_t)index >= project->spaces.count)
            continue;
        TlSpace *space = project->spaces.items[index];
        if (!selection.space)
            selection = (AppSelection){.kind = APP_SELECT_SPACE, .space = space};
        else if (selection.more_count < APP_MAX_ROOMS - 1)
            selection.more_spaces[selection.more_count++] = space;
    }
    g_list_free(rows);
    for (size_t i = 0; i < selection.more_count; i++)
        if (selection.more_spaces[i] == current) { /* первой остаётся выделенная раньше */
            selection.more_spaces[i] = selection.space;
            selection.space = current;
        }
    if (selection.space)
        app_document_select(panel->document, selection);
}

static void rename_space(AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    const char *name = gtk_entry_get_text(GTK_ENTRY(panel->space_name));
    if (panel->updating || !space || !name[0] || strcmp(name, space->name) == 0)
        return;
    app_document_checkpoint(panel->document, "name");
    tl_space_set_name(space, name);
    app_document_commit(panel->document);
}

static void on_name_activate(GtkEntry *entry, AppPanel *panel) { rename_space(panel); }

static gboolean on_name_focus_out(GtkWidget *entry, GdkEvent *event, AppPanel *panel)
{
    rename_space(panel);
    return FALSE;
}

static void on_floor_source(GtkComboBox *combo, AppPanel *panel)
{
    TlSpace *space = selected_space(panel);
    const char *id = gtk_combo_box_get_active_id(combo);
    if (panel->updating || !space || !id)
        return;
    app_document_checkpoint(panel->document, "");
    space->source_id = atoi(id);
    tl_project_update_spaces(panel->document->project);
    app_document_commit(panel->document);
}

static void on_outline_thickness_all(GtkButton *button, AppPanel *panel)
{
    TlOutline *outline = app_selection_outline(&panel->document->selection);
    if (!outline)
        return;
    double thickness = spin_value(panel->outline_thickness);
    panel->document->preferences.outline_thickness = thickness;
    app_document_checkpoint(panel->document, "");
    for (size_t wall = 0; wall < tl_outline_wall_count(outline); wall++) {
        TlWallRefList links = tl_project_linked_walls(panel->document->project, outline, wall);
        if (!links.count) /* общие стены с соседними домами не трогаем */
            outline->thickness.items[wall] = thickness;
        tl_wall_ref_list_free(&links);
    }
    tl_project_update_spaces(panel->document->project);
    app_document_commit(panel->document);
}

static void on_delete_outline(GtkButton *button, AppPanel *panel) { app_command_delete_outline(panel->document); }

static bool selected_wall(AppPanel *panel, TlOutline **outline, size_t *wall)
{
    const AppSelection *s = &panel->document->selection;
    if (panel->updating || s->kind != APP_SELECT_WALL)
        return false;
    *outline = s->outline;
    *wall = (size_t)s->index;
    return true;
}

static void change_wall_length(AppPanel *panel, double inner_length, const char *key)
{
    TlOutline *outline;
    size_t wall;
    if (!selected_wall(panel, &outline, &wall) || inner_length <= 1)
        return;
    app_document_checkpoint(panel->document, key);
    if (tl_project_set_wall_length(panel->document->project, outline, wall, inner_length,
                                   check_active(panel->keep_angles), panel->document->preferences.link_walls))
        app_document_commit(panel->document);
}

static void on_wall_inner(GtkSpinButton *spin, AppPanel *panel)
{
    change_wall_length(panel, gtk_spin_button_get_value(spin), "len_inner");
}

static void on_wall_outer(GtkSpinButton *spin, AppPanel *panel)
{
    TlOutline *outline;
    size_t wall;
    if (!selected_wall(panel, &outline, &wall))
        return;
    /* при сохранении углов разница наружной и внутренней длины не меняется */
    double difference = tl_outline_outer_length(outline, wall) - tl_outline_inner_length(outline, wall);
    change_wall_length(panel, gtk_spin_button_get_value(spin) - difference, "len_outer");
}

static void on_wall_thickness(GtkSpinButton *spin, AppPanel *panel)
{
    TlOutline *outline;
    size_t wall;
    if (!selected_wall(panel, &outline, &wall))
        return;
    TlEditToken *token = app_document_begin_geometry(panel->document, "thick", NULL);
    tl_project_set_thickness(panel->document->project, outline, wall, gtk_spin_button_get_value(spin),
                             panel->document->preferences.link_walls);
    app_document_end_geometry(panel->document, token, NULL);
}

static void on_wall_lock(GtkToggleButton *check, AppPanel *panel)
{
    TlOutline *outline;
    size_t wall;
    if (!selected_wall(panel, &outline, &wall))
        return;
    app_document_checkpoint(panel->document, "");
    outline->locked.items[wall] = gtk_toggle_button_get_active(check);
    app_document_commit(panel->document);
}

static void step_wall(AppPanel *panel, int direction)
{
    const AppSelection *s = &panel->document->selection;
    TlOutline *outline = app_selection_outline(s);
    if (!outline)
        return;
    int count = (int)tl_outline_wall_count(outline);
    int current = s->kind == APP_SELECT_WALL ? s->index : (direction > 0 ? -1 : 0);
    int next = ((current + direction) % count + count) % count;
    app_document_select(panel->document, (AppSelection){.kind = APP_SELECT_WALL, .outline = outline, .index = next});
}

static void on_previous_wall(GtkButton *button, AppPanel *panel) { step_wall(panel, -1); }
static void on_next_wall(GtkButton *button, AppPanel *panel) { step_wall(panel, 1); }

/* ---- перегородка и проём ------------------------------------------------------- */

static TlPartition *selected_partition(AppPanel *panel)
{
    const AppSelection *s = &panel->document->selection;
    return !panel->updating && s->kind == APP_SELECT_PARTITION ? s->partition : NULL;
}

/* Правка перегородки с панели — одна точка отмены; узлы и примыкания других
   перегородок следуют за ней. NULL — перегородка не выделена. */
static TlEditToken *begin_partition_change(AppPanel *panel, const char *merge_key, TlPartition **partition)
{
    *partition = selected_partition(panel);
    return *partition ? app_document_begin_geometry(panel->document, merge_key, NULL) : NULL;
}

/* own_ends — концы перегородки заданы правкой (длина, угол): не возвращать их на
   грани, к которым они были привязаны, иначе перегородка съезжает вдоль стены. */
static void end_partition_change(AppPanel *panel, TlPartition *partition, TlEditToken *token, bool own_ends)
{
    TlEditOptions options = {.master = partition, .frozen = own_ends ? partition : NULL};
    app_document_end_geometry(panel->document, token, &options);
}

static void on_partition_length(GtkSpinButton *spin, AppPanel *panel)
{
    TlPartition *partition;
    TlEditToken *token = begin_partition_change(panel, "plen", &partition);
    if (!token)
        return;
    tl_partition_set_length(partition, gtk_spin_button_get_value(spin));
    end_partition_change(panel, partition, token, true);
}

static void on_partition_thickness(GtkSpinButton *spin, AppPanel *panel)
{
    TlPartition *partition;
    TlEditToken *token = begin_partition_change(panel, "pthick", &partition);
    if (!token)
        return;
    tl_partition_set_thickness(partition, gtk_spin_button_get_value(spin));
    end_partition_change(panel, partition, token, false);
}

static void on_partition_angle(GtkSpinButton *spin, AppPanel *panel)
{
    TlPartition *partition;
    TlEditToken *token = begin_partition_change(panel, "pangle", &partition);
    if (!token)
        return;
    tl_partition_set_angle(partition, gtk_spin_button_get_value(spin));
    end_partition_change(panel, partition, token, true);
}

static void on_partition_corner_offset(GtkSpinButton *spin, AppPanel *panel)
{
    TlPartitionCorner corner;
    if (!app_selection_partition_corner(&panel->document->selection, &corner))
        return;
    TlPartition *partition;
    TlEditToken *token = begin_partition_change(panel, "pcorner", &partition);
    if (!token)
        return;
    partition->corner_offsets[corner] = gtk_spin_button_get_value(spin);
    end_partition_change(panel, partition, token, false);
}

static void on_partition_make_rectangular(GtkButton *button, AppPanel *panel)
{
    TlPartition *partition;
    TlEditToken *token = begin_partition_change(panel, "", &partition);
    if (!token)
        return;
    tl_partition_set_thickness(partition, tl_partition_thickness_at(partition, TL_PARTITION_START));
    end_partition_change(panel, partition, token, false);
}

static void on_partition_lock(GtkToggleButton *check, AppPanel *panel)
{
    TlPartition *partition = selected_partition(panel);
    if (!partition)
        return;
    app_document_checkpoint(panel->document, "");
    partition->locked = gtk_toggle_button_get_active(check);
    app_document_commit(panel->document);
}

static void on_delete_selection(GtkButton *button, AppPanel *panel) { app_command_delete_selection(panel->document); }

static TlOpening *selected_opening(AppPanel *panel)
{
    const AppSelection *s = &panel->document->selection;
    return !panel->updating && s->kind == APP_SELECT_OPENING ? s->opening : NULL;
}

static void on_opening_width(GtkSpinButton *spin, AppPanel *panel)
{
    TlOpening *opening = selected_opening(panel);
    if (!opening)
        return;
    app_document_checkpoint(panel->document, "owidth");
    tl_project_set_opening_width(panel->document->project, opening, gtk_spin_button_get_value(spin));
    app_document_commit_rooms(panel->document);
}

static void place_opening(AppPanel *panel, double distance, bool from_end)
{
    TlOpening *opening = selected_opening(panel);
    if (!opening)
        return;
    app_document_checkpoint(panel->document, "opos");
    if (tl_project_place_opening_at(panel->document->project, opening, distance, from_end))
        app_document_commit(panel->document);
}

static void on_opening_from_start(GtkSpinButton *spin, AppPanel *panel)
{
    place_opening(panel, gtk_spin_button_get_value(spin), false);
}

static void on_opening_to_end(GtkSpinButton *spin, AppPanel *panel)
{
    place_opening(panel, gtk_spin_button_get_value(spin), true);
}

/* ---- стык комнат в проёме ----------------------------------------------------- */

/* Комнаты по обе стороны проёма: у грани 0 сечения (справа) и у грани 1
   (слева; за наружной стеной комнаты нет — outside). */
typedef struct DoorwaySides {
    TlVec2 right, left;
    double depth;
    const char *right_room, *left_room;
    bool outside;
} DoorwaySides;

#define ROOM_PROBE_MM 5.0 /* насколько за бок перегородки искать комнату */

static bool doorway_sides(TlProject *project, const TlOpening *opening, DoorwaySides *sides)
{
    if (!tl_project_opening_section(project, opening, &sides->right, &sides->left))
        return false;
    TlVec2 across = tl_vec2_sub(sides->left, sides->right);
    sides->depth = tl_vec2_length(across);
    TlVec2 step = tl_vec2_scale(tl_vec2_normalized(across), ROOM_PROBE_MM);
    TlSpace *right = tl_project_space_at(project, tl_vec2_sub(sides->right, step));
    TlSpace *left = tl_project_space_at(project, tl_vec2_add(sides->left, step));
    sides->right_room = right ? right->name : "комната";
    sides->left_room = left ? left->name : "комната";
    sides->outside = tl_project_opening_outside(project, opening);
    return sides->depth > 0;
}

/* Стык по середине проёма (middle) и наклону (skew — разница долей у косяков);
   концы не выходят за грани. */
static void set_threshold(AppPanel *panel, double middle, double skew)
{
    TlOpening *opening = selected_opening(panel);
    if (!opening)
        return;
    app_document_checkpoint(panel->document, "threshold");
    if (tl_project_set_threshold(panel->document->project, opening, middle, skew))
        app_document_commit(panel->document);
}

/* Глубина проёма, отданная комнате справа, — в мм. */
static void on_threshold_depth(GtkSpinButton *spin, AppPanel *panel)
{
    TlOpening *opening = selected_opening(panel);
    DoorwaySides sides;
    if (opening && doorway_sides(panel->document->project, opening, &sides))
        set_threshold(panel, gtk_spin_button_get_value(spin) / sides.depth, opening->threshold_end - opening->threshold);
}

/* Наклон стыка: на сколько мм конец у косяка в конце проёма дальше, чем в начале. */
static void on_threshold_skew(GtkSpinButton *spin, AppPanel *panel)
{
    TlOpening *opening = selected_opening(panel);
    DoorwaySides sides;
    if (opening && doorway_sides(panel->document->project, opening, &sides))
        set_threshold(panel, (opening->threshold + opening->threshold_end) / 2,
                      gtk_spin_button_get_value(spin) / sides.depth);
}

static void on_threshold_gap(GtkSpinButton *spin, AppPanel *panel)
{
    TlOpening *opening = selected_opening(panel);
    if (!opening)
        return;
    app_document_checkpoint(panel->document, "threshold_gap");
    opening->threshold_gap = gtk_spin_button_get_value(spin);
    app_document_commit_rooms(panel->document);
}

static void on_threshold_preset(GtkButton *button, AppPanel *panel)
{
    set_threshold(panel, *(const double *)g_object_get_data(G_OBJECT(button), "share"), 0);
}

static GtkWidget *threshold_button(AppPanel *panel, const double *share)
{
    GtkWidget *button = gtk_button_new();
    g_object_set_data(G_OBJECT(button), "share", (gpointer)share);
    return connect_button(button, G_CALLBACK(on_threshold_preset), panel);
}

static void on_opening_lock(GtkToggleButton *check, AppPanel *panel)
{
    TlOpening *opening = selected_opening(panel);
    if (!opening)
        return;
    app_document_checkpoint(panel->document, "");
    opening->locked = gtk_toggle_button_get_active(check);
    app_document_commit(panel->document);
}

/* ---- настройки правки --------------------------------------------------------- */

static void on_partition_preference(GtkSpinButton *spin, AppPanel *panel)
{
    panel->document->preferences.partition_thickness = gtk_spin_button_get_value(spin);
}

static void on_opening_preference(GtkSpinButton *spin, AppPanel *panel)
{
    panel->document->preferences.opening_width = gtk_spin_button_get_value(spin);
}

static void on_link_walls(GtkToggleButton *check, AppPanel *panel)
{
    panel->document->preferences.link_walls = gtk_toggle_button_get_active(check);
}

static void on_auto_align(GtkToggleButton *check, AppPanel *panel)
{
    panel->document->preferences.auto_align = gtk_toggle_button_get_active(check);
}

static void on_measure_magnet(GtkToggleButton *check, AppPanel *panel)
{
    panel->document->preferences.measure_magnet = gtk_toggle_button_get_active(check);
}

static void on_expansion_joint_preference(GtkSpinButton *spin, AppPanel *panel)
{
    panel->document->preferences.expansion_joint_gap = gtk_spin_button_get_value(spin);
}

/* Зазор выделенного шва: меняет куски плитки по обе стороны. */
static void on_expansion_joint_gap(GtkSpinButton *spin, AppPanel *panel)
{
    const AppSelection *s = &panel->document->selection;
    if (panel->updating || s->kind != APP_SELECT_EXPANSION_JOINT)
        return;
    app_document_checkpoint(panel->document, "jgap");
    s->expansion_joint->gap = gtk_spin_button_get_value(spin);
    tl_project_update_spaces(panel->document->project);
    app_document_commit(panel->document);
}

static void on_delete_all_rulers(GtkButton *button, AppPanel *panel) { app_command_delete_all_rulers(panel->document); }

static void on_pipe_preference(GtkSpinButton *spin, AppPanel *panel)
{
    panel->document->preferences.pipe_diameter = gtk_spin_button_get_value(spin);
}

/* Диаметр выделенной трубы; он же — для следующих. */
static void on_pipe_diameter(GtkSpinButton *spin, AppPanel *panel)
{
    const AppSelection *s = &panel->document->selection;
    if (panel->updating || s->kind != APP_SELECT_PIPE)
        return;
    app_document_checkpoint(panel->document, "pipe_d");
    s->pipe->diameter = gtk_spin_button_get_value(spin);
    panel->document->preferences.pipe_diameter = s->pipe->diameter;
    tl_project_update_spaces(panel->document->project);
    app_document_commit(panel->document);
}

static void on_pipe_coordinate(GtkSpinButton *spin, AppPanel *panel)
{
    const AppSelection *s = &panel->document->selection;
    if (panel->updating || s->kind != APP_SELECT_PIPE)
        return;
    app_document_checkpoint(panel->document, "pipe_xy");
    s->pipe->center = tl_vec2(spin_value(panel->pipe_x), spin_value(panel->pipe_y));
    tl_project_update_spaces(panel->document->project);
    app_document_commit(panel->document);
}

static void on_keep_angles(GtkToggleButton *check, AppPanel *panel)
{
    panel->document->preferences.keep_angles = gtk_toggle_button_get_active(check);
}

static void on_tool_button(GtkButton *button, AppPanel *panel)
{
    app_canvas_set_tool(panel->canvas, (AppToolId)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tool")));
}

static void on_add_house(GtkButton *button, AppPanel *panel)
{
    if (panel->actions.add_house)
        panel->actions.add_house(panel->actions.user_data);
}

static void on_print(GtkButton *button, AppPanel *panel)
{
    if (panel->actions.print)
        panel->actions.print(panel->actions.user_data);
}

/* ---- построение секций ---------------------------------------------------------- */

static GtkWidget *tool_button(AppPanel *panel, const char *icon, const char *text, AppToolId tool)
{
    GtkWidget *button = app_icon_button_new(icon, text);
    g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(tool));
    return connect_button(button, G_CALLBACK(on_tool_button), panel);
}

static void build_rooms(AppPanel *panel, GtkWidget *box)
{
    AppSection section = app_section_new(box, "Комнаты");
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 100);
    panel->room_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(panel->room_list), GTK_SELECTION_MULTIPLE);
    gtk_widget_set_tooltip_text(panel->room_list, "Ctrl или Shift+щелчок — несколько комнат: плитку, клей и СВП\n"
                                                  "можно задать им сразу (на плане — Shift+щелчок)");
    gtk_container_add(GTK_CONTAINER(scrolled), panel->room_list);
    g_signal_connect(panel->room_list, "selected-rows-changed", G_CALLBACK(on_rooms_selected), panel);
    app_section_wide(&section, scrolled);
    GtkWidget *buttons = app_hbox(4);
    gtk_box_pack_start(GTK_BOX(buttons),
                       connect_button(app_icon_button_new("house", "Дом по размерам…"), G_CALLBACK(on_add_house), panel),
                       TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), tool_button(panel, "rect", "Нарисовать", APP_TOOL_RECT), TRUE, TRUE, 0);
    app_section_wide(&section, buttons);
}

static void build_tools(AppPanel *panel, GtkWidget *box)
{
    AppSection section = app_section_new(box, "Перегородки, проёмы, швы, линейки, трубы");
    panel->partition_thickness = app_unit_spin_new(10, 1000, 10, 0, "мм");
    g_signal_connect(panel->partition_thickness, "value-changed", G_CALLBACK(on_partition_preference), panel);
    gtk_grid_attach(GTK_GRID(section.grid), tool_button(panel, "partition", "Перегородка", APP_TOOL_PARTITION), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(section.grid), panel->partition_thickness, 1, 0, 1, 1);
    panel->opening_width = app_unit_spin_new(100, 10000, 50, 0, "мм");
    g_signal_connect(panel->opening_width, "value-changed", G_CALLBACK(on_opening_preference), panel);
    gtk_grid_attach(GTK_GRID(section.grid), tool_button(panel, "opening", "Проём", APP_TOOL_OPENING), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(section.grid), panel->opening_width, 1, 1, 1, 1);
    panel->expansion_joint_gap = app_unit_spin_new(1, 100, 1, 0, "мм");
    gtk_widget_set_tooltip_text(panel->expansion_joint_gap, "Зазор новых деформационных швов");
    g_signal_connect(panel->expansion_joint_gap, "value-changed", G_CALLBACK(on_expansion_joint_preference), panel);
    gtk_grid_attach(GTK_GRID(section.grid),
                    tool_button(panel, "expansion_joint", "Шов", APP_TOOL_EXPANSION_JOINT), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(section.grid), panel->expansion_joint_gap, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(section.grid), tool_button(panel, "ruler", "Линейка", APP_TOOL_RULER), 0, 3, 1, 1);
    panel->measure_magnet = gtk_check_button_new_with_label("Примагничивать");
    gtk_widget_set_tooltip_text(panel->measure_magnet,
                                "Концы линейки притягиваются к углам, серединам, краям и осям стен и к швам плитки, "
                                "концы деформационного шва — к поверхностям стен. Shift — временно без притягивания.");
    g_signal_connect(panel->measure_magnet, "toggled", G_CALLBACK(on_measure_magnet), panel);
    gtk_grid_attach(GTK_GRID(section.grid), panel->measure_magnet, 1, 3, 1, 1);
    panel->pipe_diameter = app_unit_spin_new(TL_PIPE_MIN_DIAMETER, TL_PIPE_MAX_DIAMETER, 1, 0, "мм");
    gtk_widget_set_tooltip_text(panel->pipe_diameter, "Диаметр новых отверстий под трубы (коронка): "
                                                      "канализация 50 и 110, отопление 20–32");
    g_signal_connect(panel->pipe_diameter, "value-changed", G_CALLBACK(on_pipe_preference), panel);
    gtk_grid_attach(GTK_GRID(section.grid), tool_button(panel, "pipe", "Труба", APP_TOOL_PIPE), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(section.grid), panel->pipe_diameter, 1, 4, 1, 1);
    section.rows = 5;
    panel->auto_align = gtk_check_button_new_with_label("Автовыравнивание по горизонтали и вертикали");
    gtk_widget_set_tooltip_text(panel->auto_align,
                                "Почти горизонтальная или вертикальная линейка, стена или перегородка выпрямляется, "
                                "а точка встаёт на одну линию с углами других стен (видна пунктирная линия). "
                                "Shift — временно без выравнивания.");
    g_signal_connect(panel->auto_align, "toggled", G_CALLBACK(on_auto_align), panel);
    app_section_wide(&section, panel->auto_align);
    panel->link_walls = gtk_check_button_new_with_label("Связывать смежные стены соседних домов");
    g_signal_connect(panel->link_walls, "toggled", G_CALLBACK(on_link_walls), panel);
    app_section_wide(&section, panel->link_walls);
}

static void build_space(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->space_section;
    *s = app_section_new(box, "Комната");
    panel->space_name = gtk_entry_new();
    g_signal_connect(panel->space_name, "activate", G_CALLBACK(on_name_activate), panel);
    g_signal_connect(panel->space_name, "focus-out-event", G_CALLBACK(on_name_focus_out), panel);
    app_section_row(s, "Название", panel->space_name);
    panel->space_area = app_section_value_label(s, "Площадь пола");
    panel->space_perimeter = app_section_value_label(s, "Периметр");
    panel->floor_source = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(panel->floor_source, "Своя раскладка или общая сетка плитки с другой комнатой");
    g_signal_connect(panel->floor_source, "changed", G_CALLBACK(on_floor_source), panel);
    app_section_row(s, "Пол", panel->floor_source);
}

static void build_outline(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->outline_section;
    *s = app_section_new(box, "Контур (дом / квартира)");
    panel->outline_area = app_section_value_label(s, "Площадь внутри");
    GtkWidget *row = app_hbox(4);
    panel->outline_thickness = app_unit_spin_new(0, 2000, 10, 0, "мм");
    gtk_box_pack_start(GTK_BOX(row), panel->outline_thickness, TRUE, TRUE, 0);
    GtkWidget *apply = connect_button(gtk_button_new_with_label("Всем стенам"), G_CALLBACK(on_outline_thickness_all), panel);
    gtk_widget_set_tooltip_text(apply, "Толщина всех наружных стен контура (кроме общих с соседним домом)");
    gtk_box_pack_start(GTK_BOX(row), apply, FALSE, FALSE, 0);
    app_section_row(s, "Толщина", row);
    app_section_wide(s, connect_button(app_icon_button_new("delete", "Удалить контур целиком"),
                                       G_CALLBACK(on_delete_outline), panel));
}

static GtkWidget *spin_with(AppPanel *panel, double minimum, double maximum, double step, int digits,
                            const char *unit, GCallback callback)
{
    GtkWidget *spin = app_unit_spin_new(minimum, maximum, step, digits, unit);
    g_signal_connect(spin, "value-changed", callback, panel);
    return spin;
}

static void build_wall(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->wall_section;
    *s = app_section_new(box, "Стена");
    GtkWidget *row = app_hbox(4);
    gtk_box_pack_start(GTK_BOX(row), connect_button(gtk_button_new_with_label("◀"), G_CALLBACK(on_previous_wall), panel),
                       FALSE, FALSE, 0);
    panel->wall_number = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(row), panel->wall_number, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), connect_button(gtk_button_new_with_label("▶"), G_CALLBACK(on_next_wall), panel),
                       FALSE, FALSE, 0);
    app_section_wide(s, row);
    panel->wall_inner = spin_with(panel, 1, 200000, 10, 1, "мм", G_CALLBACK(on_wall_inner));
    app_section_row(s, "Длина внутр.", panel->wall_inner);
    panel->wall_outer = spin_with(panel, 1, 200000, 10, 1, "мм", G_CALLBACK(on_wall_outer));
    app_section_row(s, "Длина наружн.", panel->wall_outer);
    panel->wall_axis = app_section_value_label(s, "По оси стены");
    panel->wall_thickness = spin_with(panel, 0, 2000, 10, 0, "мм", G_CALLBACK(on_wall_thickness));
    app_section_row(s, "Толщина", panel->wall_thickness);
    panel->wall_angle = app_section_value_label(s, "Направление");
    panel->keep_angles = gtk_check_button_new_with_label("При изменении длины сохранять углы");
    gtk_widget_set_tooltip_text(panel->keep_angles, "Вкл.: сдвигается следующая стена параллельно себе.\n"
                                                    "Выкл.: двигается только конечный угол стены.");
    g_signal_connect(panel->keep_angles, "toggled", G_CALLBACK(on_keep_angles), panel);
    app_section_wide(s, panel->keep_angles);
    panel->wall_lock = lock_check();
    g_signal_connect(panel->wall_lock, "toggled", G_CALLBACK(on_wall_lock), panel);
    app_section_wide(s, panel->wall_lock);
}

/* Углы тела: квадратные маркеры на плане; выделенный угол — точно здесь. */
static void build_partition_corners(AppPanel *panel, AppSection *section)
{
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Углы тела</b> — для точного раскроя");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    app_section_wide(section, title);
    panel->partition_corner_hint = gtk_label_new("Выделите квадратный маркер на углу перегородки: его можно "
                                                 "тянуть поперёк оси или задать отступ здесь.");
    gtk_label_set_line_wrap(GTK_LABEL(panel->partition_corner_hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(panel->partition_corner_hint), 0);
    gtk_widget_set_opacity(panel->partition_corner_hint, 0.7);
    app_section_wide(section, panel->partition_corner_hint);

    AppSection corner = {gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), gtk_grid_new(), 0};
    gtk_grid_set_row_spacing(GTK_GRID(corner.grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(corner.grid), 8);
    gtk_container_add(GTK_CONTAINER(corner.frame), corner.grid);
    panel->partition_corner_name = app_section_value_label(&corner, "Угол");
    panel->partition_corner_offset = spin_with(panel, 0, 2000, 1, 1, "мм", G_CALLBACK(on_partition_corner_offset));
    gtk_widget_set_tooltip_text(panel->partition_corner_offset, "Расстояние от оси перегородки до этого угла");
    app_section_row(&corner, "От оси", panel->partition_corner_offset);
    panel->partition_corner_position = app_section_value_label(&corner, "Координаты");
    panel->partition_corner_box = corner.frame;
    app_section_wide(section, corner.frame);
    app_section_wide(section, connect_button(gtk_button_new_with_label("Сделать прямоугольной"),
                                             G_CALLBACK(on_partition_make_rectangular), panel));
}

static void build_partition(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->partition_section;
    *s = app_section_new(box, "Перегородка");
    panel->partition_length = spin_with(panel, 1, 200000, 10, 1, "мм", G_CALLBACK(on_partition_length));
    app_section_row(s, "Длина (по оси)", panel->partition_length);
    panel->partition_own_thickness = spin_with(panel, 1, 2000, 10, 0, "мм", G_CALLBACK(on_partition_thickness));
    app_section_row(s, "Толщина", panel->partition_own_thickness);
    panel->partition_angle = spin_with(panel, 0, 360, 1, 1, "°", G_CALLBACK(on_partition_angle));
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(panel->partition_angle), TRUE);
    gtk_widget_set_tooltip_text(panel->partition_angle, "Поворот вокруг начала перегородки. 0° — вправо, 90° — вверх");
    app_section_row(s, "Направление", panel->partition_angle);

    build_partition_corners(panel, s);
    panel->partition_lock = lock_check();
    g_signal_connect(panel->partition_lock, "toggled", G_CALLBACK(on_partition_lock), panel);
    app_section_wide(s, panel->partition_lock);
    app_section_wide(s, connect_button(app_icon_button_new("delete", "Удалить перегородку"),
                                       G_CALLBACK(on_delete_selection), panel));
}

static void build_expansion_joint(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->expansion_joint_section;
    *s = app_section_new(box, "Деформационный шов");
    panel->expansion_joint_own_gap = spin_with(panel, 1, 100, 1, 1, "мм", G_CALLBACK(on_expansion_joint_gap));
    gtk_widget_set_tooltip_text(panel->expansion_joint_own_gap, "Ширина шва: на столько плитка отступает от линии шва "
                                                                "(поровну с каждой стороны)");
    app_section_row(s, "Зазор", panel->expansion_joint_own_gap);
    panel->expansion_joint_length = app_section_value_label(s, "Длина");
    app_section_wide(s, connect_button(app_icon_button_new("delete", "Удалить шов"),
                                       G_CALLBACK(on_delete_selection), panel));
}

static void build_pipe(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->pipe_section;
    *s = app_section_new(box, "Труба (отверстие в плитке)");
    panel->pipe_own_diameter = spin_with(panel, TL_PIPE_MIN_DIAMETER, TL_PIPE_MAX_DIAMETER, 1, 0, "мм",
                                         G_CALLBACK(on_pipe_diameter));
    gtk_widget_set_tooltip_text(panel->pipe_own_diameter, "Диаметр коронки — на чертеже подрезки");
    app_section_row(s, "Диаметр", panel->pipe_own_diameter);
    panel->pipe_x = spin_with(panel, -1e7, 1e7, 1, 0, "мм", G_CALLBACK(on_pipe_coordinate));
    app_section_row(s, "Центр X", panel->pipe_x);
    panel->pipe_y = spin_with(panel, -1e7, 1e7, 1, 0, "мм", G_CALLBACK(on_pipe_coordinate));
    app_section_row(s, "Центр Y", panel->pipe_y);
    app_section_wide(s, connect_button(app_icon_button_new("delete", "Удалить трубу"),
                                       G_CALLBACK(on_delete_selection), panel));
}

/* ---- точка с координатами: узел перегородок, угол дома ------------------------ */

#define COORDINATE_LIMIT 1e7

static void on_node_coordinate(GtkSpinButton *spin, AppPanel *panel)
{
    (void)spin;
    if (panel->updating)
        return;
    TlVec2 point = tl_vec2(gtk_spin_button_get_value(GTK_SPIN_BUTTON(panel->node_x)),
                           gtk_spin_button_get_value(GTK_SPIN_BUTTON(panel->node_y)));
    AppSelection selection = panel->document->selection;
    if (selection.kind == APP_SELECT_TEE)
        app_command_set_tee_point(panel->document, &selection, point);
    else
        app_command_set_node_point(panel->document, &selection, point);
}

static void on_vertex_coordinate(GtkSpinButton *spin, AppPanel *panel)
{
    (void)spin;
    const AppSelection *s = &panel->document->selection;
    if (panel->updating || s->kind != APP_SELECT_VERTEX)
        return;
    TlVec2 point = tl_vec2(gtk_spin_button_get_value(GTK_SPIN_BUTTON(panel->vertex_x)),
                           gtk_spin_button_get_value(GTK_SPIN_BUTTON(panel->vertex_y)));
    app_command_set_vertex(panel->document, s->outline, (size_t)s->index, point);
}

static void build_node(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->node_section;
    *s = app_section_new(box, "Узел перегородок");
    panel->node_point_name = app_section_value_label(s, "Точка");
    panel->node_members = app_section_value_label(s, "Перегородок");
    panel->node_x = spin_with(panel, -COORDINATE_LIMIT, COORDINATE_LIMIT, 1, 1, "мм", G_CALLBACK(on_node_coordinate));
    gtk_widget_set_tooltip_text(panel->node_x, "Середина узла переносит его целиком — перегородки тянутся за ним;\n"
                                               "угол узла меняет углы тел у узла, дальние концы остаются на месте;\n"
                                               "примыкание скользит по боку, а за гранью становится узлом.\n"
                                               "Delete на узле — убрать его: перегородка выпрямится");
    app_section_row(s, "X", panel->node_x);
    panel->node_y = spin_with(panel, -COORDINATE_LIMIT, COORDINATE_LIMIT, 1, 1, "мм", G_CALLBACK(on_node_coordinate));
    app_section_row(s, "Y", panel->node_y);
}

static void build_vertex(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->vertex_section;
    *s = app_section_new(box, "Угол дома");
    panel->vertex_x = spin_with(panel, -COORDINATE_LIMIT, COORDINATE_LIMIT, 1, 1, "мм", G_CALLBACK(on_vertex_coordinate));
    gtk_widget_set_tooltip_text(panel->vertex_x, "Внутренний угол стен; перегородки, упёртые в эти стены, следуют за ними");
    app_section_row(s, "X", panel->vertex_x);
    panel->vertex_y = spin_with(panel, -COORDINATE_LIMIT, COORDINATE_LIMIT, 1, 1, "мм", G_CALLBACK(on_vertex_coordinate));
    app_section_row(s, "Y", panel->vertex_y);
}

static void refresh_node(AppPanel *panel)
{
    TlNode node;
    TlTee tee;
    const AppSelection *s = &panel->document->selection;
    if (app_selection_tee(s, panel->document->project, &tee)) {
        gtk_label_set_text(GTK_LABEL(panel->node_point_name), "примыкание к боку");
        set_label(panel->node_members, "%.0f", 2.0);
        set_spin(panel->node_x, tee.point.x);
        set_spin(panel->node_y, tee.point.y);
        return;
    }
    if (!app_selection_node(s, panel->document->project, &node))
        return;
    char name[64];
    if (s->index == 0)
        snprintf(name, sizeof name, "середина узла");
    else
        snprintf(name, sizeof name, "угол %d из %zu", s->index, node.count);
    gtk_label_set_text(GTK_LABEL(panel->node_point_name), name);
    set_label(panel->node_members, "%.0f", (double)node.count);
    TlVec2 point = app_node_point(&node, s->index);
    set_spin(panel->node_x, point.x);
    set_spin(panel->node_y, point.y);
}

static void build_ruler(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->ruler_section;
    *s = app_section_new(box, "Линейка");
    panel->ruler_name = name_entry(panel, "Имя линейки — по нему её выбирают в «Шов по линейке»");
    app_section_row(s, "Имя", panel->ruler_name);
    panel->ruler_length = app_section_value_label(s, "Длина");
    panel->ruler_count = app_section_value_label(s, "Всего линеек");
    app_section_wide(s, connect_button(app_icon_button_new("delete", "Удалить линейку"),
                                       G_CALLBACK(on_delete_selection), panel));
    app_section_wide(s, connect_button(gtk_button_new_with_label("Удалить все линейки"),
                                       G_CALLBACK(on_delete_all_rulers), panel));
}

/* Стык и пол под проёмом: в перегородке и в стене между домами — пол обеих
   комнат, в наружной стене — пол комнаты до стыка. */
static void build_threshold(AppPanel *panel, AppSection *section)
{
    static const double ALL_TO_LEFT = 0, HALVES = 0.5, ALL_TO_RIGHT = 1;
    AppSection threshold = {gtk_box_new(GTK_ORIENTATION_VERTICAL, 4), gtk_grid_new(), 0};
    gtk_grid_set_row_spacing(GTK_GRID(threshold.grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(threshold.grid), 8);
    panel->threshold_title = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(panel->threshold_title), 0);
    gtk_box_pack_start(GTK_BOX(threshold.frame), panel->threshold_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(threshold.frame), threshold.grid, FALSE, FALSE, 0);
    panel->threshold_depth = spin_with(panel, 0, 2000, 1, 0, "мм", G_CALLBACK(on_threshold_depth));
    gtk_widget_set_tooltip_text(panel->threshold_depth, "Сколько глубины проёма — пол этой комнаты; остальное — "
                                                        "соседней (у наружной стены — улица).\nСтык можно и "
                                                        "потянуть мышью на плане");
    panel->threshold_depth_caption = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(panel->threshold_depth_caption), 0);
    gtk_label_set_ellipsize(GTK_LABEL(panel->threshold_depth_caption), PANGO_ELLIPSIZE_END);
    gtk_grid_attach(GTK_GRID(threshold.grid), panel->threshold_depth_caption, 0, 0, 1, 1);
    gtk_widget_set_hexpand(panel->threshold_depth, TRUE);
    gtk_grid_attach(GTK_GRID(threshold.grid), panel->threshold_depth, 1, 0, 1, 1);
    panel->threshold_gap = spin_with(panel, 0, 50, 0.5, 1, "мм", G_CALLBACK(on_threshold_gap));
    gtk_widget_set_tooltip_text(panel->threshold_gap, "Зазор между плиткой комнат по линии стыка — поровну в обе "
                                                      "стороны.\nОтступ плитки от стен у стыка не действует: "
                                                      "зазор там задаёт только шов");
    panel->threshold_skew = spin_with(panel, -2000, 2000, 1, 0, "мм", G_CALLBACK(on_threshold_skew));
    gtk_widget_set_tooltip_text(panel->threshold_skew, "Стык наискосок: на сколько у одного косяка он дальше, чем у "
                                                       "другого (0 — параллельно полотну).\nКонцы стыка можно и "
                                                       "потянуть мышью — у выделенного стыка");
    const char *captions[] = {"Наклон", "Шов"};
    GtkWidget *spins[] = {panel->threshold_skew, panel->threshold_gap};
    for (int row = 0; row < 2; row++) {
        GtkWidget *caption = gtk_label_new(captions[row]);
        gtk_label_set_xalign(GTK_LABEL(caption), 0);
        gtk_grid_attach(GTK_GRID(threshold.grid), caption, 0, row + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(threshold.grid), spins[row], 1, row + 1, 1, 1);
    }
    GtkWidget *presets = app_hbox(4);
    panel->threshold_to_left = threshold_button(panel, &ALL_TO_LEFT);
    panel->threshold_middle = threshold_button(panel, &HALVES);
    panel->threshold_to_right = threshold_button(panel, &ALL_TO_RIGHT);
    gtk_button_set_label(GTK_BUTTON(panel->threshold_middle), "Пополам");
    GtkWidget *buttons[] = {panel->threshold_to_left, panel->threshold_middle, panel->threshold_to_right};
    for (size_t i = 0; i < G_N_ELEMENTS(buttons); i++)
        gtk_box_pack_start(GTK_BOX(presets), buttons[i], TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(threshold.frame), presets, FALSE, FALSE, 0);
    panel->threshold_box = threshold.frame;
    app_section_wide(section, threshold.frame);
}

/* Кнопка «весь проём — комнате»: на ней имя комнаты. */
static void label_threshold_button(GtkWidget *button, const char *room)
{
    char *text = g_strdup_printf("Весь — «%s»", room);
    gtk_button_set_label(GTK_BUTTON(button), text);
    gtk_widget_set_tooltip_text(button, text);
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(button));
    if (GTK_IS_LABEL(label))
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    g_free(text);
}

static void refresh_threshold(AppPanel *panel, TlOpening *opening)
{
    DoorwaySides sides;
    bool doorway = doorway_sides(panel->document->project, opening, &sides);
    gtk_widget_set_visible(panel->threshold_box, doorway);
    if (!doorway)
        return;
    gtk_label_set_markup(GTK_LABEL(panel->threshold_title),
                         sides.outside ? "<b>Пол под дверью</b> — до стыка, дальше улица"
                                       : "<b>Стык комнат</b> — пол под дверью");
    char *caption = g_strdup_printf("У «%s»", sides.right_room);
    gtk_label_set_text(GTK_LABEL(panel->threshold_depth_caption), caption);
    g_free(caption);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(panel->threshold_depth), 0, sides.depth);
    set_spin(panel->threshold_depth, (opening->threshold + opening->threshold_end) / 2 * sides.depth);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(panel->threshold_skew), -sides.depth, sides.depth);
    set_spin(panel->threshold_skew, (opening->threshold_end - opening->threshold) * sides.depth);
    set_spin(panel->threshold_gap, opening->threshold_gap);
    if (sides.outside) { /* стык у внутренней грани — пола под проёмом нет */
        gtk_button_set_label(GTK_BUTTON(panel->threshold_to_left), "Без пола");
        gtk_widget_set_tooltip_text(panel->threshold_to_left, "Пола под проёмом нет: плитка — до стены");
    } else {
        label_threshold_button(panel->threshold_to_left, sides.left_room); /* стык у грани 0 */
    }
    label_threshold_button(panel->threshold_to_right, sides.right_room);
    GtkWidget *controls[] = {panel->threshold_depth, panel->threshold_skew, panel->threshold_gap,
                             panel->threshold_to_left, panel->threshold_middle, panel->threshold_to_right};
    for (size_t i = 0; i < G_N_ELEMENTS(controls); i++)
        gtk_widget_set_sensitive(controls[i], !opening->locked);
}

static void build_opening(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->opening_section;
    *s = app_section_new(box, "Проём");
    panel->opening_name = name_entry(panel, "Имя проёма — по нему его выбирают в «От двери»");
    app_section_row(s, "Имя", panel->opening_name);
    panel->opening_host = app_section_value_label(s, "Где");
    panel->opening_own_width = spin_with(panel, 10, 20000, 50, 0, "мм", G_CALLBACK(on_opening_width));
    app_section_row(s, "Ширина", panel->opening_own_width);
    panel->opening_from_start = spin_with(panel, 0, 200000, 10, 1, "мм", G_CALLBACK(on_opening_from_start));
    app_section_row(s, "От начала стены", panel->opening_from_start);
    panel->opening_to_end = spin_with(panel, 0, 200000, 10, 1, "мм", G_CALLBACK(on_opening_to_end));
    app_section_row(s, "До конца стены", panel->opening_to_end);
    build_threshold(panel, s);
    panel->opening_lock = lock_check();
    g_signal_connect(panel->opening_lock, "toggled", G_CALLBACK(on_opening_lock), panel);
    app_section_wide(s, panel->opening_lock);
    app_section_wide(s, connect_button(app_icon_button_new("delete", "Удалить проём"), G_CALLBACK(on_delete_selection), panel));
}

static GtkWidget *small_button(AppPanel *panel, const char *text, GCallback callback, const char *key, int value)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    if (key)
        g_object_set_data(G_OBJECT(button), key, GINT_TO_POINTER(value));
    return connect_button(button, callback, panel);
}

static void build_tile(AppPanel *panel, GtkWidget *box)
{
    AppSection *s = &panel->tile_section;
    *s = app_section_new(box, "Плитка");
    panel->shared_note = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(panel->shared_note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(panel->shared_note), 0);
    app_section_wide(s, panel->shared_note);
    GtkWidget *enabled = gtk_check_button_new_with_label("Укладывать плитку");
    bind_tile(panel, "enabled", enabled);
    app_section_wide(s, enabled);

    GtkWidget *texture = app_hbox(4);
    panel->texture_name = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(panel->texture_name), 0);
    gtk_label_set_ellipsize(GTK_LABEL(panel->texture_name), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(texture), panel->texture_name, TRUE, TRUE, 0);
    GtkWidget *choose = connect_button(gtk_button_new_with_label("Выбрать…"), G_CALLBACK(on_choose_texture), panel);
    gtk_widget_set_tooltip_text(choose, "Плитка из каталога: размер и рисунок (Ctrl+K)");
    gtk_box_pack_start(GTK_BOX(texture), choose, FALSE, FALSE, 0);
    app_section_row(s, "Рисунок", texture);

    panel->pattern = gtk_combo_box_text_new();
    size_t pattern_count;
    const TlPatternInfo *patterns = tl_patterns(&pattern_count);
    for (size_t i = 0; i < pattern_count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->pattern), patterns[i].name);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->pattern), "разные"); /* у нескольких комнат */
    gtk_widget_set_tooltip_text(panel->pattern, "Как лежит плитка: прямая, шахматка, ёлочка, модульная из\n"
                                                "нескольких форматов. «Ёлочка по диагонали» — поворот 45°.");
    g_signal_connect(panel->pattern, "changed", G_CALLBACK(on_pattern), panel);
    app_section_row(s, "Укладка", panel->pattern);
    panel->pattern_note = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(panel->pattern_note), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(panel->pattern_note), 40);
    gtk_label_set_xalign(GTK_LABEL(panel->pattern_note), 0);
    gtk_widget_set_opacity(panel->pattern_note, 0.75);
    app_section_wide(s, panel->pattern_note);

    GtkWidget *length = app_hbox(4);
    gtk_box_pack_start(GTK_BOX(length), tile_spin(panel, "w", 10, 5000, 10, 0, "мм"), TRUE, TRUE, 0);
    GtkWidget *swap = small_button(panel, "⇄", G_CALLBACK(on_swap_tile), NULL, 0);
    gtk_widget_set_tooltip_text(swap, "Повернуть плитку на 90° (поменять стороны)");
    gtk_box_pack_start(GTK_BOX(length), swap, FALSE, FALSE, 0);
    app_section_row(s, "Длина плитки", length);
    app_section_row(s, "Ширина плитки", tile_spin(panel, "h", 10, 5000, 10, 0, "мм"));
    app_section_row(s, "Шов (зазор)", tile_spin(panel, "gap", 0, 50, 0.5, 1, "мм"));
    GtkWidget *chevron = tile_spin(panel, "pattern_angle", 15, 75, 5, 0, "°");
    gtk_widget_set_tooltip_text(chevron, "Угол среза торцов шеврона к длинной стороне (обычно 45°)");
    app_section_row(s, "Угол шеврона", chevron);
    GtkWidget *insert = tile_spin(panel, "insert", 10, 500, 5, 0, "мм");
    gtk_widget_set_tooltip_text(insert, "Сторона квадратной вставки (кабошона) в углах плиток");
    app_section_row(s, "Вставка (кабошон)", insert);

    panel->row_shift = gtk_combo_box_text_new();
    for (size_t i = 0; i < G_N_ELEMENTS(ROW_SHIFTS); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->row_shift), ROW_SHIFTS[i].title);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->row_shift), "разные"); /* у нескольких комнат */
    g_signal_connect(panel->row_shift, "changed", G_CALLBACK(on_row_shift), panel);
    app_section_row(s, "Сдвиг рядов", panel->row_shift);

    app_section_row(s, "Поворот", tile_spin(panel, "angle", -180, 180, 5, 1, "°"));
    GtkWidget *presets = app_hbox(4);
    const int preset_angles[3] = {0, 45, 90};
    for (int i = 0; i < 3; i++) {
        char text[8];
        g_snprintf(text, sizeof text, "%d°", preset_angles[i]);
        gtk_box_pack_start(GTK_BOX(presets),
                           small_button(panel, text, G_CALLBACK(on_rotation_preset), "angle", preset_angles[i]), TRUE,
                           TRUE, 0);
    }
    app_section_row(s, "", presets);
    app_section_row(s, "Смещение X", tile_spin(panel, "off_x", -100000, 100000, 5, 1, "мм"));
    app_section_row(s, "Смещение Y", tile_spin(panel, "off_y", -100000, 100000, 5, 1, "мм"));

    GtkWidget *corners = panel->corners = app_hbox(4);
    gtk_box_pack_start(GTK_BOX(corners), gtk_label_new("От угла:"), FALSE, FALSE, 0);
    const char *arrows[4] = {"↖", "↗", "↙", "↘"};
    const char *tips[4] = {"левом верхнем", "правом верхнем", "левом нижнем", "правом нижнем"};
    for (int c = 0; c < 4; c++) {
        GtkWidget *button = small_button(panel, arrows[c], G_CALLBACK(on_corner), "corner", c);
        char *tip = g_strdup_printf("Целая плитка в %s углу комнаты", tips[c]);
        gtk_widget_set_tooltip_text(button, tip);
        g_free(tip);
        gtk_box_pack_start(GTK_BOX(corners), button, TRUE, TRUE, 0);
    }
    app_section_wide(s, corners);
    GtkWidget *centers = panel->centers = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(centers), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(centers), 4);
    gtk_grid_set_row_spacing(GTK_GRID(centers), 4);
    gtk_grid_attach(GTK_GRID(centers), small_button(panel, "Центр — плитка", G_CALLBACK(on_center), "joint", 0), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(centers), small_button(panel, "Центр — шов", G_CALLBACK(on_center), "joint", 1), 1, 0, 1, 1);
    GtkWidget *narrow = small_button(panel, "Без узких подрезок", G_CALLBACK(on_optimize_narrow), NULL, 0);
    gtk_widget_set_tooltip_text(narrow, "Подобрать смещение с минимумом узких подрезок, затем — плиток");
    gtk_grid_attach(GTK_GRID(centers), narrow, 0, 1, 1, 1);
    GtkWidget *count = connect_button(app_icon_button_new("magic", "Минимум плиток"), G_CALLBACK(on_optimize_count), panel);
    gtk_widget_set_tooltip_text(count, "Подобрать смещение, при котором нужно меньше всего плиток к покупке\n"
                                       "(с учётом общей раскладки соседних комнат и остатков подрезок)");
    gtk_grid_attach(GTK_GRID(centers), count, 1, 1, 1, 1);
    app_section_wide(s, centers);
    GtkWidget *lines = panel->lines = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(lines), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(lines), 4);
    gtk_grid_set_row_spacing(GTK_GRID(lines), 4);
    panel->door_choice = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(panel->door_choice, "Двери комнаты — по именам на плане (Д1, Д2…)");
    gtk_grid_attach(GTK_GRID(lines), panel->door_choice, 0, 0, 1, 1);
    GtkWidget *door = small_button(panel, "От двери", G_CALLBACK(on_door), NULL, 0);
    gtk_widget_set_tooltip_text(door, "От стыка в проёме в комнату — целый ряд, проём — посередине плитки или шва\n"
                                      "(где куски у косяков шире)");
    gtk_grid_attach(GTK_GRID(lines), door, 1, 0, 2, 1);
    panel->ruler_choice = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(panel->ruler_choice, "Линейки в комнате — по именам на плане (Л1, Л2…):\n"
                                                     "ось ниши, унитаза, окна");
    gtk_grid_attach(GTK_GRID(lines), panel->ruler_choice, 0, 1, 1, 1);
    GtkWidget *joint = small_button(panel, "Шов", G_CALLBACK(on_ruler_line), "mode", TL_LINE_JOINT);
    gtk_widget_set_tooltip_text(joint, "По линейке пройдёт шов");
    gtk_grid_attach(GTK_GRID(lines), joint, 1, 1, 1, 1);
    GtkWidget *middle = small_button(panel, "Плитка", G_CALLBACK(on_ruler_line), "mode", TL_LINE_TILE_CENTER);
    gtk_widget_set_tooltip_text(middle, "По линейке — середина плитки");
    gtk_grid_attach(GTK_GRID(lines), middle, 2, 1, 1, 1);
    app_section_wide(s, lines);

    GtkWidget *gap = tile_spin(panel, "wall_gap", 0, 100, 1, 1, "мм");
    gtk_widget_set_tooltip_text(gap, "Плитка не доходит до стен и перегородок на это расстояние\n"
                                     "(компенсационный зазор; обычно закрывается плинтусом)");
    app_section_row(s, "Отступ от стены", gap);
    GtkWidget *min_cut = tile_spin(panel, "min_cut", 0, 1000, 5, 0, "мм");
    gtk_widget_set_tooltip_text(min_cut, "Самая узкая полоска плитки, которую нормально отрезать и уложить.\n"
                                         "Подрезки уже этого — «узкие»: подсвечиваются красным, считаются в\n"
                                         "расчёте; «Без узких подрезок» подбирает раскладку без них.");
    app_section_row(s, "Мин. ширина подрезки", min_cut);
    GtkWidget *reuse = gtk_check_button_new_with_label("Использовать остатки подрезок");
    gtk_widget_set_tooltip_text(reuse, "Остаток подрезанной плитки идёт в другую подрезку — в этой\n"
                                       "или любой другой комнате с той же плиткой, где тоже включено\n"
                                       "(учитывается в итоге)");
    bind_tile(panel, "reuse", reuse);
    app_section_wide(s, reuse);
    app_section_row(s, "Запас на рез", tile_spin(panel, "cut_allow", 0, 20, 0.5, 1, "мм"));
    app_section_row(s, "Запас", tile_spin(panel, "reserve", 0, 100, 1, 0, "%"));
    panel->color_button = gtk_color_button_new();
    g_object_set_data(G_OBJECT(panel->color_button), "key", "color");
    g_signal_connect(panel->color_button, "color-set", G_CALLBACK(on_color), panel);
    app_section_row(s, "Цвет", panel->color_button);
    panel->color2_button = gtk_color_button_new();
    g_object_set_data(G_OBJECT(panel->color2_button), "key", "color2");
    gtk_widget_set_tooltip_text(panel->color2_button, "Вторая плитка шахматки — отдельная позиция к покупке");
    g_signal_connect(panel->color2_button, "color-set", G_CALLBACK(on_color), panel);
    app_section_row(s, "Цвет второй плитки", panel->color2_button);

    panel->glue_section = app_section_new(box, "Клей");
    GtkWidget *rate = tile_spin(panel, "glue_rate", 0.1, 10, 0.05, 2, "кг/м²");
    gtk_widget_set_tooltip_text(rate, "Расход клея на 1 м² при толщине слоя 1 мм (см. упаковку)");
    app_section_row(&panel->glue_section, "Расход на 1 мм", rate);
    app_section_row(&panel->glue_section, "Толщина слоя", tile_spin(panel, "glue_mm", 0.5, 50, 0.5, 1, "мм"));
    app_section_row(&panel->glue_section, "Масса мешка", tile_spin(panel, "glue_bag", 1, 100, 1, 1, "кг"));

    panel->clip_section = app_section_new(box, "СВП (система выравнивания)");
    app_section_row(&panel->clip_section, "На длинную сторону", tile_spin(panel, "svp_long", 0, 20, 1, 0, "шт."));
    app_section_row(&panel->clip_section, "На короткую сторону", tile_spin(panel, "svp_short", 0, 20, 1, 0, "шт."));
    app_section_row(&panel->clip_section, "В упаковке", tile_spin(panel, "svp_pack", 1, 10000, 50, 0, "шт."));

    AppSection *grout = &panel->grout_section;
    *grout = app_section_new(box, "Затирка и грунт");
    GtkWidget *thickness = tile_spin(panel, "thickness", 1, 50, 0.5, 1, "мм");
    gtk_widget_set_tooltip_text(thickness, "Толщина плитки — глубина шва под затирку");
    app_section_row(grout, "Толщина плитки", thickness);
    GtkWidget *density = tile_spin(panel, "grout_density", 0.5, 3, 0.05, 2, "кг/дм³");
    gtk_widget_set_tooltip_text(density, "Плотность затирки: цементная — около 1,6, эпоксидная — 1,4–1,6 (см. упаковку)");
    app_section_row(grout, "Плотность затирки", density);
    app_section_row(grout, "Затирка в упаковке", tile_spin(panel, "grout_pack", 0.1, 100, 0.5, 1, "кг"));
    GtkWidget *primer = tile_spin(panel, "primer_rate", 0, 2, 0.05, 2, "л/м²");
    gtk_widget_set_tooltip_text(primer, "Расход грунта под плитку (см. канистру): обычно 0,1–0,3 л/м²");
    app_section_row(grout, "Расход грунта", primer);
    app_section_row(grout, "Грунт в канистре", tile_spin(panel, "primer_can", 0.5, 100, 0.5, 1, "л"));

    AppSection *prices = &panel->price_section;
    *prices = app_section_new(box, "Цены для сметы");
    GtkWidget *note = gtk_label_new("0 — позиция без цены, в смету не идёт.");
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_widget_set_opacity(note, 0.7);
    app_section_wide(prices, note);
    app_section_row(prices, "Плитка", tile_spin(panel, "price_tile", 0, 1e6, 50, 0, "₽/м²"));
    app_section_row(prices, "Клей", tile_spin(panel, "price_glue", 0, 1e6, 10, 0, "₽/мешок"));
    app_section_row(prices, "СВП", tile_spin(panel, "price_svp", 0, 1e6, 10, 0, "₽/уп."));
    app_section_row(prices, "Затирка", tile_spin(panel, "price_grout", 0, 1e6, 10, 0, "₽/уп."));
    app_section_row(prices, "Грунт", tile_spin(panel, "price_primer", 0, 1e6, 10, 0, "₽/кан."));
    app_section_row(prices, "Укладка", tile_spin(panel, "price_work", 0, 1e6, 50, 0, "₽/м²"));
}

static void build_calc(AppPanel *panel, GtkWidget *box)
{
    AppSection section = app_section_new(box, "Расчёт");
    panel->calc = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(panel->calc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(panel->calc), 0);
    gtk_label_set_selectable(GTK_LABEL(panel->calc), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(panel->calc), 40);
    app_section_wide(&section, panel->calc);
    app_section_wide(&section, connect_button(app_icon_button_new("print", "Печать / PDF…"), G_CALLBACK(on_print), panel));
}

/* ---- обновление ------------------------------------------------------------------- */

static void refresh_room_list(AppPanel *panel)
{
    TlProject *project = panel->document->project;
    GString *names = g_string_new(NULL);
    for (size_t i = 0; i < project->spaces.count; i++)
        g_string_append_printf(names, "%s\n", project->spaces.items[i]->name);
    if (!panel->room_names || strcmp(panel->room_names, names->str) != 0) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(panel->room_list));
        for (GList *child = children; child; child = child->next)
            gtk_widget_destroy(child->data);
        g_list_free(children);
        for (size_t i = 0; i < project->spaces.count; i++) {
            GtkWidget *label = gtk_label_new(project->spaces.items[i]->name);
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_list_box_insert(GTK_LIST_BOX(panel->room_list), label, -1);
        }
        gtk_widget_show_all(panel->room_list);
        g_free(panel->room_names);
        panel->room_names = g_strdup(names->str);
    }
    g_string_free(names, TRUE);
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = selected_spaces(panel, rooms);
    gtk_list_box_unselect_all(GTK_LIST_BOX(panel->room_list));
    for (size_t i = 0; i < project->spaces.count; i++)
        for (size_t k = 0; k < count; k++)
            if (project->spaces.items[i] == rooms[k])
                gtk_list_box_select_row(GTK_LIST_BOX(panel->room_list),
                                        gtk_list_box_get_row_at_index(GTK_LIST_BOX(panel->room_list), (int)i));
}

/* Несколько комнат: общие поля плитки, клея и СВП; смещение сетки и раскладка от угла —
   у каждой своя, недоступны. */
static void refresh_rooms(AppPanel *panel, TlSpace *const *rooms, size_t count)
{
    double area = 0;
    GString *names = g_string_new(NULL);
    for (size_t i = 0; i < count; i++) {
        area += tl_space_room_area(rooms[i]);
        g_string_append_printf(names, "%s«%s»", i ? ", " : "", rooms[i]->name);
    }
    char *note = g_strdup_printf("Выбрано комнат: %zu (%s), %.2f м². Изменения применяются ко всем. "
                                 "Shift+щелчок — добавить или убрать комнату.",
                                 count, names->str, area / 1e6);
    gtk_label_set_text(GTK_LABEL(panel->shared_note), note);
    gtk_widget_set_visible(panel->shared_note, TRUE);
    g_free(note);
    g_string_free(names, TRUE);
}

static void refresh_space(AppPanel *panel, TlSpace *space)
{
    TlProject *project = panel->document->project;
    gtk_entry_set_text(GTK_ENTRY(panel->space_name), space->name);
    set_label(panel->space_area, "%.2f м²", tl_space_room_area(space) / 1e6);
    set_label(panel->space_perimeter, "%.0f мм", tl_space_room_perimeter(space));
    GtkComboBoxText *floor = GTK_COMBO_BOX_TEXT(panel->floor_source);
    gtk_combo_box_text_remove_all(floor);
    gtk_combo_box_text_append(floor, "0", "Своя раскладка");
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *other = project->spaces.items[i];
        if (other == space || other->layout_source != other)
            continue;
        char id[16];
        g_snprintf(id, sizeof id, "%d", other->id);
        char *title = g_strdup_printf("Как в «%s» (общая сетка)", other->name);
        gtk_combo_box_text_append(floor, id, title);
        g_free(title);
    }
    char active[16];
    g_snprintf(active, sizeof active, "%d", space->source_id);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(floor), active))
        gtk_combo_box_set_active(GTK_COMBO_BOX(floor), 0);

    bool shared = tl_space_has_shared_floor(space);
    char *note = g_strdup_printf("Пол общий с «%s»: настройки плитки и клея общие.", space->layout_source->name);
    gtk_label_set_text(GTK_LABEL(panel->shared_note), shared ? note : "");
    gtk_widget_set_visible(panel->shared_note, shared);
    g_free(note);
}

/* Что значат поля плитки у рисунка: у модульного — клетка и форматы к покупке. */
static void refresh_pattern_note(AppPanel *panel, const TlTileSettings *tile, const TlPatternInfo *info)
{
    GString *note = g_string_new(NULL);
    TlPatternUnit unit;
    if (info->modular && tl_pattern_unit(tile, &unit)) {
        g_string_append(note, "Длина и ширина — клетка модуля. Форматы:");
        for (size_t i = 0; i < unit.spec_count; i++) {
            char name[64];
            tl_tile_spec_name(&unit.specs[i], name, sizeof name);
            g_string_append_printf(note, "%s %s", i ? "," : "", name);
        }
    } else if (strcmp(info->id, "herringbone") == 0) {
        g_string_append(note, "Длинная сторона — вдоль ряда. Ёлочка по диагонали комнаты — поворот 45°.");
    } else if (strcmp(info->id, "chevron") == 0) {
        g_string_append(note, "Длина — по длинной стороне плитки-параллелограмма, торцы срезаны под углом шеврона.");
    } else if (strcmp(info->id, "insert") == 0) {
        g_string_append(note, "Углы плитки срезаны под квадратную вставку (кабошон) — её размер и цвет ниже.");
    } else if (strcmp(info->id, "basket") == 0) {
        g_string_append(note, "Квадрат из нескольких плиток вдоль, рядом — поперёк. Ровно, если длина = n × ширина + швы.");
    } else if (info->uses_second_color) {
        g_string_append(note, "Плитки двух цветов через одну: вторая — отдельная позиция к покупке.");
    }
    gtk_label_set_text(GTK_LABEL(panel->pattern_note), note->str);
    gtk_widget_set_visible(panel->pattern_note, note->len > 0);
    g_string_free(note, TRUE);
}

static void set_color_button(GtkWidget *button, const char *color)
{
    GdkRGBA rgba;
    if (gdk_rgba_parse(&rgba, color))
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(button), &rgba);
}

/* Список имён: id — номер в проекте; выбранный остаётся выбранным, пока он есть. */
static void fill_choice(GtkWidget *choice, const char *const *names, const size_t *indices, size_t count)
{
    char *active = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(choice)));
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(choice));
    for (size_t i = 0; i < count; i++) {
        char id[24];
        g_snprintf(id, sizeof id, "%zu", indices[i]);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(choice), id, names[i]);
    }
    if (!active || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice), active))
        gtk_combo_box_set_active(GTK_COMBO_BOX(choice), count ? 0 : -1);
    gtk_widget_set_sensitive(choice, count > 0);
    g_free(active);
}

static size_t index_in(void *const *items, size_t count, const void *item)
{
    for (size_t i = 0; i < count; i++)
        if (items[i] == item)
            return i;
    return 0;
}

/* Двери и линейки комнаты — для раскладки от двери и по линейке. */
static void refresh_lines(AppPanel *panel, TlSpace *space)
{
    TlProject *project = panel->document->project;
    TlOpeningList doors = tl_project_space_doors(project, space);
    const char **names = g_new(const char *, doors.count + 1);
    size_t *indices = g_new(size_t, doors.count + 1);
    for (size_t i = 0; i < doors.count; i++) {
        names[i] = doors.items[i]->name;
        indices[i] = index_in((void *const *)project->openings.items, project->openings.count, doors.items[i]);
    }
    fill_choice(panel->door_choice, names, indices, doors.count);
    tl_opening_list_free(&doors);
    g_free(names);
    g_free(indices);

    TlRulerList rulers = tl_project_space_rulers(project, space);
    char **labels = g_new0(char *, rulers.count + 1);
    indices = g_new(size_t, rulers.count + 1);
    for (size_t i = 0; i < rulers.count; i++) {
        labels[i] = g_strdup_printf("%s (%.0f мм)", rulers.items[i]->name, tl_ruler_length(rulers.items[i]));
        indices[i] = index_in((void *const *)project->rulers.items, project->rulers.count, rulers.items[i]);
    }
    fill_choice(panel->ruler_choice, (const char *const *)labels, indices, rulers.count);
    tl_ruler_list_free(&rulers);
    g_strfreev(labels);
    g_free(indices);
}

static void refresh_tile(AppPanel *panel, TlSpace *const *rooms, size_t count)
{
    const TlTileSettings *tile = tl_space_effective_tile(rooms[0]);
    refresh_tile_bindings(panel, rooms, count);
    size_t pattern_count;
    const TlPatternInfo *patterns = tl_patterns(&pattern_count);
    const TlPatternInfo *pattern = tl_pattern_of(tile);
    bool same_pattern = true;
    for (size_t i = 1; i < count; i++)
        same_pattern = same_pattern && tl_pattern_of(tl_space_effective_tile(rooms[i])) == pattern;
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->pattern),
                             same_pattern ? (int)(pattern - patterns) : (int)pattern_count);
    refresh_pattern_note(panel, tile, pattern);
    gtk_widget_set_sensitive(panel->row_shift, !same_pattern || pattern->uses_row_shift);
    gtk_widget_set_sensitive(panel->color2_button, !same_pattern || pattern->uses_second_color);
    for (size_t i = 0; i < panel->tile_binding_count; i++) { /* поля, нужные только своему рисунку */
        const char *key = panel->tile_bindings[i].field->key;
        if (strcmp(key, "pattern_angle") == 0)
            gtk_widget_set_sensitive(panel->tile_bindings[i].widget, !same_pattern || strcmp(pattern->id, "chevron") == 0);
        if (strcmp(key, "insert") == 0)
            gtk_widget_set_sensitive(panel->tile_bindings[i].widget, !same_pattern || strcmp(pattern->id, "insert") == 0);
    }
    set_color_button(panel->color2_button, tile->color2);
    size_t shift_index = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(ROW_SHIFTS); i++)
        if (fabs(ROW_SHIFTS[i].value - tile->row_shift) < fabs(ROW_SHIFTS[shift_index].value - tile->row_shift))
            shift_index = i;
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->row_shift),
                             field_same(find_field("shift"), rooms, count) ? (int)shift_index
                                                                           : (int)G_N_ELEMENTS(ROW_SHIFTS));
    set_color_button(panel->color_button, tile->color);
    refresh_texture(panel, tile, !field_same(find_field("texture"), rooms, count));
    bool single = count == 1;
    for (size_t i = 0; i < panel->tile_binding_count; i++) {
        const char *key = panel->tile_bindings[i].field->key;
        if (strcmp(key, "off_x") == 0 || strcmp(key, "off_y") == 0)
            gtk_widget_set_sensitive(panel->tile_bindings[i].widget, single);
    }
    gtk_widget_set_sensitive(panel->corners, single);
    gtk_widget_set_sensitive(panel->centers, single);
    gtk_widget_set_sensitive(panel->lines, single);
    if (single)
        refresh_lines(panel, rooms[0]);
}

static void refresh_wall(AppPanel *panel, TlOutline *outline, size_t wall)
{
    char title[64];
    g_snprintf(title, sizeof title, "стена %zu из %zu", wall + 1, tl_outline_wall_count(outline));
    gtk_label_set_text(GTK_LABEL(panel->wall_number), title);
    set_spin(panel->wall_inner, tl_outline_inner_length(outline, wall));
    set_spin(panel->wall_outer, tl_outline_outer_length(outline, wall));
    set_label(panel->wall_axis, "%.1f мм", tl_outline_axis_length(outline, wall));
    set_spin(panel->wall_thickness, outline->thickness.items[wall]);
    set_label(panel->wall_angle, "%.1f°", tl_outline_wall_angle(outline, wall));
    set_check(panel->wall_lock, outline->locked.items[wall]);
    bool movable = !outline->locked.items[wall] && !outline->locked.items[(wall + 1) % tl_outline_wall_count(outline)];
    gtk_widget_set_sensitive(panel->wall_inner, movable);
    gtk_widget_set_sensitive(panel->wall_outer, movable && outline->thickness.items[wall] > 0);
}

static void refresh_partition_corner(AppPanel *panel, TlPartition *partition)
{
    TlPartitionCorner corner;
    bool selected = app_selection_partition_corner(&panel->document->selection, &corner);
    gtk_widget_set_visible(panel->partition_corner_box, selected);
    gtk_widget_set_visible(panel->partition_corner_hint, !selected);
    if (!selected)
        return;
    bool at_start = tl_partition_corner_end(corner) == TL_PARTITION_START;
    gtk_label_set_text(GTK_LABEL(panel->partition_corner_name), at_start ? "у начала перегородки" : "у конца перегородки");
    set_spin(panel->partition_corner_offset, partition->corner_offsets[corner]);
    gtk_widget_set_sensitive(panel->partition_corner_offset, !partition->locked);
    TlPolygon body = tl_project_partition_body(panel->document->project, partition, 0);
    char text[64];
    g_snprintf(text, sizeof text, "X %.1f   Y %.1f мм", body.items[corner].x, body.items[corner].y);
    gtk_label_set_text(GTK_LABEL(panel->partition_corner_position), text);
    tl_polygon_free(&body);
}

static void refresh_partition(AppPanel *panel, TlPartition *partition)
{
    set_spin(panel->partition_length, tl_partition_length(partition));
    set_spin(panel->partition_own_thickness, tl_partition_thickness_at(partition, TL_PARTITION_START));
    set_spin(panel->partition_angle, tl_partition_angle(partition));
    gtk_widget_set_sensitive(panel->partition_angle, !partition->locked);
    set_check(panel->partition_lock, partition->locked);
    gtk_widget_set_sensitive(panel->partition_length, !partition->locked);
    refresh_partition_corner(panel, partition);
}

static void refresh_opening(AppPanel *panel, TlOpening *opening)
{
    if (!gtk_widget_has_focus(panel->opening_name))
        gtk_entry_set_text(GTK_ENTRY(panel->opening_name), opening->name);
    TlBand band;
    bool hosted = tl_project_opening_host(panel->document->project, opening, &band);
    set_spin(panel->opening_own_width, opening->width);
    set_check(panel->opening_lock, opening->locked);
    if (!hosted) {
        gtk_label_set_markup(GTK_LABEL(panel->opening_host), "<span foreground=\"#d93025\">не в стене</span>");
    } else {
        double along;
        tl_band_local(&band, opening->center, &along, NULL);
        if (band.host.kind == TL_HOST_PARTITION)
            gtk_label_set_text(GTK_LABEL(panel->opening_host), "в перегородке");
        else
            set_label(panel->opening_host, "в наружной стене %.0f", (double)band.host.wall + 1);
        double start = 0, end = band.length;
        tl_project_opening_span(panel->document->project, opening, &start, &end);
        set_spin(panel->opening_from_start, along - opening->width / 2 - start);
        set_spin(panel->opening_to_end, end - along - opening->width / 2);
    }
    gtk_widget_set_sensitive(panel->opening_from_start, hosted && !opening->locked);
    gtk_widget_set_sensitive(panel->opening_to_end, hosted && !opening->locked);
    refresh_threshold(panel, opening);
}

static void refresh(AppPanel *panel)
{
    panel->updating = true;
    AppDocument *document = panel->document;
    const AppSelection *s = &document->selection;
    TlSpace *space = selected_space(panel);
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t room_count = selected_spaces(panel, rooms);
    TlOutline *outline = room_count > 1 ? NULL : app_selection_outline(s);
    refresh_room_list(panel);

    set_spin(panel->partition_thickness, document->preferences.partition_thickness);
    set_spin(panel->opening_width, document->preferences.opening_width);
    set_check(panel->link_walls, document->preferences.link_walls);
    set_check(panel->keep_angles, document->preferences.keep_angles);
    set_check(panel->measure_magnet, document->preferences.measure_magnet);
    set_spin(panel->expansion_joint_gap, document->preferences.expansion_joint_gap);
    set_spin(panel->pipe_diameter, document->preferences.pipe_diameter);
    set_check(panel->auto_align, document->preferences.auto_align);

    gtk_widget_set_visible(panel->space_section.frame, room_count == 1);
    gtk_widget_set_visible(panel->tile_section.frame, space != NULL);
    gtk_widget_set_visible(panel->glue_section.frame, space != NULL);
    gtk_widget_set_visible(panel->clip_section.frame, space != NULL);
    gtk_widget_set_visible(panel->grout_section.frame, space != NULL);
    gtk_widget_set_visible(panel->price_section.frame, space != NULL);
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++)
        gtk_widget_set_visible(panel->zone_sections[kind].frame, space != NULL);
    gtk_widget_set_visible(panel->pipe_section.frame, s->kind == APP_SELECT_PIPE);
    gtk_widget_set_visible(panel->outline_section.frame, outline != NULL);
    gtk_widget_set_visible(panel->wall_section.frame, s->kind == APP_SELECT_WALL);
    gtk_widget_set_visible(panel->partition_section.frame, s->kind == APP_SELECT_PARTITION);
    gtk_widget_set_visible(panel->opening_section.frame, s->kind == APP_SELECT_OPENING);
    gtk_widget_set_visible(panel->ruler_section.frame, s->kind == APP_SELECT_RULER);
    gtk_widget_set_visible(panel->node_section.frame, s->kind == APP_SELECT_NODE || s->kind == APP_SELECT_TEE);
    gtk_widget_set_visible(panel->vertex_section.frame, s->kind == APP_SELECT_VERTEX);
    gtk_widget_set_visible(panel->expansion_joint_section.frame, s->kind == APP_SELECT_EXPANSION_JOINT);
    gtk_widget_set_visible(panel->hint, s->kind == APP_SELECT_NONE);

    if (room_count == 1)
        refresh_space(panel, space);
    else if (room_count > 1)
        refresh_rooms(panel, rooms, room_count);
    if (room_count > 0) {
        refresh_tile(panel, rooms, room_count);
        refresh_zones(panel, rooms[0]);
    }
    if (outline) {
        set_label(panel->outline_area, "%.2f м²", tl_outline_area(outline) / 1e6);
        set_spin(panel->outline_thickness, document->preferences.outline_thickness);
    }
    if (s->kind == APP_SELECT_WALL)
        refresh_wall(panel, s->outline, (size_t)s->index);
    if (s->kind == APP_SELECT_PARTITION)
        refresh_partition(panel, s->partition);
    if (s->kind == APP_SELECT_OPENING)
        refresh_opening(panel, s->opening);
    if (s->kind == APP_SELECT_NODE || s->kind == APP_SELECT_TEE)
        refresh_node(panel);
    if (s->kind == APP_SELECT_VERTEX) {
        set_spin(panel->vertex_x, s->outline->inner.items[s->index].x);
        set_spin(panel->vertex_y, s->outline->inner.items[s->index].y);
    }
    if (s->kind == APP_SELECT_RULER) {
        if (!gtk_widget_has_focus(panel->ruler_name))
            gtk_entry_set_text(GTK_ENTRY(panel->ruler_name), s->ruler->name);
        set_label(panel->ruler_length, "%.0f мм", tl_ruler_length(s->ruler));
        set_label(panel->ruler_count, "%.0f", (double)document->project->rulers.count);
    }
    if (s->kind == APP_SELECT_PIPE) {
        set_spin(panel->pipe_own_diameter, s->pipe->diameter);
        set_spin(panel->pipe_x, s->pipe->center.x);
        set_spin(panel->pipe_y, s->pipe->center.y);
    }
    if (s->kind == APP_SELECT_EXPANSION_JOINT) {
        set_spin(panel->expansion_joint_own_gap, s->expansion_joint->gap);
        set_label(panel->expansion_joint_length, "%.0f мм", tl_expansion_joint_length(s->expansion_joint));
    }

    char *markup = app_calc_markup(document->project, room_count == 1 ? space : NULL);
    gtk_label_set_markup(GTK_LABEL(panel->calc), markup);
    g_free(markup);
    panel->updating = false;
}

static void on_document_changed(AppDocument *document, unsigned changes, void *data)
{
    if (changes & (APP_CHANGED_MODEL | APP_CHANGED_SELECTION))
        refresh(data);
}

static void hide_until_needed(AppSection *section) { gtk_widget_set_no_show_all(section->frame, TRUE); }

AppPanel *app_panel_new(AppDocument *document, AppCanvas *canvas, AppPanelActions actions)
{
    AppPanel *panel = g_new0(AppPanel, 1);
    panel->document = document;
    panel->canvas = canvas;
    panel->actions = actions;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    build_rooms(panel, box);
    build_tools(panel, box);
    panel->hint = gtk_label_new("Выберите комнату, стену, перегородку, проём, шов, трубу или линейку.");
    gtk_label_set_line_wrap(GTK_LABEL(panel->hint), TRUE);
    gtk_widget_set_no_show_all(panel->hint, TRUE);
    gtk_box_pack_start(GTK_BOX(box), panel->hint, FALSE, FALSE, 0);
    build_space(panel, box);
    build_outline(panel, box);
    build_wall(panel, box);
    build_partition(panel, box);
    build_opening(panel, box);
    build_expansion_joint(panel, box);
    build_pipe(panel, box);
    build_node(panel, box);
    build_vertex(panel, box);
    build_ruler(panel, box);
    build_tile(panel, box);
    build_zone(panel, box, TL_ZONE_BORDER);
    build_zone(panel, box, TL_ZONE_CARPET);
    build_calc(panel, box);
    AppSection *conditional[] = {&panel->space_section, &panel->outline_section, &panel->wall_section,
                                 &panel->partition_section, &panel->opening_section, &panel->ruler_section,
                                 &panel->expansion_joint_section, &panel->pipe_section,
                                 &panel->tile_section,
                                 &panel->glue_section, &panel->clip_section, &panel->grout_section,
                                 &panel->price_section, &panel->zone_sections[TL_ZONE_BORDER],
                                 &panel->zone_sections[TL_ZONE_CARPET]};
    for (size_t i = 0; i < G_N_ELEMENTS(conditional); i++) {
        gtk_widget_show_all(conditional[i]->frame);
        hide_until_needed(conditional[i]);
    }
    GtkWidget *hint = gtk_label_new("F1 — управление");
    gtk_widget_set_opacity(hint, 0.6);
    gtk_box_pack_end(GTK_BOX(box), hint, FALSE, FALSE, 0);

    panel->root = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(panel->root), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(panel->root, 380, -1);
    gtk_container_add(GTK_CONTAINER(panel->root), box);
    app_document_add_listener(document, on_document_changed, panel);
    refresh(panel);
    return panel;
}

GtkWidget *app_panel_widget(AppPanel *panel) { return panel->root; }

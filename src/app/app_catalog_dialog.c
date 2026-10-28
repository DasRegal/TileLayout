#include "app_catalog_dialog.h"

#include <string.h>

#include "app_catalogs.h"
#include "app_commands.h"

#define THUMBNAIL_SIZE 112
#define DIALOG_WIDTH 820
#define DIALOG_HEIGHT 600

enum { RESPONSE_COLOR_ONLY = 1, RESPONSE_OPEN_FOLDER, RESPONSE_RELOAD };

/* Столбцы дерева слева: подпись, каталог (NULL — все), группа внутри каталога. */
enum { COLUMN_LABEL, COLUMN_CATALOG, COLUMN_GROUP, COLUMN_COUNT };

typedef struct Picker {
    GtkWidget *dialog, *catalogs, *tiles, *search, *choose;
    GtkTreeStore *tree;
    const TlCatalog *catalog; /* NULL — все каталоги */
    char *group;              /* группа внутри каталога; пусто — весь каталог */
} Picker;

static void destroy_child(GtkWidget *child, gpointer data)
{
    (void)data;
    gtk_widget_destroy(child);
}

/* ---- плитки ---------------------------------------------------------------------- */

static GtkWidget *tile_card(const TlCatalog *catalog, const TlCatalogTile *tile)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(box, THUMBNAIL_SIZE + 16, -1);
    GdkPixbuf *thumbnail = app_catalogs_thumbnail(catalog, tile, THUMBNAIL_SIZE);
    GtkWidget *picture = thumbnail ? gtk_image_new_from_pixbuf(thumbnail)
                                   : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DIALOG);
    if (thumbnail)
        g_object_unref(thumbnail);
    gtk_widget_set_size_request(picture, THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    gtk_box_pack_start(GTK_BOX(box), picture, FALSE, FALSE, 0);
    GtkWidget *name = gtk_label_new(tile->name);
    gtk_label_set_line_wrap(GTK_LABEL(name), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(name), 16);
    gtk_label_set_justify(GTK_LABEL(name), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
    char *size = g_strdup_printf("<small>%g × %g мм</small>", tile->width, tile->height);
    GtkWidget *size_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(size_label), size);
    g_free(size);
    gtk_box_pack_start(GTK_BOX(box), size_label, FALSE, FALSE, 0);
    char *where = tile->group[0] ? g_strdup_printf("%s / %s", catalog->name, tile->group) : g_strdup(catalog->name);
    gtk_widget_set_tooltip_text(box, where);
    g_free(where);
    return box;
}

/* Плитка в карточке: каталог и плитка — в данных дочернего элемента. */
static bool card_tile(GtkFlowBoxChild *child, const TlCatalog **catalog, const TlCatalogTile **tile)
{
    *catalog = g_object_get_data(G_OBJECT(child), "catalog");
    *tile = g_object_get_data(G_OBJECT(child), "tile");
    return *catalog && *tile;
}

/* Видна ли карточка: каталог или группа выбраны слева (или все), название или размер — в поиске. */
static gboolean show_card(GtkFlowBoxChild *child, gpointer data)
{
    Picker *picker = data;
    const TlCatalog *catalog;
    const TlCatalogTile *tile;
    if (!card_tile(child, &catalog, &tile))
        return FALSE;
    if (picker->catalog && (picker->catalog != catalog || !tl_catalog_tile_in_group(tile, picker->group)))
        return FALSE;
    const char *query = gtk_entry_get_text(GTK_ENTRY(picker->search));
    if (!query[0])
        return TRUE;
    char *size = g_strdup_printf("%g×%g %gx%g", tile->width, tile->height, tile->width, tile->height);
    char *haystack = g_utf8_casefold(tile->name, -1), *needle = g_utf8_casefold(query, -1);
    bool found = strstr(haystack, needle) || strstr(size, query);
    g_free(size);
    g_free(haystack);
    g_free(needle);
    return found;
}

static void fill_tiles(Picker *picker, const char *current)
{
    gtk_container_foreach(GTK_CONTAINER(picker->tiles), destroy_child, NULL);
    const TlCatalogList *list = app_catalogs();
    for (size_t c = 0; c < list->count; c++)
        for (size_t t = 0; t < list->items[c]->tiles.count; t++) {
            const TlCatalogTile *tile = &list->items[c]->tiles.items[t];
            GtkWidget *child = gtk_flow_box_child_new();
            gtk_container_add(GTK_CONTAINER(child), tile_card(list->items[c], tile));
            g_object_set_data(G_OBJECT(child), "catalog", list->items[c]);
            g_object_set_data(G_OBJECT(child), "tile", (gpointer)tile);
            gtk_container_add(GTK_CONTAINER(picker->tiles), child);
            char reference[TL_TEXTURE_LENGTH];
            tl_catalog_reference(list->items[c], tile, reference, sizeof reference);
            if (current && strcmp(reference, current) == 0)
                gtk_flow_box_select_child(GTK_FLOW_BOX(picker->tiles), GTK_FLOW_BOX_CHILD(child));
        }
    gtk_widget_show_all(picker->tiles);
}

/* ---- каталоги и группы слева ---------------------------------------------------- */

static size_t tiles_in(const TlCatalog *catalog, const char *group)
{
    size_t count = 0;
    for (size_t t = 0; t < catalog->tiles.count; t++)
        count += tl_catalog_tile_in_group(&catalog->tiles.items[t], group);
    return count;
}

static void add_row(Picker *picker, GtkTreeIter *row, GtkTreeIter *parent, const char *name, size_t count,
                    const TlCatalog *catalog, const char *group)
{
    char *label = g_strdup_printf("%s  (%zu)", name, count);
    gtk_tree_store_append(picker->tree, row, parent);
    gtk_tree_store_set(picker->tree, row, COLUMN_LABEL, label, COLUMN_CATALOG, catalog, COLUMN_GROUP, group, -1);
    g_free(label);
}

/* Строка группы group каталога (и строки всех групп над ней) — из rows или новая. */
static GtkTreeIter group_row(Picker *picker, GHashTable *rows, GtkTreeIter *catalog_row, const TlCatalog *catalog,
                             const char *group)
{
    GtkTreeIter *known = g_hash_table_lookup(rows, group);
    if (known)
        return *known;
    const char *slash = strrchr(group, '/');
    GtkTreeIter parent = *catalog_row;
    if (slash) {
        char *upper = g_strndup(group, (gsize)(slash - group));
        parent = group_row(picker, rows, catalog_row, catalog, upper);
        g_free(upper);
    }
    GtkTreeIter row;
    add_row(picker, &row, &parent, slash ? slash + 1 : group, tiles_in(catalog, group), catalog, group);
    g_hash_table_insert(rows, g_strdup(group), gtk_tree_iter_copy(&row));
    return row;
}

static void fill_catalogs(Picker *picker)
{
    gtk_tree_store_clear(picker->tree);
    const TlCatalogList *list = app_catalogs();
    size_t total = 0;
    for (size_t c = 0; c < list->count; c++)
        total += list->items[c]->tiles.count;
    GtkTreeIter all;
    add_row(picker, &all, NULL, "Все каталоги", total, NULL, "");
    for (size_t c = 0; c < list->count; c++) {
        const TlCatalog *catalog = list->items[c];
        GtkTreeIter catalog_row;
        add_row(picker, &catalog_row, &all, catalog->name, catalog->tiles.count, catalog, "");
        GHashTable *rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)gtk_tree_iter_free);
        for (size_t t = 0; t < catalog->tiles.count; t++)
            if (catalog->tiles.items[t].group[0])
                group_row(picker, rows, &catalog_row, catalog, catalog->tiles.items[t].group);
        g_hash_table_destroy(rows);
    }
    /* Раскрыты каталоги — видны их группы верхнего уровня (фирмы и т. п.). */
    GtkTreeView *view = GTK_TREE_VIEW(picker->catalogs);
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(picker->tree), &all);
    gtk_tree_view_expand_row(view, path, FALSE);
    for (size_t c = 0; c < list->count; c++) {
        gtk_tree_path_append_index(path, (int)c);
        gtk_tree_view_expand_row(view, path, FALSE);
        gtk_tree_path_up(path);
    }
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), path);
    gtk_tree_path_free(path);
}

static void on_catalog_selected(GtkTreeSelection *selection, Picker *picker)
{
    GtkTreeModel *model;
    GtkTreeIter row;
    g_clear_pointer(&picker->group, g_free);
    picker->catalog = NULL;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
        gtk_tree_model_get(model, &row, COLUMN_CATALOG, &picker->catalog, COLUMN_GROUP, &picker->group, -1);
    gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(picker->tiles));
}

static void on_search_changed(GtkSearchEntry *entry, Picker *picker)
{
    (void)entry;
    gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(picker->tiles));
}

static void on_tile_selected(GtkFlowBox *box, Picker *picker)
{
    GList *selected = gtk_flow_box_get_selected_children(box);
    gtk_widget_set_sensitive(picker->choose, selected != NULL);
    g_list_free(selected);
}

static void on_tile_activated(GtkFlowBox *box, GtkFlowBoxChild *child, Picker *picker)
{
    (void)box;
    (void)child;
    gtk_dialog_response(GTK_DIALOG(picker->dialog), GTK_RESPONSE_ACCEPT);
}

/* ---- окно ------------------------------------------------------------------------- */

static GtkWidget *scrolled(GtkWidget *child, GtkPolicyType horizontal)
{
    GtkWidget *window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window), horizontal, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(window), child);
    return window;
}

static void build(Picker *picker, GtkWindow *parent)
{
    picker->dialog = gtk_dialog_new_with_buttons("Плитка из каталога", parent, GTK_DIALOG_MODAL,
                                                 "Папка своих каталогов", RESPONSE_OPEN_FOLDER,
                                                 "Обновить", RESPONSE_RELOAD, "Только цвет", RESPONSE_COLOR_ONLY,
                                                 "Отмена", GTK_RESPONSE_CANCEL, "Выбрать", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(picker->dialog), DIALOG_WIDTH, DIALOG_HEIGHT);
    gtk_dialog_set_default_response(GTK_DIALOG(picker->dialog), GTK_RESPONSE_ACCEPT);
    picker->choose = gtk_dialog_get_widget_for_response(GTK_DIALOG(picker->dialog), GTK_RESPONSE_ACCEPT);
    gtk_widget_set_tooltip_text(gtk_dialog_get_widget_for_response(GTK_DIALOG(picker->dialog), RESPONSE_COLOR_ONLY),
                                "Убрать рисунок: плитка — просто цветом");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(picker->dialog));
    gtk_box_set_spacing(GTK_BOX(content), 6);
    picker->search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(picker->search), "Название или размер, например 600×300");
    g_signal_connect(picker->search, "search-changed", G_CALLBACK(on_search_changed), picker);
    gtk_box_pack_start(GTK_BOX(content), picker->search, FALSE, FALSE, 0);

    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    picker->tree = gtk_tree_store_new(COLUMN_COUNT, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING);
    picker->catalogs = gtk_tree_view_new_with_model(GTK_TREE_MODEL(picker->tree));
    g_object_unref(picker->tree); /* модель живёт с деревом */
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(picker->catalogs), FALSE);
    GtkCellRenderer *text = gtk_cell_renderer_text_new();
    g_object_set(text, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(picker->catalogs),
                                gtk_tree_view_column_new_with_attributes("", text, "text", COLUMN_LABEL, NULL));
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(picker->catalogs)), "changed",
                     G_CALLBACK(on_catalog_selected), picker);
    GtkWidget *left = scrolled(picker->catalogs, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(left, 240, -1);
    gtk_paned_pack1(GTK_PANED(panes), left, FALSE, FALSE);
    picker->tiles = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(picker->tiles), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(picker->tiles), FALSE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(picker->tiles), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(picker->tiles), 12);
    gtk_flow_box_set_filter_func(GTK_FLOW_BOX(picker->tiles), show_card, picker, NULL);
    g_signal_connect(picker->tiles, "selected-children-changed", G_CALLBACK(on_tile_selected), picker);
    g_signal_connect(picker->tiles, "child-activated", G_CALLBACK(on_tile_activated), picker);
    gtk_paned_pack2(GTK_PANED(panes), scrolled(picker->tiles, GTK_POLICY_NEVER), TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(content), panes, TRUE, TRUE, 0);

    GtkWidget *note = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(note), "<small>Свои плитки: папка с картинками «Название 600x300.jpg» внутри "
                                          "папки своих каталогов — каждая папка становится каталогом, вложенные "
                                          "папки (фирма, формат) — его разделами. Подробнее — в README.</small>");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 0);
}

static void open_user_folder(GtkWindow *parent)
{
    char *uri = g_filename_to_uri(app_catalogs_user_directory(), NULL, NULL);
    if (uri)
        gtk_show_uri_on_window(parent, uri, GDK_CURRENT_TIME, NULL);
    g_free(uri);
}

/* Выбранное — основному полу (zone == TL_ZONE_COUNT) или зоне (app_space_zone). */
static void apply_choice(AppDocument *document, int zone, const TlCatalog *catalog, const TlCatalogTile *tile)
{
    if (zone == APP_ZONE_PANNO)
        app_command_set_panno_texture(document, catalog, tile);
    else if (zone == TL_ZONE_COUNT)
        app_command_set_tile_texture(document, catalog, tile);
    else
        app_command_set_zone_texture(document, zone, catalog, tile);
}

static void run_dialog(GtkWindow *parent, AppDocument *document, int zone)
{
    TlSpace *rooms[APP_MAX_ROOMS];
    TlSpace *space = app_selection_spaces(&document->selection, rooms) ? rooms[0] : NULL;
    if (!space) {
        app_document_status(document, "Выделите комнату — плитка выбирается для неё");
        return;
    }
    Picker picker = {0};
    build(&picker, parent);
    const char *current = zone == APP_ZONE_PANNO ? space->pannos.items[document->selection.index].texture
                          : zone == TL_ZONE_COUNT ? tl_space_effective_tile(space)->texture
                                                  : app_space_zone(space, zone)->tile.texture;
    fill_catalogs(&picker);
    fill_tiles(&picker, current);
    on_tile_selected(GTK_FLOW_BOX(picker.tiles), &picker);
    gtk_widget_show_all(picker.dialog);
    for (;;) {
        int response = gtk_dialog_run(GTK_DIALOG(picker.dialog));
        if (response == RESPONSE_OPEN_FOLDER) {
            open_user_folder(GTK_WINDOW(picker.dialog));
            continue;
        }
        if (response == RESPONSE_RELOAD) { /* добавили свои картинки — перечитать, не закрывая */
            app_catalogs_reload();
            fill_catalogs(&picker);
            fill_tiles(&picker, current);
            continue;
        }
        if (response == RESPONSE_COLOR_ONLY) {
            apply_choice(document, zone, NULL, NULL);
        } else if (response == GTK_RESPONSE_ACCEPT) {
            GList *selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(picker.tiles));
            const TlCatalog *catalog;
            const TlCatalogTile *tile;
            if (selected && card_tile(selected->data, &catalog, &tile))
                apply_choice(document, zone, catalog, tile);
            g_list_free(selected);
        }
        break;
    }
    g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(picker.catalogs)), &picker);
    gtk_widget_destroy(picker.dialog);
    g_free(picker.group);
}

void app_catalog_dialog_for_selection(GtkWindow *parent, AppDocument *document)
{
    run_dialog(parent, document, TL_ZONE_COUNT);
}

void app_catalog_dialog_for_zone(GtkWindow *parent, AppDocument *document, int zone)
{
    run_dialog(parent, document, zone);
}

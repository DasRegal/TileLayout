#include "app_catalog_dialog.h"

#include <string.h>

#include "app_catalogs.h"
#include "app_commands.h"

#define THUMBNAIL_SIZE 112
#define MAX_CARDS 240       /* карточек за раз: в больших каталогах — десятки тысяч плиток */
#define THUMBNAILS_PER_IDLE 6
#define DIALOG_WIDTH 820
#define DIALOG_HEIGHT 600

enum { RESPONSE_COLOR_ONLY = 1, RESPONSE_OPEN_FOLDER, RESPONSE_RELOAD };

/* Столбцы дерева слева: подпись, каталог (NULL — все), группа внутри каталога. */
enum { COLUMN_LABEL, COLUMN_CATALOG, COLUMN_GROUP, COLUMN_COUNT };

typedef struct Picker {
    GtkWidget *dialog, *catalogs, *tiles, *search, *choose, *shown;
    GtkTreeStore *tree;
    const TlCatalog *catalog; /* NULL — все каталоги */
    char *group;              /* группа внутри каталога; пусто — весь каталог */
    const char *current;      /* ссылка на плитку комнаты — выделить её */
    GPtrArray *pending;       /* карточки без рисунка: рисунки грузятся понемногу, окно не замирает */
    guint loader;
} Picker;

static void destroy_child(GtkWidget *child, gpointer data)
{
    (void)data;
    gtk_widget_destroy(child);
}

/* ---- плитки ---------------------------------------------------------------------- */

/* Карточка — сразу без рисунка (он грузится потом, load_thumbnails); picture — его место. */
static GtkWidget *tile_card(const TlCatalog *catalog, const TlCatalogTile *tile, GtkWidget **picture_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(box, THUMBNAIL_SIZE + 16, -1);
    GtkWidget *picture = *picture_out = gtk_image_new();
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

/* Плитка в выбранном слева (каталог, раздел или все) и в поиске (название или размер; needle — casefold). */
/* Подходит ли плитка: в выбранном слева разделе и каждое слово запроса есть в каталоге,
 * производителе/коллекции (папках), названии или размере (мм или см: «600×300», «60x30»). */
static bool tile_matches(const Picker *picker, const TlCatalog *catalog, const TlCatalogTile *tile,
                         char *const *words)
{
    if (picker->catalog && (picker->catalog != catalog || !tl_catalog_tile_in_group(tile, picker->group)))
        return false;
    if (!words[0])
        return true;
    char *text = g_strdup_printf("%s/%s/%s %g×%g %gx%g %gх%g %g×%g %gx%g %gх%g", catalog->name, tile->group,
                                 tile->name, tile->width, tile->height, tile->width, tile->height, tile->width,
                                 tile->height, tile->width / 10, tile->height / 10, tile->width / 10,
                                 tile->height / 10, tile->width / 10, tile->height / 10);
    char *haystack = g_utf8_casefold(text, -1);
    bool found = true;
    for (size_t i = 0; found && words[i]; i++)
        found = strstr(haystack, words[i]) != NULL;
    g_free(haystack);
    g_free(text);
    return found;
}

/* Рисунок карточки (виджет и плитка — в данных картинки). */
typedef struct PendingCard {
    GtkWidget *picture;
    const TlCatalog *catalog;
    const TlCatalogTile *tile;
} PendingCard;

static void stop_loader(Picker *picker)
{
    if (picker->loader)
        g_source_remove(picker->loader);
    picker->loader = 0;
    if (picker->pending)
        g_ptr_array_set_size(picker->pending, 0);
}

/* Несколько рисунков за раз, между ними окно отвечает. */
static gboolean load_thumbnails(gpointer data)
{
    Picker *picker = data;
    for (int n = 0; n < THUMBNAILS_PER_IDLE && picker->pending->len; n++) {
        PendingCard *card = g_ptr_array_index(picker->pending, 0);
        GdkPixbuf *thumbnail = app_catalogs_thumbnail(card->catalog, card->tile, THUMBNAIL_SIZE);
        if (thumbnail) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(card->picture), thumbnail);
            g_object_unref(thumbnail);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(card->picture), "image-missing", GTK_ICON_SIZE_DIALOG);
        }
        g_ptr_array_remove_index(picker->pending, 0);
    }
    if (picker->pending->len)
        return G_SOURCE_CONTINUE;
    picker->loader = 0;
    return G_SOURCE_REMOVE;
}

/* Карточки выбранного слева и найденного — не больше MAX_CARDS (в большом каталоге — десятки тысяч). */
static void fill_tiles(Picker *picker)
{
    stop_loader(picker);
    gtk_container_foreach(GTK_CONTAINER(picker->tiles), destroy_child, NULL);
    const TlCatalogList *list = app_catalogs();
    const char *query = gtk_entry_get_text(GTK_ENTRY(picker->search));
    char *needle = g_utf8_casefold(query, -1);
    char **words = g_strsplit_set(g_strstrip(needle), " \t", -1);
    size_t kept = 0; /* без пустых слов от двойных пробелов */
    for (size_t i = 0; words[i]; i++)
        if (words[i][0])
            words[kept++] = words[i];
        else
            g_free(words[i]);
    words[kept] = NULL;
    size_t found = 0;
    for (size_t c = 0; c < list->count; c++)
        for (size_t t = 0; t < list->items[c]->tiles.count; t++) {
            const TlCatalogTile *tile = &list->items[c]->tiles.items[t];
            if (!tile_matches(picker, list->items[c], tile, words) || found++ >= MAX_CARDS)
                continue;
            GtkWidget *child = gtk_flow_box_child_new(), *picture;
            gtk_container_add(GTK_CONTAINER(child), tile_card(list->items[c], tile, &picture));
            g_object_set_data(G_OBJECT(child), "catalog", list->items[c]);
            g_object_set_data(G_OBJECT(child), "tile", (gpointer)tile);
            gtk_container_add(GTK_CONTAINER(picker->tiles), child);
            PendingCard *card = g_new(PendingCard, 1);
            *card = (PendingCard){picture, list->items[c], tile};
            g_ptr_array_add(picker->pending, card);
            char reference[TL_TEXTURE_LENGTH];
            tl_catalog_reference(list->items[c], tile, reference, sizeof reference);
            if (picker->current && strcmp(reference, picker->current) == 0)
                gtk_flow_box_select_child(GTK_FLOW_BOX(picker->tiles), GTK_FLOW_BOX_CHILD(child));
        }
    g_strfreev(words);
    g_free(needle);
    gtk_widget_show_all(picker->tiles);
    if (picker->pending->len)
        picker->loader = g_idle_add(load_thumbnails, picker);
    char *text = found > MAX_CARDS ? g_strdup_printf("Показано %d из %zu — выберите раздел слева или уточните поиск.",
                                                     MAX_CARDS, found)
                 : found == 0      ? g_strdup("Ничего не найдено.")
                                   : NULL;
    gtk_label_set_text(GTK_LABEL(picker->shown), text ? text : "");
    gtk_widget_set_visible(picker->shown, text != NULL);
    g_free(text);
}

/* ---- каталоги и группы слева ---------------------------------------------------- */

/* Плиток в каждой группе каталога вместе с вложенными: группа → число (за один проход). */
static GHashTable *count_groups(const TlCatalog *catalog)
{
    GHashTable *counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (size_t t = 0; t < catalog->tiles.count; t++) {
        char *group = g_strdup(catalog->tiles.items[t].group);
        while (group[0]) { /* «Фирма/60×120», затем «Фирма» */
            gpointer count = g_hash_table_lookup(counts, group);
            g_hash_table_insert(counts, g_strdup(group), GSIZE_TO_POINTER(GPOINTER_TO_SIZE(count) + 1));
            char *slash = strrchr(group, '/');
            if (slash)
                *slash = '\0';
            else
                group[0] = '\0';
        }
        g_free(group);
    }
    return counts;
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
static GtkTreeIter group_row(Picker *picker, GHashTable *rows, GHashTable *counts, GtkTreeIter *catalog_row,
                             const TlCatalog *catalog, const char *group)
{
    GtkTreeIter *known = g_hash_table_lookup(rows, group);
    if (known)
        return *known;
    const char *slash = strrchr(group, '/');
    GtkTreeIter parent = *catalog_row;
    if (slash) {
        char *upper = g_strndup(group, (gsize)(slash - group));
        parent = group_row(picker, rows, counts, catalog_row, catalog, upper);
        g_free(upper);
    }
    GtkTreeIter row;
    add_row(picker, &row, &parent, slash ? slash + 1 : group, GPOINTER_TO_SIZE(g_hash_table_lookup(counts, group)),
            catalog, group);
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
        GHashTable *counts = count_groups(catalog);
        for (size_t t = 0; t < catalog->tiles.count; t++)
            if (catalog->tiles.items[t].group[0])
                group_row(picker, rows, counts, &catalog_row, catalog, catalog->tiles.items[t].group);
        g_hash_table_destroy(counts);
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
    fill_tiles(picker);
}

static void on_search_changed(GtkSearchEntry *entry, Picker *picker)
{
    (void)entry;
    fill_tiles(picker);
}

/* Плитка комнаты — открыть её раздел (в большом каталоге среди первых карточек её может не быть). */
typedef struct FindRow {
    const TlCatalog *catalog;
    const char *group;
    GtkTreePath *path;
} FindRow;

static gboolean find_group_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *row, gpointer data)
{
    FindRow *find = data;
    const TlCatalog *catalog;
    char *group;
    gtk_tree_model_get(model, row, COLUMN_CATALOG, &catalog, COLUMN_GROUP, &group, -1);
    bool same = catalog == find->catalog && group && strcmp(group, find->group) == 0;
    g_free(group);
    if (same)
        find->path = gtk_tree_path_copy(path);
    return same;
}

static void select_current_group(Picker *picker)
{
    const TlCatalog *catalog = NULL;
    const TlCatalogTile *tile = picker->current && picker->current[0]
                                    ? tl_catalog_find(app_catalogs(), picker->current, &catalog)
                                    : NULL;
    if (!tile)
        return;
    FindRow find = {catalog, tile->group, NULL};
    gtk_tree_model_foreach(GTK_TREE_MODEL(picker->tree), find_group_row, &find);
    if (!find.path)
        return;
    GtkTreeView *view = GTK_TREE_VIEW(picker->catalogs);
    gtk_tree_view_expand_to_path(view, find.path);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), find.path);
    gtk_tree_view_scroll_to_cell(view, find.path, NULL, TRUE, 0.3f, 0);
    gtk_tree_path_free(find.path);
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
    gtk_entry_set_placeholder_text(GTK_ENTRY(picker->search), "Производитель, коллекция, слово из названия или размер (600×300)");
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
    g_signal_connect(picker->tiles, "selected-children-changed", G_CALLBACK(on_tile_selected), picker);
    g_signal_connect(picker->tiles, "child-activated", G_CALLBACK(on_tile_activated), picker);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    picker->shown = gtk_label_new(NULL); /* в большом каталоге показаны не все — подсказка */
    gtk_label_set_xalign(GTK_LABEL(picker->shown), 0);
    gtk_label_set_line_wrap(GTK_LABEL(picker->shown), TRUE);
    gtk_widget_set_no_show_all(picker->shown, TRUE);
    g_object_set(picker->shown, "margin-start", 6, NULL);
    gtk_box_pack_start(GTK_BOX(right), picker->shown, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), scrolled(picker->tiles, GTK_POLICY_NEVER), TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(panes), right, TRUE, FALSE);
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
    Picker picker = {.pending = g_ptr_array_new_with_free_func(g_free)};
    build(&picker, parent);
    picker.current = zone == APP_ZONE_PANNO ? space->pannos.items[document->selection.index].texture
                     : zone == TL_ZONE_COUNT ? tl_space_effective_tile(space)->texture
                                             : app_space_zone(space, zone)->tile.texture;
    fill_catalogs(&picker); /* выделяет «все» — карточки заполняет on_catalog_selected */
    select_current_group(&picker);
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
            select_current_group(&picker);
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
    stop_loader(&picker);
    g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(picker.catalogs)), &picker);
    g_signal_handlers_disconnect_by_data(picker.search, &picker);
    gtk_widget_destroy(picker.dialog);
    g_ptr_array_free(picker.pending, TRUE);
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

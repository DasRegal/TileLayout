/*
 * Размер числом: щелчок по подписи размера на плане (длина линейки, ширина проёма, длина стены дома
 * снаружи и внутри) — поле ввода рядом с ней; Enter — применить, Esc — отмена.
 */
#include "app_canvas_private.h"

#include <math.h>

#include "app_render.h"

typedef struct DimensionEdit {
    AppCanvas *canvas;
    AppDimension dimension;
    GtkWidget *popover, *entry;
} DimensionEdit;

static bool in_project(TlProject *project, const AppDimension *d)
{
    void *const *items = d->kind == APP_DIMENSION_RULER     ? (void *const *)project->rulers.items
                         : d->kind == APP_DIMENSION_OPENING ? (void *const *)project->openings.items
                                                            : (void *const *)project->outlines.items;
    size_t count = d->kind == APP_DIMENSION_RULER     ? project->rulers.count
                   : d->kind == APP_DIMENSION_OPENING ? project->openings.count
                                                      : project->outlines.count;
    for (size_t i = 0; i < count; i++)
        if (items[i] == d->item)
            return true;
    return false;
}

/* false — размер не подходит (проём уже 100 мм, стена — меньше толщин). */
static bool apply(AppDocument *document, const AppDimension *d, double value)
{
    TlProject *project = document->project;
    if (!(value > 0) || !in_project(project, d))
        return false;
    switch (d->kind) {
    case APP_DIMENSION_RULER: {
        TlRuler moved = *(TlRuler *)d->item;
        if (!tl_ruler_set_length(&moved, value))
            return false;
        app_document_checkpoint(document, "");
        *(TlRuler *)d->item = moved;
        app_document_commit(document);
        return true;
    }
    case APP_DIMENSION_OPENING: {
        TlOpening *opening = d->item;
        if (opening->locked || value < 100 || value > 10000)
            return false;
        app_document_checkpoint(document, "");
        tl_project_set_opening_width(project, opening, value);
        app_document_commit_rooms(document);
        return true;
    }
    case APP_DIMENSION_WALL: {
        TlOutline *outline = d->item;
        double inner = d->outer ? value - (tl_outline_outer_length(outline, d->wall) -
                                           tl_outline_inner_length(outline, d->wall))
                                : value;
        if (inner <= 1)
            return false;
        app_document_checkpoint(document, "");
        if (!tl_project_set_wall_length(project, outline, d->wall, inner, document->preferences.keep_angles,
                                        document->preferences.link_walls))
            return false;
        app_document_commit(document);
        return true;
    }
    }
    return false;
}

static const char *title_of(const AppDimension *d)
{
    switch (d->kind) {
    case APP_DIMENSION_RULER: return "Длина линейки";
    case APP_DIMENSION_OPENING: return "Ширина проёма";
    default: return d->outer ? "Длина стены снаружи" : "Длина стены внутри";
    }
}

static void on_activate(GtkEntry *entry, DimensionEdit *edit)
{
    char *text = g_strdup(gtk_entry_get_text(entry));
    g_strdelimit(text, ",", '.');
    char *end;
    double value = g_ascii_strtod(text, &end);
    g_free(text);
    AppDocument *document = edit->canvas->document;
    gtk_popover_popdown(GTK_POPOVER(edit->popover));
    if (end == text || fabs(value - edit->dimension.value) < 1e-9)
        return;
    if (apply(document, &edit->dimension, value))
        app_document_status(document, "%s: %g мм", title_of(&edit->dimension), value);
    else
        app_document_status(document, "%s", "Такой размер не подходит (или предмет заблокирован)");
}

static void on_closed(GtkPopover *popover, DimensionEdit *edit)
{
    gtk_widget_destroy(edit->popover);
    g_free(edit);
}

void app_canvas_edit_dimension(AppCanvas *canvas, const AppDimension *dimension)
{
    DimensionEdit *edit = g_new0(DimensionEdit, 1);
    edit->canvas = canvas;
    edit->dimension = *dimension;
    edit->popover = gtk_popover_new(canvas->area);
    GdkRectangle at = {(int)(dimension->center.x - dimension->width / 2), (int)(dimension->center.y - dimension->height / 2),
                       (int)dimension->width, (int)dimension->height};
    gtk_popover_set_pointing_to(GTK_POPOVER(edit->popover), &at);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    GtkWidget *title = gtk_label_new(title_of(dimension));
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_widget_set_opacity(title, 0.75);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    edit->entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(edit->entry), 8);
    gtk_entry_set_input_purpose(GTK_ENTRY(edit->entry), GTK_INPUT_PURPOSE_NUMBER);
    char value[32];
    snprintf(value, sizeof value, "%g", dimension->value);
    gtk_entry_set_text(GTK_ENTRY(edit->entry), value);
    g_signal_connect(edit->entry, "activate", G_CALLBACK(on_activate), edit);
    gtk_box_pack_start(GTK_BOX(row), edit->entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("мм"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(edit->popover), box);
    g_signal_connect(edit->popover, "closed", G_CALLBACK(on_closed), edit);
    gtk_widget_show_all(box);
    gtk_popover_popup(GTK_POPOVER(edit->popover));
    gtk_widget_grab_focus(edit->entry);
    gtk_editable_select_region(GTK_EDITABLE(edit->entry), 0, -1);
}

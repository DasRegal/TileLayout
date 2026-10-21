#include "app_widgets.h"

#include <stdbool.h>
#include <string.h>

#include "app_icons.h"

static gboolean format_with_unit(GtkSpinButton *spin, gpointer data)
{
    const char *unit = data;
    char *text = g_strdup_printf("%.*f%s%s", gtk_spin_button_get_digits(spin), gtk_spin_button_get_value(spin),
                                 unit[0] ? " " : "", unit);
    gtk_entry_set_text(GTK_ENTRY(spin), text);
    g_free(text);
    return TRUE;
}

/* Разбор введённого: число с точкой или запятой, единицы игнорируются. */
static gint parse_with_unit(GtkSpinButton *spin, gdouble *value, gpointer data)
{
    char *text = g_strdup(gtk_entry_get_text(GTK_ENTRY(spin)));
    for (char *p = text; *p; p++)
        if (*p == ',')
            *p = '.';
    char *end = NULL;
    double parsed = g_ascii_strtod(text, &end);
    bool ok = end != text;
    g_free(text);
    if (!ok)
        return GTK_INPUT_ERROR;
    *value = parsed;
    return TRUE;
}

GtkWidget *app_unit_spin_new(double minimum, double maximum, double step, int digits, const char *unit)
{
    GtkWidget *spin = gtk_spin_button_new_with_range(minimum, maximum, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), (guint)digits);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), FALSE); /* иначе GTK отбросит текст с единицей */
    gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_IF_VALID);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 7);
    gtk_widget_set_hexpand(spin, TRUE);
    char *stored = g_strdup(unit ? unit : "");
    g_object_set_data_full(G_OBJECT(spin), "unit", stored, g_free);
    g_signal_connect(spin, "output", G_CALLBACK(format_with_unit), stored);
    g_signal_connect(spin, "input", G_CALLBACK(parse_with_unit), stored);
    return spin;
}

AppSection app_section_new(GtkWidget *container, const char *title)
{
    AppSection section = {gtk_frame_new(NULL), gtk_grid_new(), 0};
    GtkWidget *label = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_frame_set_label_widget(GTK_FRAME(section.frame), label);
    gtk_grid_set_row_spacing(GTK_GRID(section.grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(section.grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(section.grid), 6);
    gtk_container_add(GTK_CONTAINER(section.frame), section.grid);
    gtk_box_pack_start(GTK_BOX(container), section.frame, FALSE, FALSE, 0);
    return section;
}

void app_section_row(AppSection *section, const char *label, GtkWidget *widget)
{
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    gtk_grid_attach(GTK_GRID(section->grid), caption, 0, section->rows, 1, 1);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(section->grid), widget, 1, section->rows, 1, 1);
    section->rows++;
}

void app_section_wide(AppSection *section, GtkWidget *widget)
{
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(section->grid), widget, 0, section->rows, 2, 1);
    section->rows++;
}

GtkWidget *app_section_value_label(AppSection *section, const char *label)
{
    GtkWidget *value = gtk_label_new("—");
    gtk_label_set_xalign(GTK_LABEL(value), 0);
    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
    app_section_row(section, label, value);
    return value;
}

GtkWidget *app_icon_button_new(const char *icon, const char *text)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    if (icon) {
        gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_pixbuf(app_icon_pixbuf(icon, 18)));
        gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    }
    return button;
}

GtkWidget *app_hbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing); }

/* Небольшие помощники GTK: поля с единицами, секции панели, кнопки с иконками. */
#ifndef APP_WIDGETS_H
#define APP_WIDGETS_H

#include <gtk/gtk.h>

/* Поле числа с единицей измерения («600 мм»), принимает и точку, и запятую. */
GtkWidget *app_unit_spin_new(double minimum, double maximum, double step, int digits, const char *unit);

/* Секция панели: рамка с заголовком и сеткой «подпись — поле». */
typedef struct AppSection {
    GtkWidget *frame;
    GtkWidget *grid;
    int rows;
} AppSection;

AppSection app_section_new(GtkWidget *container, const char *title);
void app_section_row(AppSection *section, const char *label, GtkWidget *widget);
/* Виджет на всю ширину секции. */
void app_section_wide(AppSection *section, GtkWidget *widget);
GtkWidget *app_section_value_label(AppSection *section, const char *label);

GtkWidget *app_icon_button_new(const char *icon, const char *text);
GtkWidget *app_hbox(int spacing);

#endif

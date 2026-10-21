/* Иконки кнопок, нарисованные кодом (не зависят от темы и интернета). */
#ifndef APP_ICONS_H
#define APP_ICONS_H

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Имена: undo redo select rect contour partition opening tile_move fit
 * covering svp numbers lock unlock house delete magic print. */
void app_icon_draw(cairo_t *cr, const char *name, double x, double y, double size);
/* Картинка заданного размера (кэшируется, не освобождать). */
GdkPixbuf *app_icon_pixbuf(const char *name, int size);

#endif

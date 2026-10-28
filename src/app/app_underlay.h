/* Подложка плана в программе: загрузка фото и DXF, отрисовка под планом (tl_underlay.h). */
#ifndef APP_UNDERLAY_H
#define APP_UNDERLAY_H

#include <cairo.h>
#include <glib.h>

#include "app_view.h"
#include "tl_underlay.h"

/* Фото или скан плана: уменьшается до 2400 пикселей по длинной стороне и хранится в JPEG.
   false — не картинка (*error — причина, освобождать g_free). */
bool app_underlay_load_image(TlUnderlay *underlay, const char *path, char **error);
bool app_underlay_load_dxf(TlUnderlay *underlay, const char *path, char **error);
/* Под планом: картинка или линии DXF с прозрачностью подложки (невидимая — не рисуется). */
void app_render_underlay(cairo_t *cr, const AppView *view, const TlUnderlay *underlay);

#endif

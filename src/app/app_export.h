/* Экспорт плана в картинку PNG. */
#ifndef APP_EXPORT_H
#define APP_EXPORT_H

#include <stdbool.h>

#include "app_render.h"

/* План целиком, вписанный в картинку width×height (pixel_ratio — для чёткости). */
bool app_export_png(TlProject *project, const char *path, int width, int height, double pixel_ratio,
                    const AppRenderOptions *options);

#endif

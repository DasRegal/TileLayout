/*
 * Ссылка на проект без сервера — как на сайте (web/js/share.js): проект одной строкой, без картинки
 * подложки, сжат deflate и записан в адрес сайта после «#p=» (base64url). Открывается на сайте и здесь.
 */
#ifndef APP_SHARE_H
#define APP_SHARE_H

#include <glib.h>

#include "tl_project.h"

#define APP_SHARE_SITE "https://dasregal.github.io/TileLayout/"

/* Ссылка на проект (g_free). */
char *app_share_link(const TlProject *project);
/* Проект из ссылки (её можно вставить с пробелами и переносами); NULL — не ссылка на проект, *error —
   причина (g_free). */
TlProject *app_share_open(const char *link, char **error);

#endif

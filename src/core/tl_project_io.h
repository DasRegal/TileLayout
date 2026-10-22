/*
 * Проект ↔ JSON. Формат совпадает с Python-версией TileLayout (*.tilelayout.json),
 * поддерживаются и старые файлы, где каждая комната была отдельным контуром.
 */
#ifndef TL_PROJECT_IO_H
#define TL_PROJECT_IO_H

#include "tl_json.h"
#include "tl_project.h"

TlJson *tl_project_to_json(const TlProject *project);
/* Заменяет содержимое project; при ошибке project остаётся пустым. */
bool tl_project_from_json(TlProject *project, const TlJson *root, char *error, size_t error_size);

char *tl_project_to_string(const TlProject *project);
bool tl_project_from_string(TlProject *project, const char *text, char *error, size_t error_size);

#endif

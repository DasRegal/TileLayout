/*
 * Команды над документом — общие для канвы, панели и меню: добавление
 * домов, комнат, перегородок, проёмов, линеек и деформационных швов,
 * удаление, сдвиг стрелками.
 * Каждая команда — одна точка отмены.
 */
#ifndef APP_COMMANDS_H
#define APP_COMMANDS_H

#include "app_document.h"
#include "tl_catalog.h"

/* Выделенное заблокировано от перемещения (у дома — если что-то в нём). */
bool app_selection_is_locked(TlProject *project, const AppSelection *selection);

/* Контур на пустом месте — новый дом; внутри дома — комната из перегородок. */
void app_command_add_contour(AppDocument *document, const TlVec2 *corners, size_t corner_count);
void app_command_add_house(AppDocument *document, TlVec2 origin, double width, double height,
                           double thickness);
void app_command_add_partition(AppDocument *document, TlVec2 start, TlVec2 end);
void app_command_add_opening(AppDocument *document, const TlBand *band, double along);
void app_command_add_ruler(AppDocument *document, TlVec2 start, TlVec2 end);
void app_command_delete_all_rulers(AppDocument *document);
void app_command_add_expansion_joint(AppDocument *document, TlVec2 start, TlVec2 end);
void app_command_insert_vertex(AppDocument *document, TlOutline *outline, size_t wall, TlVec2 point);
/* Угол дома — в точку (концы перегородок на его стенах следуют). */
void app_command_set_vertex(AppDocument *document, TlOutline *outline, size_t index, TlVec2 point);
/* Точка выделенного узла — в точку: середина двигает узел целиком, угол меняет
   углы тел у узла. */
void app_command_set_node_point(AppDocument *document, const AppSelection *selection, TlVec2 point);
/* Плитка выделенных комнат (у общего пола — комнаты-источника) из каталога: размер,
   рисунок, цвет; catalog = NULL — убрать рисунок, оставив цвет. */
void app_command_set_tile_texture(AppDocument *document, const TlCatalog *catalog,
                                  const TlCatalogTile *tile);
/* Точка выделенного примыкания — в точку: вдоль бока — скользит, за грань — узел. */
void app_command_set_tee_point(AppDocument *document, const AppSelection *selection, TlVec2 point);

void app_command_delete_selection(AppDocument *document);
void app_command_delete_outline(AppDocument *document);

/* Стрелки: сдвиг выделенного; move_tiles — сдвигать сетку плитки комнаты. */
void app_command_nudge(AppDocument *document, double dx, double dy, bool move_tiles);

#endif

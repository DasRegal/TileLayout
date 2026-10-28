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
/* Трап — выделяется. */
void app_command_add_drain(AppDocument *document, TlDrain drain);
/* Предмет (ванна, поддон…) — выделяется. */
void app_command_add_fixture(AppDocument *document, TlFixture fixture);
/* Линия раздела покрытия (шов — preferences.divider_gap). */
void app_command_add_divider(AppDocument *document, TlVec2 start, TlVec2 end);
void app_command_add_expansion_joint(AppDocument *document, TlVec2 start, TlVec2 end);
/* Отверстие под трубу диаметром из настроек правки. */
void app_command_add_pipe(AppDocument *document, TlVec2 center);
void app_command_insert_vertex(AppDocument *document, TlOutline *outline, size_t wall, TlVec2 point);
/* Угол дома — в точку (концы перегородок на его стенах следуют). */
void app_command_set_vertex(AppDocument *document, TlOutline *outline, size_t index, TlVec2 point);
/* Точка выделенного узла — в точку: середина двигает узел целиком, угол меняет
   углы тел у узла. */
void app_command_set_node_point(AppDocument *document, const AppSelection *selection, TlVec2 point);
/* Плитка выделенных комнат (у общего пола — комнаты-источника) из каталога: размер,
   рисунок, цвет; catalog = NULL — убрать рисунок, оставив цвет. */
/* Зона комнаты со своей плиткой: бордюр и ковёр пола (TlZoneKind; TL_ZONE_COUNT — основной пол,
   у него зоны нет), затем полосы стен — APP_ZONE_WALL(band). */
#define APP_ZONE_WALL(band) (TL_ZONE_COUNT + 1 + (int)(band))
#define APP_ZONE_AREA APP_ZONE_WALL(TL_WALL_BAND_COUNT) /* на панели: участок выделенной части пола */
#define APP_ZONE_TOTAL (APP_ZONE_AREA + 1)
#define APP_ZONE_AREA_OF(area) (APP_ZONE_TOTAL + (int)(area)) /* участок space->areas[area] */
#define APP_ZONE_PANNO (-1) /* картинка выделенного панно — в окне каталога */
TlZone *app_space_zone(TlSpace *space, int zone);
/* Плитка из каталога для зоны выделенных комнат (catalog == NULL — только цвет). */
void app_command_set_zone_texture(AppDocument *document, int zone, const TlCatalog *catalog,
                                  const TlCatalogTile *tile);
/* Отверстие и линейка на стене wall развёртки комнаты (точки — x от левого угла, h от пола). */
void app_command_add_wall_pipe(AppDocument *document, TlSpace *space, size_t wall, TlVec2 at, double diameter);
void app_command_add_wall_ruler(AppDocument *document, TlSpace *space, size_t wall, TlVec2 start, TlVec2 end);
/* Панно на стене wall развёртки (посередине) или на полу (wall < 0, в центре комнаты) — выделяется.
   Картинка выделенного панно — из каталога (catalog == NULL — только цвет). */
void app_command_add_panno(AppDocument *document, TlSpace *space, long wall);
void app_command_set_panno_texture(AppDocument *document, const TlCatalog *catalog, const TlCatalogTile *tile);
/* «Швы как у пола»: у стены wall развёртки или у всех (wall < 0). */
/* Покрытие части пола region комнаты: covering 0 — как в комнате, 1 — своя плитка, 2 — без плитки. */
void app_command_set_area_covering(AppDocument *document, TlSpace *space, size_t region, int covering);
void app_command_align_walls_to_floor(AppDocument *document, TlSpace *space, long wall);
void app_command_set_tile_texture(AppDocument *document, const TlCatalog *catalog,
                                  const TlCatalogTile *tile);
/* Точка выделенного примыкания — в точку: вдоль бока — скользит, за грань — узел. */
void app_command_set_tee_point(AppDocument *document, const AppSelection *selection, TlVec2 point);

void app_command_delete_selection(AppDocument *document);
void app_command_delete_outline(AppDocument *document);

/* Стрелки: сдвиг выделенного; move_tiles — сдвигать сетку плитки комнаты. */
void app_command_nudge(AppDocument *document, double dx, double dy, bool move_tiles);

#endif

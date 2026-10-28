/*
 * Развёртка стен комнаты для веб-версии (tl_space_walls.c):
 * {"height": высота помещения, "finish_top": верх облицовки, "bounds": [x0, y0, x1, y1] (с полосой пола),
 *  "floor_depth": глубина полосы пола под стенами,
 *  "walls": [{"x", "length", "offset": [x, y] — сдвиг плитки стены}],
 *  "holes": [{"rect": [x0, y0, x1, y1], "name", "kind", "wall"}],
 *  "layouts": [раскладка как в сцене + "tile", "band", "wall", "numbers"],
 *  "floors": [[раскладки пола у стены i, оси — в развёртке, + "tile"]],
 *  "pipes": [{"wall", "at": [x, h], "point": [x, y] в развёртке, "diameter"}],
 *  "rulers": [{"wall", "start": [x, h], "end": [x, h], "a": [x, y], "b": [x, y], "length"}],
 *  "pannos": [панно на стенах (tl_web_panno_json) + "wall", "at": [x, h]],
 *  "stairs": [{"rect": [x0, y0, x1, y1] — столбец лестницы, "steps", "riser", "tread" — подступенок и проступь, мм}]}
 * У раскладок ступеней "stairs": true.
 * В "floors" у стены — и панно на полу (у них "panno": true).
 * Координаты развёртки: x — вдоль стен, y — вниз от потолка (пол — на y = height). Точка на стене —
 * x от левого угла, h от пола; wall — номер стены в развёртке.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_layout.h"
#include "tl_report.h"
#include "tl_room3d.h"
#include "tl_web.h"
#include "tl_web_internal.h"

static TlJson *rect_json(TlRect rect)
{
    TlJson *array = tl_json_array();
    const double values[4] = {rect.left, rect.top, rect.right, rect.bottom};
    for (int i = 0; i < 4; i++)
        tl_json_array_push(array, tl_web_number(values[i]));
    return array;
}

/* Панно для отрисовки: раскладка (to_view — в оси вида или NULL) + "tile", "size": [ширина, высота],
   "outline", "index", "panno": true. */
TlJson *tl_web_panno_json(TlSpace *space, size_t index, const TlAffine *to_view)
{
    TlLayout *layout = tl_space_panno_layout(space, index);
    if (!layout)
        return NULL;
    if (to_view)
        layout->local_to_world = tl_affine_multiply(to_view, &layout->local_to_world);
    const TlPanno *panno = &space->pannos.items[index];
    TlJson *json = tl_web_layout_json(layout);
    TlTileSettings tile = tl_space_panno_tile(space, index);
    tl_json_object_set(json, "tile", tl_web_tile_json(&tile));
    TlJson *size = tl_json_array();
    tl_json_array_push(size, tl_web_number(tl_panno_width(panno)));
    tl_json_array_push(size, tl_web_number(tl_panno_height(panno)));
    tl_json_object_set(json, "size", size);
    TlPolygon outline = tl_space_panno_outline(space, index);
    if (to_view) {
        TlPolygon moved = tl_affine_apply_polygon(to_view, &outline);
        tl_polygon_free(&outline);
        outline = moved;
    }
    tl_json_object_set(json, "outline", tl_web_polygon(&outline));
    tl_polygon_free(&outline);
    tl_json_object_set(json, "index", tl_json_number((double)index));
    tl_json_object_set(json, "panno", tl_json_bool(true));
    tl_layout_free(layout);
    return json;
}

static TlJson *point_json(TlVec2 p)
{
    TlJson *array = tl_json_array();
    tl_json_array_push(array, tl_web_number(p.x));
    tl_json_array_push(array, tl_web_number(p.y));
    return array;
}

/* Пол у стены wall: раскладки пола и зон, оси — в развёртке; только плитки у стены (в полосе). */
static TlJson *floor_strip(TlSpace *space, size_t wall)
{
    TlJson *layouts = tl_json_array();
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    if (!tl_space_effective_tile(space)->enabled)
        return layouts;
    TlRect strip = {room_wall->x - 1, space->height - 1, room_wall->x + room_wall->length + 1,
                    space->height + TL_WALL_FLOOR_DEPTH + 1};
    TlAffine to_development = tl_space_wall_floor_affine(space, wall);
    for (size_t l = 0; l < tl_space_floor_layout_count(space); l++) {
        const TlLayout *layout = tl_space_layout_at(space, l);
        if (layout->too_many)
            continue;
        TlLayout near = *layout; /* те же плитки — без копий контуров; списки не освобождать */
        near.local_to_world = tl_affine_multiply(&to_development, &layout->local_to_world);
        near.tiles.items = malloc((layout->tiles.count + 1) * sizeof *near.tiles.items);
        near.tiles.count = 0;
        for (size_t t = 0; near.tiles.items && t < layout->tiles.count; t++) {
            const TlTile *tile = &layout->tiles.items[t];
            TlVec2 c = tl_affine_apply(&near.local_to_world, tile->center);
            double reach = fmax(tl_rect_width(tile->rect), tl_rect_height(tile->rect));
            if (c.x > strip.left - reach && c.x < strip.right + reach && c.y > strip.top - reach &&
                c.y < strip.bottom + reach)
                near.tiles.items[near.tiles.count++] = *tile;
        }
        TlJson *json = tl_web_layout_json(&near);
        tl_json_object_set(json, "tile", tl_web_tile_json(tl_space_layout_tile(space, l)));
        tl_json_array_push(layouts, json);
        free(near.tiles.items);
    }
    for (size_t i = 0; i < space->pannos.count; i++) /* панно на полу — тоже */
        if (space->pannos.items[i].wall == TL_PANNO_FLOOR) {
            TlJson *panno = tl_web_panno_json(space, i, &to_development);
            if (panno)
                tl_json_array_push(layouts, panno);
        }
    return layouts;
}

const char *tl_web_room_walls(int space_id)
{
    static char *text;
    TlProject *project = tl_web_project();
    TlSpace *space = tl_project_space_by_id(project, space_id);
    if (!space)
        return "null";
    static const char *const KINDS[] = {"door", "window", "arch"};
    TlJson *json = tl_json_object(), *walls = tl_json_array(), *holes = tl_json_array(), *layouts = tl_json_array();
    tl_json_object_set(json, "id", tl_json_number(space->id));
    tl_json_object_set(json, "name", tl_json_string(space->name));
    tl_json_object_set(json, "height", tl_json_number(space->height));
    tl_json_object_set(json, "enabled", tl_json_bool(space->walls.enabled));
    tl_json_object_set(json, "finish_top", tl_web_number(tl_space_walls_finish_top(space)));
    tl_json_object_set(json, "bounds", rect_json(tl_space_walls_bounds(space)));
    for (size_t i = 0; i < tl_space_wall_count(space); i++) {
        const TlRoomWall *wall = tl_space_wall_at(space, i);
        TlJson *item = tl_json_object();
        tl_json_object_set(item, "x", tl_web_number(wall->x));
        tl_json_object_set(item, "length", tl_web_number(wall->length));
        tl_json_object_set(item, "offset", point_json(tl_space_wall_offset(space, i)));
        tl_json_array_push(walls, item);
    }
    for (size_t i = 0; i < tl_space_wall_hole_count(space); i++) {
        const TlWallHole *hole = tl_space_wall_hole_at(space, i);
        TlJson *item = tl_json_object();
        tl_json_object_set(item, "rect", rect_json(hole->rect));
        tl_json_object_set(item, "wall", tl_json_number((double)hole->wall));
        if (hole->fixture < project->fixtures.count) { /* стену закрывает предмет */
            tl_json_object_set(item, "name", tl_json_string(tl_fixture_kind_name(project->fixtures.items[hole->fixture].kind)));
            tl_json_object_set(item, "kind", tl_json_string("fixture"));
        } else if (hole->opening < project->openings.count) {
            const TlOpening *opening = project->openings.items[hole->opening];
            tl_json_object_set(item, "name", tl_json_string(opening->name));
            tl_json_object_set(item, "kind", tl_json_string(KINDS[opening->kind]));
        }
        tl_json_array_push(holes, item);
    }
    tl_report_assign_numbers(project);
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
        TlWallBand band;
        TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, &band);
        TlJson *item = tl_web_layout_json(layout);
        tl_json_object_set(item, "tile", tl_web_tile_json(tl_space_wall_tile(space, band)));
        tl_json_object_set(item, "band", tl_json_string(tl_wall_band_key(band)));
        tl_json_object_set(item, "reveal", tl_json_bool(tl_space_wall_layout_reveal(space, i) != SIZE_MAX));
        tl_json_object_set(item, "stairs", tl_json_bool(tl_space_wall_layout_stairs(space, i, NULL, NULL) != SIZE_MAX));
        size_t on_wall;
        tl_space_wall_layout_at(space, i, &on_wall, NULL);
        tl_json_object_set(item, "wall", tl_json_number((double)on_wall));
        TlJson *numbers = tl_json_array(); /* номера плиток: x, y центра в развёртке, номер */
        for (size_t t = 0; t < layout->tiles.count && !layout->too_many; t++) {
            const TlTile *tile = &layout->tiles.items[t];
            TlVec2 center = tl_affine_apply(&layout->local_to_world, tile->center);
            tl_json_array_push(numbers, tl_web_number(center.x));
            tl_json_array_push(numbers, tl_web_number(center.y));
            tl_json_array_push(numbers, tl_json_number(tile->number));
        }
        tl_json_object_set(item, "numbers", numbers);
        tl_json_array_push(layouts, item);
    }
    TlJson *floors = tl_json_array(), *pipes = tl_json_array(), *rulers = tl_json_array();
    for (size_t i = 0; i < tl_space_wall_count(space); i++)
        tl_json_array_push(floors, floor_strip(space, i));
    for (size_t i = 0; i < space->wall_pipes.count; i++) {
        const TlWallPipe *pipe = &space->wall_pipes.items[i];
        size_t wall = tl_space_wall_of_edge(space, pipe->wall);
        TlJson *item = tl_json_object();
        tl_json_object_set(item, "wall", wall == SIZE_MAX ? tl_json_null() : tl_json_number((double)wall));
        tl_json_object_set(item, "at", point_json(pipe->at));
        tl_json_object_set(item, "point", point_json(tl_space_wall_to_development(space, wall, pipe->at)));
        tl_json_object_set(item, "diameter", tl_json_number(pipe->diameter));
        tl_json_array_push(pipes, item);
    }
    for (size_t i = 0; i < space->wall_rulers.count; i++) {
        const TlWallRuler *ruler = &space->wall_rulers.items[i];
        size_t wall = tl_space_wall_of_edge(space, ruler->wall);
        TlJson *item = tl_json_object();
        tl_json_object_set(item, "wall", wall == SIZE_MAX ? tl_json_null() : tl_json_number((double)wall));
        tl_json_object_set(item, "start", point_json(ruler->start));
        tl_json_object_set(item, "end", point_json(ruler->end));
        tl_json_object_set(item, "a", point_json(tl_space_wall_to_development(space, wall, ruler->start)));
        tl_json_object_set(item, "b", point_json(tl_space_wall_to_development(space, wall, ruler->end)));
        tl_json_object_set(item, "length", tl_json_number(round(tl_distance(ruler->start, ruler->end))));
        tl_json_array_push(rulers, item);
    }
    TlJson *pannos = tl_json_array();
    for (size_t i = 0; space->walls.enabled && i < space->pannos.count; i++) {
        const TlPanno *panno = &space->pannos.items[i];
        size_t wall = panno->wall == TL_PANNO_FLOOR ? SIZE_MAX : tl_space_wall_of_edge(space, panno->wall);
        TlJson *item = wall == SIZE_MAX ? NULL : tl_web_panno_json(space, i, NULL);
        if (!item)
            continue;
        tl_json_object_set(item, "wall", tl_json_number((double)wall));
        tl_json_object_set(item, "at", point_json(panno->at));
        tl_json_array_push(pannos, item);
    }
    tl_json_object_set(json, "pannos", pannos);
    TlJson *stairs = tl_json_array();
    for (size_t k = 0; k < tl_space_stairs_count(space); k++) {
        TlRect column;
        if (!tl_space_stairs_column(space, k, &column))
            continue;
        TlJson *item = tl_json_object();
        tl_json_object_set(item, "rect", rect_json(column));
        const TlFixture *fixture = &space->stairs.items[k];
        tl_json_object_set(item, "steps", tl_json_number(fixture->steps));
        tl_json_object_set(item, "riser", tl_json_number(fixture->height / fixture->steps));
        tl_json_object_set(item, "tread", tl_json_number(fixture->depth / fixture->steps));
        tl_json_array_push(stairs, item);
    }
    tl_json_object_set(json, "stairs", stairs);
    tl_json_object_set(json, "floor_depth", tl_json_number(TL_WALL_FLOOR_DEPTH));
    tl_json_object_set(json, "floors", floors);
    tl_json_object_set(json, "pipes", pipes);
    tl_json_object_set(json, "rulers", rulers);
    tl_json_object_set(json, "walls", walls);
    tl_json_object_set(json, "holes", holes);
    tl_json_object_set(json, "layouts", layouts);
    return tl_web_keep(&text, json);
}

/* ---- правка стен ------------------------------------------------------------------- */

static TlSpace *space_with_wall(int space_id, int wall, const TlRoomWall **room_wall)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    *room_wall = space && wall >= 0 ? tl_space_wall_at(space, (size_t)wall) : NULL;
    return *room_wall ? space : NULL;
}

/* Точка на стене: в её пределах по длине и высоте помещения. */
static TlVec2 on_wall(const TlSpace *space, const TlRoomWall *wall, double x, double h)
{
    return tl_vec2(fmin(fmax(x, 0), wall->length), fmin(fmax(h, 0), space->height));
}

bool tl_web_set_wall_offset(int space_id, int wall, double x, double y, double now_seconds)
{
    const TlRoomWall *room_wall;
    TlSpace *space = space_with_wall(space_id, wall, &room_wall);
    if (!space || !isfinite(x) || !isfinite(y))
        return false;
    tl_web_checkpoint("wall_offset", now_seconds);
    tl_space_set_wall_offset(space, (size_t)wall, tl_vec2(x, y));
    return true;
}

int tl_web_wall_align_floor(int space_id, int wall, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    if (!space)
        return 0;
    tl_web_checkpoint("", now_seconds);
    int aligned = 0;
    for (size_t w = 0; w < tl_space_wall_count(space); w++)
        if ((wall < 0 || (size_t)wall == w) && tl_space_wall_align_floor(space, w))
            aligned++;
    return aligned;
}

int tl_web_add_wall_pipe(int space_id, int wall, double x, double h, double diameter, double now_seconds)
{
    const TlRoomWall *room_wall;
    TlSpace *space = space_with_wall(space_id, wall, &room_wall);
    if (!space || !(diameter >= TL_PIPE_MIN_DIAMETER && diameter <= TL_PIPE_MAX_DIAMETER))
        return -1;
    tl_web_checkpoint("", now_seconds);
    tl_wall_pipe_list_push(&space->wall_pipes, (TlWallPipe){room_wall->edge, on_wall(space, room_wall, x, h), diameter});
    return (int)space->wall_pipes.count - 1;
}

bool tl_web_set_wall_pipe(int space_id, int index, int wall, double x, double h, double diameter, double now_seconds)
{
    const TlRoomWall *room_wall;
    TlSpace *space = space_with_wall(space_id, wall, &room_wall);
    if (!space || index < 0 || (size_t)index >= space->wall_pipes.count ||
        !(diameter >= TL_PIPE_MIN_DIAMETER && diameter <= TL_PIPE_MAX_DIAMETER))
        return false;
    tl_web_checkpoint("wall_pipe", now_seconds);
    space->wall_pipes.items[index] = (TlWallPipe){room_wall->edge, on_wall(space, room_wall, x, h), diameter};
    return true;
}

int tl_web_add_wall_ruler(int space_id, int wall, double x0, double h0, double x1, double h1, double now_seconds)
{
    const TlRoomWall *room_wall;
    TlSpace *space = space_with_wall(space_id, wall, &room_wall);
    TlVec2 start = space ? on_wall(space, room_wall, x0, h0) : tl_vec2(0, 0);
    TlVec2 end = space ? on_wall(space, room_wall, x1, h1) : tl_vec2(0, 0);
    if (!space || tl_distance(start, end) < 1)
        return -1;
    tl_web_checkpoint("", now_seconds);
    tl_wall_ruler_list_push(&space->wall_rulers, (TlWallRuler){room_wall->edge, start, end});
    return (int)space->wall_rulers.count - 1;
}

bool tl_web_set_wall_ruler(int space_id, int index, double x0, double h0, double x1, double h1, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    if (!space || index < 0 || (size_t)index >= space->wall_rulers.count)
        return false;
    TlWallRuler *ruler = &space->wall_rulers.items[index];
    const TlRoomWall *room_wall = tl_space_wall_at(space, tl_space_wall_of_edge(space, ruler->wall));
    if (!room_wall)
        return false;
    TlVec2 start = on_wall(space, room_wall, x0, h0), end = on_wall(space, room_wall, x1, h1);
    if (tl_distance(start, end) < 1)
        return false;
    tl_web_checkpoint("wall_ruler", now_seconds);
    ruler->start = start;
    ruler->end = end;
    return true;
}

bool tl_web_delete_wall_item(int space_id, const char *kind, int index, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    bool pipe = strcmp(kind, "wall_pipe") == 0, ruler = strcmp(kind, "wall_ruler") == 0;
    bool panno = strcmp(kind, "panno") == 0;
    size_t count = !space ? 0 : pipe ? space->wall_pipes.count : ruler ? space->wall_rulers.count
                                : panno ? space->pannos.count : 0;
    if (index < 0 || (size_t)index >= count)
        return false;
    tl_web_checkpoint("", now_seconds);
    if (pipe)
        tl_wall_pipe_list_remove_at(&space->wall_pipes, (size_t)index);
    else if (panno)
        tl_panno_list_remove_at(&space->pannos, (size_t)index);
    else
        tl_wall_ruler_list_remove_at(&space->wall_rulers, (size_t)index);
    return true;
}

const char *tl_web_walls_snap(int space_id, double x, double y, double tolerance)
{
    static char *text;
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    TlVec2 point = space ? tl_space_walls_snap(space, tl_vec2(x, y), tolerance) : tl_vec2(x, y);
    return tl_web_keep(&text, point_json(point));
}

/* ---- панно ------------------------------------------------------------------------- */

int tl_web_add_panno(int space_id, int wall, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    const TlRoomWall *room_wall = space && wall >= 0 ? tl_space_wall_at(space, (size_t)wall) : NULL;
    if (!space || (wall >= 0 && !room_wall))
        return -1;
    TlPanno panno = tl_panno_default(room_wall ? room_wall->edge : TL_PANNO_FLOOR);
    if (room_wall) /* посередине стены */
        panno.at.x = fmax(0, (room_wall->length - tl_panno_width(&panno)) / 2);
    tl_web_checkpoint("", now_seconds);
    tl_panno_list_push(&space->pannos, panno);
    return (int)space->pannos.count - 1;
}

/* fields: {"columns", "rows", "w", "h", "gap", "x", "y", "price", "color", "texture", "wall"} — что есть. */
bool tl_web_set_panno(int space_id, int index, const char *fields, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    bool ok = space && index >= 0 && (size_t)index < space->pannos.count && parsed && tl_json_type(parsed) == TL_JSON_OBJECT;
    if (ok) {
        TlPanno panno = space->pannos.items[index];
        const TlJson *wall = tl_json_get(parsed, "wall");
        if (wall) {
            double number = tl_json_number_or(wall, -1);
            const TlRoomWall *room_wall = number >= 0 ? tl_space_wall_at(space, (size_t)number) : NULL;
            ok = number < 0 || room_wall;
            panno.wall = room_wall ? room_wall->edge : TL_PANNO_FLOOR;
        }
        static const char *const NUMBERS[] = {"w", "h", "gap", "x", "y", "price"};
        double *const targets[] = {&panno.tile_width, &panno.tile_height, &panno.joint_width, &panno.at.x, &panno.at.y,
                                   &panno.price};
        for (size_t i = 0; i < 6; i++) {
            const TlJson *value = tl_json_get(parsed, NUMBERS[i]);
            if (value)
                ok = ok && isfinite(*targets[i] = tl_json_number_or(value, NAN));
        }
        const TlJson *columns = tl_json_get(parsed, "columns"), *rows = tl_json_get(parsed, "rows");
        if (columns)
            panno.columns = (int)tl_json_number_or(columns, panno.columns);
        if (rows)
            panno.rows = (int)tl_json_number_or(rows, panno.rows);
        const TlJson *color = tl_json_get(parsed, "color"), *texture = tl_json_get(parsed, "texture");
        if (color)
            snprintf(panno.color, sizeof panno.color, "%s", tl_json_string_or(color, panno.color));
        if (texture)
            snprintf(panno.texture, sizeof panno.texture, "%s", tl_json_string_or(texture, ""));
        if (ok) {
            char merge_key[64] = "";
            if (tl_json_length(parsed) == 1)
                snprintf(merge_key, sizeof merge_key, "panno_%d_%d_%s", space_id, index, tl_json_key_at(parsed, 0));
            tl_web_checkpoint(merge_key, now_seconds);
            tl_panno_clamp(&panno);
            space->pannos.items[index] = panno;
        }
    }
    tl_json_free(parsed);
    return ok;
}

/* ---- объёмный вид (tl_room3d.h) ----------------------------------------------------
 * {"camera": {"yaw", "pitch", "distance"}, "paints": [{"c": [[x0, y0, …], …] — контуры в пикселях
 *  (заливка «чёт-нечет»), "f": заливка, "s": обводка или "", "w": её толщина}]}
 * distance < 0 — камера по умолчанию, 0 — те же углы, расстояние — вся комната в кадре. */

const char *tl_web_room_3d(int space_id, double yaw, double pitch, double distance, double width, double height)
{
    static char *text;
    TlProject *project = tl_web_project();
    TlSpace *space = tl_project_space_by_id(project, space_id);
    TlJson *json = tl_json_object(), *paints_json = tl_json_array();
    if (space) {
        TlCamera camera = tl_room3d_default_camera(project, space, width, height);
        if (distance >= 0) {
            camera.yaw = yaw;
            camera.pitch = pitch;
            camera.distance = distance;
        }
        TlPaintList paints = tl_room3d_paint(project, space, &camera, width, height);
        for (size_t i = 0; i < paints.count; i++) {
            const TlPaint *paint = &paints.items[i];
            TlJson *item = tl_json_object(), *contours = tl_json_array();
            for (size_t c = 0; c < paint->contours.count; c++) {
                TlJson *points = tl_json_array();
                for (size_t k = 0; k < paint->contours.items[c].count; k++) {
                    TlVec2 p = paint->contours.items[c].items[k];
                    tl_json_array_push(points, tl_json_number(round(p.x * 10) / 10));
                    tl_json_array_push(points, tl_json_number(round(p.y * 10) / 10));
                }
                tl_json_array_push(contours, points);
            }
            tl_json_object_set(item, "c", contours);
            tl_json_object_set(item, "f", tl_json_string(paint->fill));
            tl_json_object_set(item, "s", tl_json_string(paint->stroke));
            tl_json_object_set(item, "w", tl_json_number(paint->stroke_width));
            tl_json_array_push(paints_json, item);
        }
        tl_paint_list_free_all(&paints);
        TlJson *camera_json = tl_json_object();
        tl_json_object_set(camera_json, "yaw", tl_json_number(camera.yaw));
        tl_json_object_set(camera_json, "pitch", tl_json_number(camera.pitch));
        tl_json_object_set(camera_json, "distance", tl_json_number(camera.distance));
        tl_json_object_set(json, "camera", camera_json);
    }
    tl_json_object_set(json, "paints", paints_json);
    return tl_web_keep(&text, json);
}

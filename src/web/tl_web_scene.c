/*
 * Сцена для рисования в браузере — всё в мм, многоугольники плоскими
 * массивами [x0, y0, x1, y1, …]:
 *
 * {"bounds": [left, top, right, bottom],
 *  "walls": [{"outer": [...], "inner": [...]}],         основные стены
 *  "partitions": [[...], ...],                           силуэт перегородок
 *  "doorways": [[...], ...],                             пол под проёмами — стены его не закрывают
 *  "openings": [{"kind": "door" | "wall",
 *                "cut": [...],                           вырез в стене (у "wall")
 *                "jambs": [[x0, y0, x1, y1], ...],       косяки (у "door")
 *                "threshold": [x0, y0, x1, y1] | null,   стык полов
 *                "width": мм, "name": "Д1", "from": [x, y], "to": [x, y]}],  подпись — вдоль from→to
 *  "dimensions": [{"from": [x, y], "to": [x, y], "normal": [x, y], "length": мм}],  снаружи дома
 *  "rulers": [{"line": [x0, y0, x1, y1], "length": мм, "name": "Л1"}],
 *  "joints": [{"line": [x0, y0, x1, y1], "gap": мм, "divider"}], деформационные швы и линии раздела
 *  "underlay": подложка без содержимого (tl_web_underlay.c),
 *  "fixtures": [{"kind", "name", "outline", "center", "w", "d", "h", "angle", "steps" — у лестницы}], предметы
 *  "drains": [{"kind", "outline"}], "slopes": [x0, y0, x1, y1, …] — трапы и разрезы конверта
 *  "pipes": [{"center": [x, y], "diameter": мм}],              отверстия под трубы
 *  "spaces": [{"id", "name", "outline": [...], "holes": [[...]], "room": [...],      room — для длин стен
 *              "label": [x, y], "area": м², "source": "имя" | null,
 *              "tile": {настройки, ключи как в файле},
 *              "layout": null | {"matrix": [a, b, c, d, tx, ty],  оси раскладки → мм плана
 *                                "floor": [...], "holes": [[...]], "too_many": bool,
 *                                "tiles": [left, top, right, bottom, вид, плитка, ...],  вид: 0 целая, 1 подрезка,
 *                                          2 узкая; плитка — номер в "specs"
 *                                "specs": [{"color", "textured": рисунок из каталога}],  виды плитки рисунка
 *                                "clips": [x0, y0, ...], "crosses": [...] — пересечения швов, "count": плиток,
 *                                "shapes": {"номер плитки": [x0, y0, …]} — плитки-многоугольники},
 *              "zones": [раскладки зон — как "layout" и "tile": настройки плитки зоны]}]}
 */
#include <math.h>
#include <stdio.h>

#include "tl_layout.h"
#include "tl_web.h"
#include "tl_web_internal.h"

#define FACES_OTHER_PROBE 50.0 /* мм за стену: там другой дом — размер снаружи не нужен */

static TlJson *point(TlVec2 p)
{
    TlJson *array = tl_json_array();
    tl_json_array_push(array, tl_web_number(p.x));
    tl_json_array_push(array, tl_web_number(p.y));
    return array;
}

static TlJson *segment(TlVec2 a, TlVec2 b)
{
    TlPolygon line = {0};
    tl_polygon_push(&line, a);
    tl_polygon_push(&line, b);
    TlJson *json = tl_web_polygon(&line);
    tl_polygon_free(&line);
    return json;
}

static TlJson *polygons(const TlPolygonList *list)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < list->count; i++)
        tl_json_array_push(array, tl_web_polygon(&list->items[i]));
    return array;
}

/* ---- стены ------------------------------------------------------------------ */

static TlJson *walls(TlProject *project)
{
    TlJson *array = tl_json_array();
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        TlPolygon outer = tl_outline_outer(outline);
        TlJson *wall = tl_json_object();
        tl_json_object_set(wall, "outer", tl_web_polygon(&outer));
        tl_json_object_set(wall, "inner", tl_web_polygon(&outline->inner));
        tl_json_array_push(array, wall);
        tl_polygon_free(&outer);
    }
    return array;
}

static TlJson *doorways(TlProject *project)
{
    TlPolygonList list = {0};
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon doorway;
        if (tl_project_opening_doorway(project, project->openings.items[i], &doorway))
            tl_polygon_list_push(&list, doorway);
    }
    TlJson *json = polygons(&list);
    tl_polygon_list_free_all(&list);
    return json;
}

/* Стык полов под проёмом; у основной стены — только если под проёмом пол. */
static TlJson *threshold(TlProject *project, const TlOpening *opening, bool door)
{
    TlVec2 from, to;
    if (!tl_project_opening_threshold(project, opening, &from, &to))
        return tl_json_null();
    TlPolygon doorway;
    if (!door) {
        if (!tl_project_opening_doorway(project, opening, &doorway))
            return tl_json_null();
        tl_polygon_free(&doorway);
    }
    return segment(from, to);
}

/* NULL — проём не на стене. */
static TlJson *opening_json(TlProject *project, const TlOpening *opening)
{
    TlBand band;
    bool door = tl_project_opening_host(project, opening, &band) && band.host.kind == TL_HOST_PARTITION;
    TlJson *jambs = tl_json_array();
    TlJson *json = tl_json_object();
    TlVec2 width_from, width_to;
    if (door) {
        TlVec2 from[2], to[2];
        if (!tl_project_opening_jambs(project, opening, from, to)) {
            tl_json_free(jambs);
            tl_json_free(json);
            return NULL;
        }
        tl_json_array_push(jambs, segment(from[0], from[1]));
        tl_json_array_push(jambs, segment(to[0], to[1]));
        width_from = tl_vec2_midpoint(from[0], from[1]);
        width_to = tl_vec2_midpoint(to[0], to[1]);
    } else {
        TlPolygon cut;
        if (!tl_project_opening_cut(project, opening, &cut)) {
            tl_json_free(jambs);
            tl_json_free(json);
            return NULL;
        }
        tl_json_object_set(json, "cut", tl_web_polygon(&cut));
        width_from = tl_vec2_midpoint(cut.items[0], cut.items[3]);
        width_to = tl_vec2_midpoint(cut.items[1], cut.items[2]);
        tl_polygon_free(&cut);
    }
    tl_json_object_set(json, "kind", tl_json_string(door ? "door" : "wall"));
    tl_json_object_set(json, "jambs", jambs);
    tl_json_object_set(json, "threshold", threshold(project, opening, door));
    tl_json_object_set(json, "width", tl_json_number(opening->width));
    tl_json_object_set(json, "name", tl_json_string(opening->name));
    tl_json_object_set(json, "from", point(width_from));
    tl_json_object_set(json, "to", point(width_to));
    return json;
}

static TlJson *openings(TlProject *project)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < project->openings.count; i++) {
        TlJson *json = opening_json(project, project->openings.items[i]);
        if (json)
            tl_json_array_push(array, json);
    }
    return array;
}

/* ---- размеры снаружи ---------------------------------------------------------- */

static bool faces_other_outline(TlProject *project, TlOutline *outline, const TlPolygon *outer, size_t wall)
{
    TlVec2 normal;
    tl_outline_wall_frame(outline, wall, NULL, &normal);
    TlVec2 middle = tl_vec2_midpoint(outer->items[wall], outer->items[(wall + 1) % outer->count]);
    TlVec2 probe = tl_vec2_add(middle, tl_vec2_scale(normal, FACES_OTHER_PROBE));
    for (size_t o = 0; o < project->outlines.count; o++)
        if (project->outlines.items[o] != outline && tl_outline_contains(project->outlines.items[o], probe))
            return true;
    return false;
}

static TlJson *dimensions(TlProject *project)
{
    TlJson *array = tl_json_array();
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        TlPolygon outer = tl_outline_outer(outline);
        for (size_t wall = 0; wall < tl_outline_wall_count(outline); wall++) {
            if (outline->thickness.items[wall] <= 0 || faces_other_outline(project, outline, &outer, wall))
                continue;
            TlVec2 a = outer.items[wall], b = outer.items[(wall + 1) % outer.count], normal;
            tl_outline_wall_frame(outline, wall, NULL, &normal);
            TlJson *dimension = tl_json_object();
            tl_json_object_set(dimension, "from", point(a));
            tl_json_object_set(dimension, "to", point(b));
            tl_json_object_set(dimension, "normal", point(normal));
            tl_json_object_set(dimension, "length", tl_json_number(round(tl_distance(a, b))));
            tl_json_array_push(array, dimension);
        }
        tl_polygon_free(&outer);
    }
    return array;
}

/* ---- комнаты ------------------------------------------------------------------- */

TlJson *tl_web_layout_json(const TlLayout *layout)
{
    TlJson *json = tl_json_object();
    const TlAffine *m = &layout->local_to_world;
    const double matrix[6] = {m->a, m->b, m->c, m->d, m->tx, m->ty};
    TlJson *matrix_json = tl_json_array();
    for (int i = 0; i < 6; i++)
        tl_json_array_push(matrix_json, tl_json_number(matrix[i]));
    tl_json_object_set(json, "matrix", matrix_json);
    tl_json_object_set(json, "floor", tl_web_polygon(&layout->floor_outline));
    tl_json_object_set(json, "holes", polygons(&layout->floor_holes));
    tl_json_object_set(json, "too_many", tl_json_bool(layout->too_many));
    TlJson *tiles = tl_json_array();
    for (size_t i = 0; i < layout->tiles.count && !layout->too_many; i++) {
        const TlTile *tile = &layout->tiles.items[i];
        tl_json_array_push(tiles, tl_web_number(tile->rect.left));
        tl_json_array_push(tiles, tl_web_number(tile->rect.top));
        tl_json_array_push(tiles, tl_web_number(tile->rect.right));
        tl_json_array_push(tiles, tl_web_number(tile->rect.bottom));
        tl_json_array_push(tiles, tl_json_number(tile->kind));
        tl_json_array_push(tiles, tl_json_number(tile->spec));
    }
    tl_json_object_set(json, "tiles", tiles);
    TlJson *specs = tl_json_array();
    for (size_t i = 0; i < layout->pattern.spec_count; i++) {
        TlJson *spec = tl_json_object();
        tl_json_object_set(spec, "color", tl_json_string(layout->pattern.specs[i].color));
        tl_json_object_set(spec, "textured", tl_json_bool(layout->pattern.specs[i].textured));
        tl_json_array_push(specs, spec);
    }
    tl_json_object_set(json, "specs", specs);
    TlJson *shapes = tl_json_object(); /* шеврон, вставка: контур в осях раскладки */
    for (size_t i = 0; i < layout->tiles.count && !layout->too_many; i++) {
        const TlTile *tile = &layout->tiles.items[i];
        if (!tile->outline.count)
            continue;
        TlPolygon outline = tl_polygon_copy(&tile->outline);
        tl_polygon_translate(&outline, tl_vec2(tile->rect.left, tile->rect.top));
        char key[24];
        snprintf(key, sizeof key, "%zu", i);
        tl_json_object_set(shapes, key, tl_web_polygon(&outline));
        tl_polygon_free(&outline);
    }
    tl_json_object_set(json, "shapes", shapes);
    tl_json_object_set(json, "clips", tl_web_polygon(&layout->clip_points));
    tl_json_object_set(json, "crosses", tl_web_polygon(&layout->cross_points));
    tl_json_object_set(json, "count", tl_json_number(layout->too_many ? 0 : tl_layout_total(layout)));
    return json;
}

static TlJson *space_json(TlSpace *space)
{
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "id", tl_json_number(space->id));
    tl_json_object_set(json, "name", tl_json_string(space->name));
    tl_json_object_set(json, "outline", tl_web_polygon(&space->outline));
    tl_json_object_set(json, "holes", polygons(&space->holes));
    tl_json_object_set(json, "room", tl_web_polygon(tl_space_room_outline(space)));
    tl_json_object_set(json, "label", point(tl_space_label_point(space)));
    tl_json_object_set(json, "area", tl_json_number(round(tl_space_room_area(space) / 1e4) / 100));
    tl_json_object_set(json, "source", tl_space_has_shared_floor(space) ? tl_json_string(space->layout_source->name)
                                                                        : tl_json_null());
    const TlTileSettings *tile = tl_space_effective_tile(space);
    tl_json_object_set(json, "tile", tl_web_tile_json(tile));
    tl_json_object_set(json, "layout", tile->enabled ? tl_web_layout_json(tl_space_layout(space)) : tl_json_null());
    TlJson *zones = tl_json_array(); /* бордюр (по раскладке на стену), ковёр — поверх основного пола */
    for (size_t l = 1; tile->enabled && l < tl_space_floor_layout_count(space); l++) {
        TlJson *zone = tl_web_layout_json(tl_space_layout_at(space, l));
        tl_json_object_set(zone, "tile", tl_web_tile_json(tl_space_layout_tile(space, l)));
        tl_json_array_push(zones, zone);
    }
    tl_json_object_set(json, "zones", zones);
    TlJson *pannos = tl_json_array(); /* панно на полу — поверх пола */
    for (size_t i = 0; tile->enabled && i < space->pannos.count; i++) {
        TlJson *panno = space->pannos.items[i].wall == TL_PANNO_FLOOR ? tl_web_panno_json(space, i, NULL) : NULL;
        if (panno)
            tl_json_array_push(pannos, panno);
    }
    tl_json_object_set(json, "pannos", pannos);
    return json;
}

/* ---- сцена ----------------------------------------------------------------------- */

static TlJson *bounds(TlProject *project)
{
    TlRect rect = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlPolygon outer = tl_outline_outer(project->outlines.items[o]);
        for (size_t i = 0; i < outer.count; i++) {
            rect.left = fmin(rect.left, outer.items[i].x);
            rect.top = fmin(rect.top, outer.items[i].y);
            rect.right = fmax(rect.right, outer.items[i].x);
            rect.bottom = fmax(rect.bottom, outer.items[i].y);
        }
        tl_polygon_free(&outer);
    }
    if (!isfinite(rect.left))
        rect = (TlRect){0, 0, 10000, 8000};
    const double values[4] = {rect.left, rect.top, rect.right, rect.bottom};
    TlJson *array = tl_json_array();
    for (int i = 0; i < 4; i++)
        tl_json_array_push(array, tl_web_number(values[i]));
    return array;
}

const char *tl_web_scene(void)
{
    static char *scene;
    TlProject *project = tl_web_project();
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "bounds", bounds(project));
    tl_json_object_set(json, "walls", walls(project));
    TlPolygonList silhouette = tl_project_partition_silhouette(project);
    tl_json_object_set(json, "partitions", polygons(&silhouette));
    tl_polygon_list_free_all(&silhouette);
    tl_json_object_set(json, "doorways", doorways(project));
    tl_json_object_set(json, "openings", openings(project));
    tl_json_object_set(json, "dimensions", dimensions(project));
    TlJson *rulers = tl_json_array();
    for (size_t i = 0; i < project->rulers.count; i++) {
        TlJson *ruler = tl_json_object();
        tl_json_object_set(ruler, "line", segment(project->rulers.items[i]->start, project->rulers.items[i]->end));
        tl_json_object_set(ruler, "length", tl_json_number(round(tl_ruler_length(project->rulers.items[i]))));
        tl_json_object_set(ruler, "name", tl_json_string(project->rulers.items[i]->name));
        tl_json_array_push(rulers, ruler);
    }
    tl_json_object_set(json, "rulers", rulers);
    TlJson *joints = tl_json_array();
    for (size_t i = 0; i < project->expansion_joints.count; i++) {
        const TlExpansionJoint *item = project->expansion_joints.items[i];
        TlJson *joint = tl_json_object();
        tl_json_object_set(joint, "line", segment(item->start, item->end));
        tl_json_object_set(joint, "gap", tl_json_number(item->gap));
        tl_json_object_set(joint, "divider", tl_json_bool(item->kind == TL_LINE_DIVIDER));
        tl_json_array_push(joints, joint);
    }
    tl_json_object_set(json, "joints", joints);
    TlJson *fixtures = tl_json_array();
    for (size_t i = 0; i < project->fixtures.count; i++) {
        const TlFixture *item = &project->fixtures.items[i];
        TlJson *fixture = tl_json_object();
        TlPolygon outline = tl_fixture_outline(item);
        tl_json_object_set(fixture, "kind", tl_json_string(tl_fixture_kind_key(item->kind)));
        tl_json_object_set(fixture, "name", tl_json_string(tl_fixture_kind_name(item->kind)));
        tl_json_object_set(fixture, "outline", tl_web_polygon(&outline));
        tl_json_object_set(fixture, "center", point(item->center));
        tl_json_object_set(fixture, "w", tl_json_number(item->width));
        tl_json_object_set(fixture, "d", tl_json_number(item->depth));
        tl_json_object_set(fixture, "angle", tl_json_number(item->angle));
        if (item->kind == TL_FIXTURE_STAIRS)
            tl_json_object_set(fixture, "steps", tl_json_number(item->steps));
        tl_json_array_push(fixtures, fixture);
        tl_polygon_free(&outline);
    }
    tl_json_object_set(json, "fixtures", fixtures);
    tl_json_object_set(json, "underlay", tl_web_underlay_json(false));
    TlJson *drains = tl_json_array(), *slopes = tl_json_array();
    for (size_t i = 0; i < project->drains.count; i++) {
        TlJson *drain = tl_json_object();
        TlPolygon outline = tl_drain_outline(&project->drains.items[i]);
        tl_json_object_set(drain, "kind", tl_json_string(tl_drain_kind_key(project->drains.items[i].kind)));
        tl_json_object_set(drain, "outline", tl_web_polygon(&outline));
        tl_json_array_push(drains, drain);
        tl_polygon_free(&outline);
    }
    for (size_t s = 0; s < project->spaces.count; s++) {
        const TlSpace *space = project->spaces.items[s];
        for (size_t i = 0; i < space->slope_lines.count; i++)
            for (int k = 0; k < 2; k++) {
                tl_json_array_push(slopes, tl_web_number(space->slope_lines.items[i].items[k].x));
                tl_json_array_push(slopes, tl_web_number(space->slope_lines.items[i].items[k].y));
            }
    }
    tl_json_object_set(json, "drains", drains);
    tl_json_object_set(json, "slopes", slopes);
    TlJson *pipes = tl_json_array();
    for (size_t i = 0; i < project->pipes.count; i++) {
        TlJson *pipe = tl_json_object();
        tl_json_object_set(pipe, "center", point(project->pipes.items[i]->center));
        tl_json_object_set(pipe, "diameter", tl_json_number(project->pipes.items[i]->diameter));
        tl_json_array_push(pipes, pipe);
    }
    tl_json_object_set(json, "pipes", pipes);
    TlJson *spaces = tl_json_array();
    for (size_t s = 0; s < project->spaces.count; s++)
        tl_json_array_push(spaces, space_json(project->spaces.items[s]));
    tl_json_object_set(json, "spaces", spaces);
    return tl_web_keep(&scene, json);
}

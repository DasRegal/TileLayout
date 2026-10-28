#include "tl_project_io.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FORMAT_VERSION 4 /* 4 — стык и пол под проёмами основных стен */

/* ---- настройки плитки (таблица полей — в tl_tile_settings) ------------- */

static TlJson *tile_to_json(const TlTileSettings *tile)
{
    TlJson *object = tl_json_object();
    const char *base = (const char *)tile;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++) {
        const TlTileField *field = &fields[i];
        const void *member = base + field->offset;
        TlJson *value = field->type == TL_FIELD_BOOL     ? tl_json_bool(*(const bool *)member)
                        : field->type == TL_FIELD_DOUBLE ? tl_json_number(*(const double *)member)
                        : field->type == TL_FIELD_INT    ? tl_json_number(*(const int *)member)
                                                         : tl_json_string((const char *)member);
        tl_json_object_set(object, field->key, value);
    }
    return object;
}

/* Отсутствующие в файле поля — из base. */
static TlTileSettings tile_from_json_over(const TlJson *object, TlTileSettings tile)
{
    char *base = (char *)&tile;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++) {
        const TlTileField *field = &fields[i];
        const TlJson *value = tl_json_get(object, field->key);
        if (!value)
            continue;
        void *member = base + field->offset;
        switch (field->type) {
        case TL_FIELD_BOOL: *(bool *)member = tl_json_bool_or(value, *(bool *)member); break;
        case TL_FIELD_DOUBLE: *(double *)member = tl_json_number_or(value, *(double *)member); break;
        case TL_FIELD_INT: *(int *)member = (int)tl_json_number_or(value, *(int *)member); break;
        case TL_FIELD_COLOR:
        case TL_FIELD_TEXT:
            snprintf((char *)member, field->size, "%s", tl_json_string_or(value, (char *)member));
            break;
        }
    }
    return tile;
}

/* Отсутствующие в файле поля — из настроек для новой комнаты. */
static TlTileSettings tile_from_json(const TlJson *object)
{
    return tile_from_json_over(object, tl_tile_settings_for_new_room());
}

/* ---- зоны пола комнаты --------------------------------------------------- */

static TlJson *zones_to_json(const TlSpace *space)
{
    TlJson *zones = tl_json_object();
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++) {
        const TlZone *zone = &space->zones[kind];
        TlJson *json = tl_json_object();
        tl_json_object_set(json, "enabled", tl_json_bool(zone->enabled));
        tl_json_object_set(json, "width", tl_json_number(zone->width));
        tl_json_object_set(json, "fit", tl_json_bool(zone->fit));
        if (kind == TL_ZONE_CARPET) {
            tl_json_object_set(json, "height", tl_json_number(zone->height));
            tl_json_object_set(json, "dx", tl_json_number(zone->dx));
            tl_json_object_set(json, "dy", tl_json_number(zone->dy));
        }
        tl_json_object_set(json, "tile", tile_to_json(&zone->tile));
        tl_json_object_set(zones, tl_zone_key((TlZoneKind)kind), json);
    }
    return zones;
}

static void zones_from_json(TlSpace *space, const TlJson *zones)
{
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++) {
        const TlJson *json = tl_json_get(zones, tl_zone_key((TlZoneKind)kind));
        if (!json)
            continue;
        TlZone *zone = &space->zones[kind];
        zone->enabled = tl_json_bool_or(tl_json_get(json, "enabled"), false);
        zone->width = tl_json_number_or(tl_json_get(json, "width"), zone->width);
        zone->fit = tl_json_bool_or(tl_json_get(json, "fit"), zone->fit);
        zone->height = tl_json_number_or(tl_json_get(json, "height"), zone->height);
        zone->dx = tl_json_number_or(tl_json_get(json, "dx"), 0);
        zone->dy = tl_json_number_or(tl_json_get(json, "dy"), 0);
        zone->tile = tile_from_json_over(tl_json_get(json, "tile"), zone->tile);
    }
}

/* ---- облицовка стен комнаты ------------------------------------------------ */

static TlJson *walls_to_json(const TlWallFinish *finish)
{
    TlJson *json = tl_json_object(), *bands = tl_json_object();
    tl_json_object_set(json, "enabled", tl_json_bool(finish->enabled));
    tl_json_object_set(json, "height", tl_json_number(finish->height));
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++) {
        const TlZone *zone = &finish->bands[band];
        TlJson *item = tl_json_object();
        if (band != TL_WALL_MAIN) {
            tl_json_object_set(item, "enabled", tl_json_bool(zone->enabled));
            tl_json_object_set(item, "width", tl_json_number(zone->width));
            tl_json_object_set(item, "fit", tl_json_bool(zone->fit));
        }
        if (band == TL_WALL_FRIEZE)
            tl_json_object_set(item, "dy", tl_json_number(zone->dy));
        tl_json_object_set(item, "tile", tile_to_json(&zone->tile));
        tl_json_object_set(bands, tl_wall_band_key((TlWallBand)band), item);
    }
    tl_json_object_set(json, "bands", bands);
    return json;
}

static void walls_from_json(TlWallFinish *finish, const TlJson *json)
{
    if (!json)
        return;
    finish->enabled = tl_json_bool_or(tl_json_get(json, "enabled"), false);
    finish->height = tl_json_number_or(tl_json_get(json, "height"), 0);
    const TlJson *bands = tl_json_get(json, "bands");
    for (int band = 0; bands && band < TL_WALL_BAND_COUNT; band++) {
        const TlJson *item = tl_json_get(bands, tl_wall_band_key((TlWallBand)band));
        if (!item)
            continue;
        TlZone *zone = &finish->bands[band];
        if (band != TL_WALL_MAIN)
            zone->enabled = tl_json_bool_or(tl_json_get(item, "enabled"), false);
        zone->width = tl_json_number_or(tl_json_get(item, "width"), zone->width);
        zone->fit = tl_json_bool_or(tl_json_get(item, "fit"), zone->fit);
        zone->dy = tl_json_number_or(tl_json_get(item, "dy"), zone->dy);
        zone->tile = tile_from_json_over(tl_json_get(item, "tile"), zone->tile);
    }
}

/* Предметы на стенах: трубы [стена, x, h, диаметр], линейки [стена, x0, h0, x1, h1], смещения
   плитки [стена, x, y]; стена — номер стороны контура комнаты. */
static TlJson *numbers_row(const double *values, size_t count)
{
    TlJson *row = tl_json_array();
    for (size_t i = 0; i < count; i++)
        tl_json_array_push(row, tl_json_number(values[i]));
    return row;
}

static TlJson *wall_items_to_json(const TlSpace *space)
{
    TlJson *json = tl_json_object(), *pipes = tl_json_array(), *rulers = tl_json_array(), *shifts = tl_json_array();
    for (size_t i = 0; i < space->wall_pipes.count; i++) {
        const TlWallPipe *p = &space->wall_pipes.items[i];
        const double row[] = {(double)p->wall, p->at.x, p->at.y, p->diameter};
        tl_json_array_push(pipes, numbers_row(row, 4));
    }
    for (size_t i = 0; i < space->wall_rulers.count; i++) {
        const TlWallRuler *r = &space->wall_rulers.items[i];
        const double row[] = {(double)r->wall, r->start.x, r->start.y, r->end.x, r->end.y};
        tl_json_array_push(rulers, numbers_row(row, 5));
    }
    for (size_t i = 0; i < space->wall_shifts.count; i++) {
        const TlWallShift *s = &space->wall_shifts.items[i];
        const double row[] = {(double)s->wall, s->offset.x, s->offset.y};
        tl_json_array_push(shifts, numbers_row(row, 3));
    }
    tl_json_object_set(json, "pipes", pipes);
    tl_json_object_set(json, "rulers", rulers);
    tl_json_object_set(json, "shifts", shifts);
    return json;
}

/* Панно: {"wall": сторона или −1 (пол), "x", "y", "columns", "rows", "w", "h", "gap", "color", "texture", "price"}. */
static TlJson *pannos_to_json(const TlSpace *space)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < space->pannos.count; i++) {
        const TlPanno *p = &space->pannos.items[i];
        TlJson *json = tl_json_object();
        tl_json_object_set(json, "wall", tl_json_number(p->wall == TL_PANNO_FLOOR ? -1 : (double)p->wall));
        tl_json_object_set(json, "x", tl_json_number(p->at.x));
        tl_json_object_set(json, "y", tl_json_number(p->at.y));
        tl_json_object_set(json, "columns", tl_json_number(p->columns));
        tl_json_object_set(json, "rows", tl_json_number(p->rows));
        tl_json_object_set(json, "w", tl_json_number(p->tile_width));
        tl_json_object_set(json, "h", tl_json_number(p->tile_height));
        tl_json_object_set(json, "gap", tl_json_number(p->joint_width));
        tl_json_object_set(json, "color", tl_json_string(p->color));
        tl_json_object_set(json, "texture", tl_json_string(p->texture));
        tl_json_object_set(json, "price", tl_json_number(p->price));
        tl_json_array_push(array, json);
    }
    return array;
}

static void pannos_from_json(TlSpace *space, const TlJson *array)
{
    for (size_t i = 0; i < tl_json_length(array); i++) {
        const TlJson *json = tl_json_at(array, i);
        double wall = tl_json_number_or(tl_json_get(json, "wall"), -1);
        TlPanno p = tl_panno_default(wall < 0 ? TL_PANNO_FLOOR : (size_t)wall);
        p.at = tl_vec2(tl_json_number_or(tl_json_get(json, "x"), p.at.x), tl_json_number_or(tl_json_get(json, "y"), p.at.y));
        p.columns = (int)tl_json_number_or(tl_json_get(json, "columns"), p.columns);
        p.rows = (int)tl_json_number_or(tl_json_get(json, "rows"), p.rows);
        p.tile_width = tl_json_number_or(tl_json_get(json, "w"), p.tile_width);
        p.tile_height = tl_json_number_or(tl_json_get(json, "h"), p.tile_height);
        p.joint_width = tl_json_number_or(tl_json_get(json, "gap"), p.joint_width);
        p.price = tl_json_number_or(tl_json_get(json, "price"), 0);
        snprintf(p.color, sizeof p.color, "%s", tl_json_string_or(tl_json_get(json, "color"), p.color));
        snprintf(p.texture, sizeof p.texture, "%s", tl_json_string_or(tl_json_get(json, "texture"), ""));
        tl_panno_clamp(&p);
        tl_panno_list_push(&space->pannos, p);
    }
}

/* Участки пола: {"x", "y" — точка внутри, "name", "tiled", "tile"}. */
static TlJson *areas_to_json(const TlSpace *space)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < space->areas.count; i++) {
        const TlFloorArea *area = &space->areas.items[i];
        TlJson *json = tl_json_object();
        tl_json_object_set(json, "x", tl_json_number(area->anchor.x));
        tl_json_object_set(json, "y", tl_json_number(area->anchor.y));
        tl_json_object_set(json, "name", tl_json_string(area->name));
        tl_json_object_set(json, "tiled", tl_json_bool(area->tiled));
        tl_json_object_set(json, "tile", tile_to_json(&area->zone.tile));
        tl_json_array_push(array, json);
    }
    return array;
}

static void areas_from_json(TlSpace *space, const TlJson *array)
{
    for (size_t i = 0; i < tl_json_length(array); i++) {
        const TlJson *json = tl_json_at(array, i);
        TlFloorArea area = {.zone = tl_zone_factory(TL_ZONE_CARPET)};
        area.zone.enabled = true;
        area.anchor = tl_vec2(tl_json_number_or(tl_json_get(json, "x"), 0), tl_json_number_or(tl_json_get(json, "y"), 0));
        snprintf(area.name, sizeof area.name, "%s", tl_json_string_or(tl_json_get(json, "name"), "Участок"));
        area.tiled = tl_json_bool_or(tl_json_get(json, "tiled"), true);
        area.zone.tile = tile_from_json_over(tl_json_get(json, "tile"), space->tile);
        tl_floor_area_list_push(&space->areas, area);
    }
}

/* Строка чисел длины count; false — не такая. */
static bool numbers_from_row(const TlJson *row, double *values, size_t count)
{
    if (tl_json_length(row) != count)
        return false;
    for (size_t i = 0; i < count; i++) {
        values[i] = tl_json_number_or(tl_json_at(row, i), NAN);
        if (!isfinite(values[i]))
            return false;
    }
    return values[0] >= 0;
}

static void wall_items_from_json(TlSpace *space, const TlJson *json)
{
    double v[5];
    const TlJson *pipes = tl_json_get(json, "pipes"), *rulers = tl_json_get(json, "rulers");
    const TlJson *shifts = tl_json_get(json, "shifts");
    for (size_t i = 0; i < tl_json_length(pipes); i++)
        if (numbers_from_row(tl_json_at(pipes, i), v, 4) && v[3] > 0)
            tl_wall_pipe_list_push(&space->wall_pipes, (TlWallPipe){(size_t)v[0], {v[1], v[2]}, v[3]});
    for (size_t i = 0; i < tl_json_length(rulers); i++)
        if (numbers_from_row(tl_json_at(rulers, i), v, 5))
            tl_wall_ruler_list_push(&space->wall_rulers, (TlWallRuler){(size_t)v[0], {v[1], v[2]}, {v[3], v[4]}});
    for (size_t i = 0; i < tl_json_length(shifts); i++)
        if (numbers_from_row(tl_json_at(shifts, i), v, 3))
            tl_wall_shift_list_push(&space->wall_shifts, (TlWallShift){(size_t)v[0], {v[1], v[2]}});
}

/* ---- точки и списки ---------------------------------------------------- */

static TlJson *point_to_json(TlVec2 point)
{
    TlJson *pair = tl_json_array();
    tl_json_array_push(pair, tl_json_number(point.x));
    tl_json_array_push(pair, tl_json_number(point.y));
    return pair;
}

static TlVec2 point_from_json(const TlJson *pair)
{
    return tl_vec2(tl_json_number_or(tl_json_at(pair, 0), 0), tl_json_number_or(tl_json_at(pair, 1), 0));
}

static TlJson *polygon_to_json(const TlPolygon *polygon)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < polygon->count; i++)
        tl_json_array_push(array, point_to_json(polygon->items[i]));
    return array;
}

static TlPolygon polygon_from_json(const TlJson *array)
{
    TlPolygon polygon = {0};
    for (size_t i = 0; i < tl_json_length(array); i++)
        tl_polygon_push(&polygon, point_from_json(tl_json_at(array, i)));
    return polygon;
}

/* ---- сущности ------------------------------------------------------------ */

static TlJson *outline_to_json(const TlOutline *outline)
{
    TlJson *object = tl_json_object();
    TlJson *thickness = tl_json_array(), *locked = tl_json_array();
    for (size_t i = 0; i < outline->thickness.count; i++) {
        tl_json_array_push(thickness, tl_json_number(outline->thickness.items[i]));
        tl_json_array_push(locked, tl_json_bool(outline->locked.items[i]));
    }
    tl_json_object_set(object, "pts", polygon_to_json(&outline->inner));
    tl_json_object_set(object, "thick", thickness);
    tl_json_object_set(object, "locked", locked);
    return object;
}

static TlOutline *outline_from_json(const TlJson *object)
{
    TlPolygon corners = polygon_from_json(tl_json_get(object, "pts"));
    TlOutline *outline = tl_outline_new(corners.items, corners.count, 200);
    tl_polygon_free(&corners);
    const TlJson *thickness = tl_json_get(object, "thick"), *locked = tl_json_get(object, "locked");
    for (size_t i = 0; i < outline->thickness.count; i++) {
        outline->thickness.items[i] = tl_json_number_or(tl_json_at(thickness, i), 200);
        outline->locked.items[i] = tl_json_bool_or(tl_json_at(locked, i), false);
    }
    return outline;
}

static TlJson *partition_to_json(const TlPartition *partition)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p1", point_to_json(partition->start));
    tl_json_object_set(object, "p2", point_to_json(partition->end));
    tl_json_object_set(object, "thick", tl_json_number(tl_partition_thickness_at(partition, TL_PARTITION_START)));
    TlJson *corners = tl_json_array(); /* отступы углов тела от оси, по порядку TlPartitionCorner */
    for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++)
        tl_json_array_push(corners, tl_json_number(partition->corner_offsets[corner]));
    tl_json_object_set(object, "corners", corners);
    tl_json_object_set(object, "locked", tl_json_bool(partition->locked));
    return object;
}

/* Форма: четыре угла; в старых файлах — толщина и, может быть, толщина у конца. */
static void read_partition_shape(TlPartition *partition, const TlJson *object)
{
    const TlJson *corners = tl_json_get(object, "corners");
    if (tl_json_length(corners) == TL_PARTITION_CORNER_COUNT) {
        for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++)
            partition->corner_offsets[corner] = tl_json_number_or(tl_json_at(corners, (size_t)corner), 50);
        return;
    }
    double start_thickness = tl_partition_thickness_at(partition, TL_PARTITION_START);
    double end_thickness = tl_json_number_or(tl_json_get(object, "thick_end"), start_thickness);
    partition->corner_offsets[TL_PARTITION_END_LEFT] = end_thickness / 2;
    partition->corner_offsets[TL_PARTITION_END_RIGHT] = end_thickness / 2;
}

static TlPartition *partition_from_json(const TlJson *object)
{
    TlPartition *partition = tl_partition_new(point_from_json(tl_json_get(object, "p1")),
                                              point_from_json(tl_json_get(object, "p2")),
                                              tl_json_number_or(tl_json_get(object, "thick"), 100));
    read_partition_shape(partition, object);
    partition->locked = tl_json_bool_or(tl_json_get(object, "locked"), false);
    return partition;
}

static TlJson *ruler_to_json(const TlRuler *ruler)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p1", point_to_json(ruler->start));
    tl_json_object_set(object, "p2", point_to_json(ruler->end));
    tl_json_object_set(object, "name", tl_json_string(ruler->name));
    return object;
}

/* Имя без него в файле — пустое: проект даст следующее свободное. */
static void name_from_json(const TlJson *object, char *name)
{
    snprintf(name, TL_NAME_LENGTH, "%s", tl_json_string_or(tl_json_get(object, "name"), ""));
}

static TlRuler *ruler_from_json(const TlJson *object)
{
    TlRuler *ruler = tl_ruler_new(point_from_json(tl_json_get(object, "p1")), point_from_json(tl_json_get(object, "p2")));
    name_from_json(object, ruler->name);
    return ruler;
}

static TlJson *expansion_joint_to_json(const TlExpansionJoint *joint)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p1", point_to_json(joint->start));
    tl_json_object_set(object, "p2", point_to_json(joint->end));
    tl_json_object_set(object, "gap", tl_json_number(joint->gap));
    if (joint->kind == TL_LINE_DIVIDER)
        tl_json_object_set(object, "kind", tl_json_string("divider"));
    return object;
}

static TlExpansionJoint *expansion_joint_from_json(const TlJson *object)
{
    bool divider = strcmp(tl_json_string_or(tl_json_get(object, "kind"), ""), "divider") == 0;
    TlExpansionJoint *joint = tl_expansion_joint_new(
        point_from_json(tl_json_get(object, "p1")), point_from_json(tl_json_get(object, "p2")),
        tl_json_number_or(tl_json_get(object, "gap"), divider ? TL_DIVIDER_DEFAULT_GAP : TL_EXPANSION_JOINT_DEFAULT_GAP));
    joint->kind = divider ? TL_LINE_DIVIDER : TL_LINE_EXPANSION_JOINT;
    return joint;
}

static TlJson *fixture_to_json(const TlFixture *fixture)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "kind", tl_json_string(tl_fixture_kind_key(fixture->kind)));
    tl_json_object_set(object, "center", point_to_json(fixture->center));
    tl_json_object_set(object, "w", tl_json_number(fixture->width));
    tl_json_object_set(object, "d", tl_json_number(fixture->depth));
    tl_json_object_set(object, "h", tl_json_number(fixture->height));
    tl_json_object_set(object, "angle", tl_json_number(fixture->angle));
    tl_json_object_set(object, "under", tl_json_bool(fixture->under));
    tl_json_object_set(object, "behind", tl_json_bool(fixture->behind));
    if (fixture->kind == TL_FIXTURE_STAIRS)
        tl_json_object_set(object, "steps", tl_json_number(fixture->steps));
    return object;
}

static TlFixture fixture_from_json(const TlJson *object)
{
    TlFixture fixture = tl_fixture_default(tl_fixture_kind_from_key(tl_json_string_or(tl_json_get(object, "kind"), "")),
                                           point_from_json(tl_json_get(object, "center")));
    fixture.width = fmax(10, tl_json_number_or(tl_json_get(object, "w"), fixture.width));
    fixture.depth = fmax(10, tl_json_number_or(tl_json_get(object, "d"), fixture.depth));
    fixture.height = fmax(0, tl_json_number_or(tl_json_get(object, "h"), fixture.height));
    fixture.angle = tl_json_number_or(tl_json_get(object, "angle"), 0);
    fixture.under = tl_json_bool_or(tl_json_get(object, "under"), fixture.under);
    fixture.behind = tl_json_bool_or(tl_json_get(object, "behind"), fixture.behind);
    if (fixture.kind == TL_FIXTURE_STAIRS)
        fixture.steps = (int)fmax(1, fmin(50, tl_json_number_or(tl_json_get(object, "steps"), fixture.steps)));
    return fixture;
}

static TlJson *drain_to_json(const TlDrain *drain)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "kind", tl_json_string(tl_drain_kind_key(drain->kind)));
    tl_json_object_set(object, "center", point_to_json(drain->center));
    tl_json_object_set(object, "size", tl_json_number(drain->size));
    tl_json_object_set(object, "length", tl_json_number(drain->length));
    tl_json_object_set(object, "angle", tl_json_number(drain->angle));
    tl_json_object_set(object, "slope", tl_json_number(drain->slope));
    tl_json_object_set(object, "envelope", tl_json_bool(drain->envelope));
    return object;
}

static TlDrain drain_from_json(const TlJson *object)
{
    TlDrain drain = tl_drain_default(tl_drain_kind_from_key(tl_json_string_or(tl_json_get(object, "kind"), "")),
                                     point_from_json(tl_json_get(object, "center")));
    drain.size = fmax(10, tl_json_number_or(tl_json_get(object, "size"), drain.size));
    drain.length = fmax(10, tl_json_number_or(tl_json_get(object, "length"), drain.length));
    drain.angle = tl_json_number_or(tl_json_get(object, "angle"), 0);
    drain.slope = fmax(0, tl_json_number_or(tl_json_get(object, "slope"), drain.slope));
    drain.envelope = tl_json_bool_or(tl_json_get(object, "envelope"), drain.envelope);
    return drain;
}

static TlJson *pipe_to_json(const TlPipe *pipe)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p", point_to_json(pipe->center));
    tl_json_object_set(object, "d", tl_json_number(pipe->diameter));
    return object;
}

static TlPipe *pipe_from_json(const TlJson *object)
{
    double diameter = tl_json_number_or(tl_json_get(object, "d"), TL_PIPE_DEFAULT_DIAMETER);
    return tl_pipe_new(point_from_json(tl_json_get(object, "p")),
                       fmin(TL_PIPE_MAX_DIAMETER, fmax(TL_PIPE_MIN_DIAMETER, diameter)));
}

static TlJson *opening_to_json(const TlOpening *opening)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "x", tl_json_number(opening->center.x));
    tl_json_object_set(object, "y", tl_json_number(opening->center.y));
    tl_json_object_set(object, "width", tl_json_number(opening->width));
    tl_json_object_set(object, "threshold", tl_json_number(opening->threshold));
    tl_json_object_set(object, "threshold_end", tl_json_number(opening->threshold_end));
    tl_json_object_set(object, "threshold_gap", tl_json_number(opening->threshold_gap));
    tl_json_object_set(object, "locked", tl_json_bool(opening->locked));
    tl_json_object_set(object, "name", tl_json_string(opening->name));
    static const char *const KINDS[] = {"door", "window", "arch"};
    tl_json_object_set(object, "kind", tl_json_string(KINDS[opening->kind]));
    tl_json_object_set(object, "height", tl_json_number(opening->height));
    tl_json_object_set(object, "sill", tl_json_number(opening->sill));
    if (opening->reveal > 0)
        tl_json_object_set(object, "reveal", tl_json_number(opening->reveal));
    return object;
}

static TlOpening *opening_from_json(const TlJson *object)
{
    TlOpening *opening = tl_opening_new(tl_vec2(tl_json_number_or(tl_json_get(object, "x"), 0),
                                                tl_json_number_or(tl_json_get(object, "y"), 0)),
                                        tl_json_number_or(tl_json_get(object, "width"), 800));
    opening->threshold = tl_json_number_or(tl_json_get(object, "threshold"), TL_OPENING_DEFAULT_THRESHOLD);
    opening->threshold_end = tl_json_number_or(tl_json_get(object, "threshold_end"), opening->threshold);
    opening->threshold_gap =
        fmax(0, tl_json_number_or(tl_json_get(object, "threshold_gap"), TL_OPENING_DEFAULT_THRESHOLD_GAP));
    opening->locked = tl_json_bool_or(tl_json_get(object, "locked"), false);
    name_from_json(object, opening->name);
    const char *kind = tl_json_string_or(tl_json_get(object, "kind"), "door");
    tl_opening_set_kind(opening, strcmp(kind, "window") == 0 ? TL_OPENING_WINDOW
                                 : strcmp(kind, "arch") == 0 ? TL_OPENING_ARCH
                                                             : TL_OPENING_DOOR);
    opening->height = tl_json_number_or(tl_json_get(object, "height"), opening->height);
    opening->sill = tl_json_number_or(tl_json_get(object, "sill"), opening->sill);
    opening->reveal = fmax(0, tl_json_number_or(tl_json_get(object, "reveal"), 0));
    return opening;
}

static TlJson *space_to_json(const TlSpace *space)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "id", tl_json_number(space->id));
    tl_json_object_set(object, "name", tl_json_string(space->name));
    tl_json_object_set(object, "tile", tile_to_json(&space->tile));
    tl_json_object_set(object, "source", space->source_id ? tl_json_number(space->source_id) : tl_json_null());
    tl_json_object_set(object, "pts", polygon_to_json(&space->outline));
    tl_json_object_set(object, "zones", zones_to_json(space));
    tl_json_object_set(object, "height", tl_json_number(space->height));
    tl_json_object_set(object, "walls", walls_to_json(&space->walls));
    if (space->pannos.count)
        tl_json_object_set(object, "pannos", pannos_to_json(space));
    if (space->areas.count)
        tl_json_object_set(object, "areas", areas_to_json(space));
    if (space->wall_pipes.count || space->wall_rulers.count || space->wall_shifts.count)
        tl_json_object_set(object, "wall_items", wall_items_to_json(space));
    return object;
}

static TlSpace *space_from_json(const TlJson *object)
{
    TlSpace *space = tl_space_new((int)tl_json_number_or(tl_json_get(object, "id"), 0),
                                  tl_json_string_or(tl_json_get(object, "name"), "Комната"),
                                  tile_from_json(tl_json_get(object, "tile")));
    space->source_id = (int)tl_json_number_or(tl_json_get(object, "source"), 0);
    space->outline = polygon_from_json(tl_json_get(object, "pts"));
    zones_from_json(space, tl_json_get(object, "zones"));
    space->height = tl_json_number_or(tl_json_get(object, "height"), TL_ROOM_HEIGHT);
    walls_from_json(&space->walls, tl_json_get(object, "walls"));
    wall_items_from_json(space, tl_json_get(object, "wall_items"));
    pannos_from_json(space, tl_json_get(object, "pannos"));
    areas_from_json(space, tl_json_get(object, "areas"));
    return space;
}

/* ---- проект -------------------------------------------------------------- */

/* Подложка: положение, а с content — и картинка или линии DXF (плоские [x, y, x, y, …], мм). */
static TlJson *underlay_to_json(const TlUnderlay *underlay, bool content)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "origin", point_to_json(underlay->origin));
    tl_json_object_set(object, "scale", tl_json_number(underlay->scale));
    tl_json_object_set(object, "opacity", tl_json_number(underlay->opacity));
    tl_json_object_set(object, "visible", tl_json_bool(underlay->visible));
    if (!content)
        return object;
    if (underlay->image) {
        tl_json_object_set(object, "image", tl_json_string(underlay->image));
        tl_json_object_set(object, "image_w", tl_json_number(underlay->image_width));
        tl_json_object_set(object, "image_h", tl_json_number(underlay->image_height));
    }
    if (underlay->lines.count) {
        TlJson *lines = tl_json_array();
        for (size_t i = 0; i < underlay->lines.count; i++) {
            TlJson *line = tl_json_array();
            for (size_t k = 0; k < underlay->lines.items[i].count; k++) {
                TlVec2 p = underlay->lines.items[i].items[k];
                tl_json_array_push(line, tl_json_number(round(p.x * 10) / 10));
                tl_json_array_push(line, tl_json_number(round(p.y * 10) / 10));
            }
            tl_json_array_push(lines, line);
        }
        tl_json_object_set(object, "lines", lines);
    }
    return object;
}

static void underlay_from_json(TlUnderlay *underlay, const TlJson *object)
{
    *underlay = tl_underlay_default();
    if (tl_json_type(object) != TL_JSON_OBJECT)
        return;
    const char *image = tl_json_string_or(tl_json_get(object, "image"), NULL);
    int width = (int)tl_json_number_or(tl_json_get(object, "image_w"), 0);
    int height = (int)tl_json_number_or(tl_json_get(object, "image_h"), 0);
    if (image)
        tl_underlay_set_image(underlay, image, width, height);
    const TlJson *lines = tl_json_get(object, "lines");
    for (size_t i = 0; !underlay->image && i < tl_json_length(lines); i++) {
        const TlJson *line = tl_json_at(lines, i);
        TlPolygon polygon = {0};
        for (size_t k = 0; k + 1 < tl_json_length(line); k += 2)
            tl_polygon_push(&polygon, tl_vec2(tl_json_number_or(tl_json_at(line, k), 0),
                                              tl_json_number_or(tl_json_at(line, k + 1), 0)));
        if (polygon.count >= 2)
            tl_polygon_list_push(&underlay->lines, polygon);
        else
            tl_polygon_free(&polygon);
    }
    underlay->origin = point_from_json(tl_json_get(object, "origin"));
    double scale = tl_json_number_or(tl_json_get(object, "scale"), 1);
    underlay->scale = isfinite(scale) && scale > 1e-6 ? scale : 1;
    underlay->opacity = fmax(0.05, fmin(1, tl_json_number_or(tl_json_get(object, "opacity"), 0.5)));
    underlay->visible = tl_json_bool_or(tl_json_get(object, "visible"), true);
    underlay->placed = true;
}

static TlJson *project_to_json(const TlProject *project, bool content);

TlJson *tl_project_to_json(const TlProject *project) { return project_to_json(project, true); }

static TlJson *project_to_json(const TlProject *project, bool content)
{
    TlJson *root = tl_json_object();
    TlJson *outlines = tl_json_array(), *partitions = tl_json_array();
    TlJson *openings = tl_json_array(), *spaces = tl_json_array(), *rulers = tl_json_array();
    TlJson *expansion_joints = tl_json_array(), *pipes = tl_json_array();
    for (size_t i = 0; i < project->pipes.count; i++)
        tl_json_array_push(pipes, pipe_to_json(project->pipes.items[i]));
    for (size_t i = 0; i < project->outlines.count; i++)
        tl_json_array_push(outlines, outline_to_json(project->outlines.items[i]));
    for (size_t i = 0; i < project->partitions.count; i++)
        tl_json_array_push(partitions, partition_to_json(project->partitions.items[i]));
    for (size_t i = 0; i < project->openings.count; i++)
        tl_json_array_push(openings, opening_to_json(project->openings.items[i]));
    for (size_t i = 0; i < project->spaces.count; i++)
        tl_json_array_push(spaces, space_to_json(project->spaces.items[i]));
    for (size_t i = 0; i < project->rulers.count; i++)
        tl_json_array_push(rulers, ruler_to_json(project->rulers.items[i]));
    for (size_t i = 0; i < project->expansion_joints.count; i++)
        tl_json_array_push(expansion_joints, expansion_joint_to_json(project->expansion_joints.items[i]));
    tl_json_object_set(root, "version", tl_json_number(FORMAT_VERSION));
    tl_json_object_set(root, "outlines", outlines);
    tl_json_object_set(root, "partitions", partitions);
    tl_json_object_set(root, "openings", openings);
    tl_json_object_set(root, "spaces", spaces);
    tl_json_object_set(root, "rulers", rulers);
    tl_json_object_set(root, "expansion_joints", expansion_joints);
    tl_json_object_set(root, "pipes", pipes);
    if (project->drains.count) {
        TlJson *drains = tl_json_array();
        for (size_t i = 0; i < project->drains.count; i++)
            tl_json_array_push(drains, drain_to_json(&project->drains.items[i]));
        tl_json_object_set(root, "drains", drains);
    }
    if (project->fixtures.count) {
        TlJson *fixtures = tl_json_array();
        for (size_t i = 0; i < project->fixtures.count; i++)
            tl_json_array_push(fixtures, fixture_to_json(&project->fixtures.items[i]));
        tl_json_object_set(root, "fixtures", fixtures);
    }
    if (project->underlay.placed && (tl_underlay_present(&project->underlay) || !content))
        tl_json_object_set(root, "underlay", underlay_to_json(&project->underlay, content));
    tl_json_object_set(root, "next_id", tl_json_number(project->next_space_id));
    return root;
}

/* До версии 4 пол был только под проёмами перегородок, а у проёмов основных
   стен стык записывался, но не действовал: ставим им стык по умолчанию, чтобы
   проект остался прежним (в наружной стене — без пола). */
static void reset_wall_thresholds(TlProject *project)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        TlOpening *opening = project->openings.items[i];
        TlBand band;
        if (tl_project_opening_host(project, opening, &band) && band.host.kind == TL_HOST_WALL)
            tl_opening_set_threshold(opening, tl_project_default_threshold(project, opening));
    }
}

/* Файлы первых версий: каждая комната — отдельный контур с плиткой. */
static void load_legacy_rooms(TlProject *project, const TlJson *rooms)
{
    for (size_t i = 0; i < tl_json_length(rooms); i++) {
        const TlJson *room = tl_json_at(rooms, i);
        tl_project_add_outline(project, outline_from_json(room));
        TlSpace *space = tl_space_new(tl_project_take_space_id(project),
                                      tl_json_string_or(tl_json_get(room, "name"), "Комната"),
                                      tile_from_json(tl_json_get(room, "tile")));
        space->outline = polygon_from_json(tl_json_get(room, "pts"));
        tl_space_list_push(&project->spaces, space);
    }
}

bool tl_project_from_json(TlProject *project, const TlJson *root, char *error, size_t error_size)
{
    tl_project_clear(project);
    if (tl_json_type(root) != TL_JSON_OBJECT) {
        snprintf(error, error_size, "%s", "файл не похож на проект TileLayout");
        return false;
    }
    const TlJson *list;
    list = tl_json_get(root, "outlines");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_outline(project, outline_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "partitions");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_partition(project, partition_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "openings");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_opening(project, opening_from_json(tl_json_at(list, i)));
    if (tl_json_number_or(tl_json_get(root, "version"), 0) < 4)
        reset_wall_thresholds(project);
    list = tl_json_get(root, "expansion_joints");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_expansion_joint(project, expansion_joint_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "pipes");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_pipe(project, pipe_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "drains");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_drain_list_push(&project->drains, drain_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "fixtures");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_fixture_list_push(&project->fixtures, fixture_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "rulers");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_ruler(project, ruler_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "spaces");
    int max_id = 0;
    for (size_t i = 0; i < tl_json_length(list); i++) {
        TlSpace *space = space_from_json(tl_json_at(list, i));
        max_id = space->id > max_id ? space->id : max_id;
        tl_space_list_push(&project->spaces, space);
    }
    project->next_space_id = (int)tl_json_number_or(tl_json_get(root, "next_id"), max_id + 1);
    if (project->next_space_id <= max_id)
        project->next_space_id = max_id + 1;
    load_legacy_rooms(project, tl_json_get(root, "rooms"));
    if (tl_json_get(root, "underlay"))
        underlay_from_json(&project->underlay, tl_json_get(root, "underlay"));
    tl_project_merge_straight_nodes(project); /* узлы на прямой — одна перегородка */
    tl_project_update_spaces(project);
    return true;
}

char *tl_project_snapshot(const TlProject *project)
{
    TlJson *root = project_to_json(project, false);
    char *text = tl_json_stringify(root, false);
    tl_json_free(root);
    return text;
}

char *tl_project_to_string(const TlProject *project)
{
    TlJson *root = tl_project_to_json(project);
    char *text = tl_json_stringify(root, true);
    tl_json_free(root);
    return text;
}

bool tl_project_from_string(TlProject *project, const char *text, char *error, size_t error_size)
{
    TlJson *root = tl_json_parse(text, error, error_size);
    if (!root) {
        tl_project_clear(project);
        return false;
    }
    bool ok = tl_project_from_json(project, root, error, error_size);
    tl_json_free(root);
    return ok;
}

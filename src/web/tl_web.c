#include "tl_web.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_history.h"
#include "tl_layout.h"
#include "tl_report.h"
#include "tl_project_io.h"
#include "tl_web_internal.h"

static struct {
    TlProject *project;
    TlHistory *history;
    char error[TL_WEB_ERROR_SIZE];
    char *saved, *tile, *aligned;
} web;

TlProject *tl_web_project(void)
{
    if (!web.project)
        tl_web_new_demo();
    return web.project;
}

const char *tl_web_keep(char **slot, TlJson *json)
{
    free(*slot);
    *slot = tl_json_stringify(json, false);
    tl_json_free(json);
    return *slot;
}

TlJson *tl_web_number(double value) { return tl_json_number(round(value * 100) / 100); }

TlJson *tl_web_polygon(const TlPolygon *polygon)
{
    TlJson *points = tl_json_array();
    for (size_t i = 0; i < polygon->count; i++) {
        tl_json_array_push(points, tl_web_number(polygon->items[i].x));
        tl_json_array_push(points, tl_web_number(polygon->items[i].y));
    }
    return points;
}

void tl_web_checkpoint(const char *merge_key, double now_seconds)
{
    tl_history_checkpoint(web.history, tl_web_project(), merge_key, now_seconds);
}

/* ---- проект ---------------------------------------------------------------- */

static void replace_project(TlProject *project)
{
    tl_project_free(web.project);
    web.project = project;
    tl_web_underlay_touched();
    if (!web.history)
        web.history = tl_history_new();
    tl_history_clear(web.history);
}

void tl_web_new_demo(void) { replace_project(tl_project_demo()); }

bool tl_web_open(const char *json)
{
    TlProject *project = tl_project_new();
    if (!tl_project_from_string(project, json, web.error, sizeof web.error)) {
        tl_project_free(project);
        return false;
    }
    replace_project(project);
    return true;
}

const char *tl_web_error(void) { return web.error; }

char *tl_web_error_buffer(void) { return web.error; }

#ifndef TL_VERSION
#define TL_VERSION "?"
#endif

const char *tl_web_version(void) { return TL_VERSION; }

const char *tl_web_save(void)
{
    free(web.saved);
    web.saved = tl_project_to_string(tl_web_project());
    return web.saved;
}

int tl_web_space_at(double x, double y)
{
    TlSpace *space = tl_project_space_at(tl_web_project(), tl_vec2(x, y));
    return space ? space->id : 0;
}

/* ---- плитка комнаты ------------------------------------------------------- */

static TlJson *field_value(const TlTileSettings *tile, const TlTileField *field)
{
    const char *member = (const char *)tile + field->offset;
    switch (field->type) {
    case TL_FIELD_BOOL: return tl_json_bool(*(const bool *)member);
    case TL_FIELD_DOUBLE: return tl_json_number(*(const double *)member);
    case TL_FIELD_INT: return tl_json_number(*(const int *)member);
    case TL_FIELD_COLOR:
    case TL_FIELD_TEXT: return tl_json_string(member);
    }
    return tl_json_null();
}

TlJson *tl_web_tile_json(const TlTileSettings *tile)
{
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    TlJson *object = tl_json_object();
    for (size_t i = 0; i < count; i++)
        tl_json_object_set(object, fields[i].key, field_value(tile, &fields[i]));
    return object;
}

/* Стены комнаты: высота помещения, облицовка, полосы (как зоны: поля и плитка; spans — где кладутся). */
static TlJson *walls_json(TlSpace *space)
{
    TlJson *json = tl_json_object(), *bands = tl_json_object();
    tl_json_object_set(json, "room_height", tl_json_number(space->height));
    tl_json_object_set(json, "enabled", tl_json_bool(space->walls.enabled));
    tl_json_object_set(json, "height", tl_json_number(space->walls.height));
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++) {
        const TlZone *zone = &space->walls.bands[band];
        TlJson *item = tl_json_object(), *spans_json = tl_json_array();
        tl_json_object_set(item, "enabled", tl_json_bool(band == TL_WALL_MAIN || zone->enabled));
        tl_json_object_set(item, "width", tl_json_number(zone->width));
        tl_json_object_set(item, "dy", tl_json_number(zone->dy));
        tl_json_object_set(item, "fit", tl_json_bool(zone->fit));
        double spans[2][2];
        size_t count = tl_wall_band_spans(&space->walls, (TlWallBand)band, space->height, spans);
        for (size_t i = 0; i < count; i++) {
            tl_json_array_push(spans_json, tl_web_number(spans[i][0]));
            tl_json_array_push(spans_json, tl_web_number(spans[i][1]));
        }
        tl_json_object_set(item, "spans", spans_json);
        tl_json_object_set(item, "tile", tl_web_tile_json(&zone->tile));
        tl_json_object_set(bands, tl_wall_band_key((TlWallBand)band), item);
    }
    tl_json_object_set(json, "bands", bands);
    return json;
}

const char *tl_web_space_tile(int space_id)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    if (!space)
        return "null";
    TlJson *result = tl_json_object();
    tl_json_object_set(result, "space", tl_json_number(space->id));
    tl_json_object_set(result, "name", tl_json_string(space->name));
    tl_json_object_set(result, "source", tl_space_has_shared_floor(space) ? tl_json_string(space->layout_source->name)
                                                                          : tl_json_null());
    tl_json_object_set(result, "source_id", tl_json_number(space->source_id));
    TlJson *sources = tl_json_array();
    TlProject *project = tl_web_project();
    for (size_t i = 0; i < project->spaces.count; i++) {
        const TlSpace *other = project->spaces.items[i];
        if (other == space || other->layout_source != other) /* только комнаты со своей раскладкой */
            continue;
        TlJson *source = tl_json_object();
        tl_json_object_set(source, "id", tl_json_number(other->id));
        tl_json_object_set(source, "name", tl_json_string(other->name));
        tl_json_array_push(sources, source);
    }
    tl_json_object_set(result, "sources", sources);
    tl_json_object_set(result, "area", tl_json_number(round(tl_space_room_area(space) / 1e4) / 100));
    tl_json_object_set(result, "perimeter", tl_json_number(round(tl_space_room_perimeter(space))));
    const TlTileSettings *tile = tl_space_effective_tile(space);
    tl_json_object_set(result, "tile", tl_web_tile_json(tile));
    TlJson *formats = tl_json_array(); /* виды плитки рисунка: у модульного — несколько размеров */
    TlPatternUnit unit;
    for (size_t i = 0; tl_pattern_unit(tile, &unit) && i < unit.spec_count; i++) {
        char name[64];
        tl_tile_spec_name(&unit.specs[i], name, sizeof name);
        tl_json_array_push(formats, tl_json_string(name));
    }
    tl_json_object_set(result, "formats", formats);
    TlJson *zones = tl_json_object();
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++) {
        const TlZone *zone = &space->zones[kind];
        TlJson *json = tl_json_object();
        tl_json_object_set(json, "enabled", tl_json_bool(zone->enabled));
        tl_json_object_set(json, "width", tl_json_number(zone->width));
        tl_json_object_set(json, "height", tl_json_number(zone->height));
        tl_json_object_set(json, "dx", tl_json_number(zone->dx));
        tl_json_object_set(json, "dy", tl_json_number(zone->dy));
        tl_json_object_set(json, "fit", tl_json_bool(zone->fit));
        TlZone laid = tl_zone_effective(zone, (TlZoneKind)kind);
        tl_json_object_set(json, "laid_width", tl_json_number(round(laid.width * 10) / 10));
        tl_json_object_set(json, "laid_height", tl_json_number(round(laid.height * 10) / 10));
        tl_json_object_set(json, "tile", tl_web_tile_json(&zone->tile));
        tl_json_object_set(zones, tl_zone_key((TlZoneKind)kind), json);
    }
    tl_json_object_set(result, "zones", zones);
    tl_json_object_set(result, "walls", walls_json(space));
    TlJson *pannos = tl_json_array();
    for (size_t i = 0; i < space->pannos.count; i++) {
        const TlPanno *p = &space->pannos.items[i];
        TlJson *item = tl_json_object();
        size_t wall = p->wall == TL_PANNO_FLOOR ? SIZE_MAX : tl_space_wall_of_edge(space, p->wall);
        tl_json_object_set(item, "wall", tl_json_number(wall == SIZE_MAX ? -1 : (double)wall));
        tl_json_object_set(item, "x", tl_json_number(p->at.x));
        tl_json_object_set(item, "y", tl_json_number(p->at.y));
        tl_json_object_set(item, "columns", tl_json_number(p->columns));
        tl_json_object_set(item, "rows", tl_json_number(p->rows));
        tl_json_object_set(item, "w", tl_json_number(p->tile_width));
        tl_json_object_set(item, "h", tl_json_number(p->tile_height));
        tl_json_object_set(item, "gap", tl_json_number(p->joint_width));
        tl_json_object_set(item, "price", tl_json_number(p->price));
        tl_json_object_set(item, "color", tl_json_string(p->color));
        tl_json_object_set(item, "texture", tl_json_string(p->texture));
        tl_json_object_set(item, "width", tl_json_number(round(tl_panno_width(p))));
        tl_json_object_set(item, "height", tl_json_number(round(tl_panno_height(p))));
        tl_json_array_push(pannos, item);
    }
    tl_json_object_set(result, "pannos", pannos);
    TlJson *regions = tl_json_array(); /* части пола между линиями и их покрытие */
    for (size_t r = 0; space->regions.count > 1 && r < space->regions.count; r++) {
        size_t area = tl_space_area_of_region(space, r);
        TlJson *item = tl_json_object();
        tl_json_object_set(item, "index", tl_json_number((double)r));
        tl_json_object_set(item, "area", tl_json_number(round(tl_region_area(&space->regions.items[r]) / 1e4) / 100));
        tl_json_object_set(item, "covering", tl_json_string(area == SIZE_MAX ? "room" : space->areas.items[area].tiled ? "own" : "none"));
        tl_json_object_set(item, "name", tl_json_string(area == SIZE_MAX ? "" : space->areas.items[area].name));
        tl_json_object_set(item, "outline", tl_web_polygon(&space->regions.items[r].outline));
        if (area != SIZE_MAX)
            tl_json_object_set(item, "tile", tl_web_tile_json(&space->areas.items[area].zone.tile));
        tl_json_array_push(regions, item);
    }
    tl_json_object_set(result, "regions", regions);
    TlJson *doors_json = tl_json_array();
    TlOpeningList doors = tl_project_space_doors(project, space);
    for (size_t i = 0; i < doors.count; i++)
        for (size_t k = 0; k < project->openings.count; k++)
            if (project->openings.items[k] == doors.items[i]) {
                TlJson *door = tl_json_object();
                tl_json_object_set(door, "index", tl_json_number((double)k));
                tl_json_object_set(door, "name", tl_json_string(doors.items[i]->name));
                tl_json_array_push(doors_json, door);
            }
    tl_opening_list_free(&doors);
    tl_json_object_set(result, "doors", doors_json);
    TlJson *rulers_json = tl_json_array();
    TlRulerList rulers = tl_project_space_rulers(project, space);
    for (size_t i = 0; i < rulers.count; i++)
        for (size_t k = 0; k < project->rulers.count; k++)
            if (project->rulers.items[k] == rulers.items[i]) {
                TlJson *ruler = tl_json_object();
                tl_json_object_set(ruler, "index", tl_json_number((double)k));
                tl_json_object_set(ruler, "name", tl_json_string(rulers.items[i]->name));
                tl_json_object_set(ruler, "length", tl_json_number(round(tl_ruler_length(rulers.items[i]))));
                tl_json_array_push(rulers_json, ruler);
            }
    tl_ruler_list_free(&rulers);
    tl_json_object_set(result, "rulers", rulers_json);
    return tl_web_keep(&web.tile, result);
}

/* Значение value (JSON) — в поле; false — тип не подходит. */
static bool assign(TlTileSettings *tile, const TlTileField *field, const TlJson *value)
{
    char *member = (char *)tile + field->offset;
    TlJsonType type = tl_json_type(value);
    switch (field->type) {
    case TL_FIELD_BOOL:
        if (type != TL_JSON_BOOL)
            return false;
        *(bool *)member = tl_json_bool_or(value, false);
        return true;
    case TL_FIELD_DOUBLE:
    case TL_FIELD_INT: {
        double number = tl_json_number_or(value, NAN);
        if (type != TL_JSON_NUMBER || !isfinite(number))
            return false;
        if (field->type == TL_FIELD_INT)
            *(int *)member = (int)lround(number);
        else
            *(double *)member = number;
        return true;
    }
    case TL_FIELD_COLOR:
    case TL_FIELD_TEXT: {
        if (type != TL_JSON_STRING)
            return false;
        snprintf(member, field->size, "%s", tl_json_string_or(value, ""));
        return true;
    }
    }
    return false;
}

#define MAX_ROOMS 256

/* Поля из объекта fields (только известные ключи) в настройки tile; false — нет ни
   одного или значение не того типа. */
static bool apply_fields(TlTileSettings *tile, const TlJson *fields, bool *remembered)
{
    size_t count;
    const TlTileField *table = tl_tile_settings_fields(&count);
    bool any = false;
    for (size_t i = 0; i < count; i++) {
        const TlJson *value = tl_json_get(fields, table[i].key);
        if (!value)
            continue;
        if (!assign(tile, &table[i], value))
            return false;
        any = true;
        *remembered = *remembered || table[i].remembered_for_new_rooms;
    }
    return any;
}

/* Поля комнатам spaces — одним шагом отмены; все или ни одной. */
static bool set_fields_of(TlSpace *const *spaces, size_t count, const TlJson *fields, const char *merge_key,
                          double now_seconds)
{
    if (count == 0 || count > MAX_ROOMS || tl_json_type(fields) != TL_JSON_OBJECT)
        return false;
    /* в куче: у WebAssembly стек мал (64 КБ), а настройки комнаты — сотни байт */
    TlTileSettings *changed = malloc(count * sizeof *changed);
    bool remembered = false, ok = changed != NULL;
    for (size_t i = 0; ok && i < count; i++) {
        changed[i] = *tl_space_effective_tile(spaces[i]);
        ok = apply_fields(&changed[i], fields, &remembered);
    }
    if (ok) {
        tl_history_checkpoint(web.history, web.project, merge_key, now_seconds);
        for (size_t i = 0; i < count; i++)
            *tl_space_effective_tile(spaces[i]) = changed[i];
        if (remembered)
            tl_tile_settings_remember_as_default(&changed[0]);
    }
    free(changed);
    return ok;
}

static bool set_fields(int space_id, const TlJson *fields, const char *merge_key, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    return space && set_fields_of(&space, 1, fields, merge_key, now_seconds);
}

/* Комнаты по JSON-массиву id; 0 — пусто, нет такой комнаты или их слишком много. */
static size_t spaces_by_ids(const char *space_ids, TlSpace **spaces)
{
    TlJson *ids = tl_json_parse(space_ids, NULL, 0);
    size_t count = ids && tl_json_type(ids) == TL_JSON_ARRAY ? tl_json_length(ids) : 0;
    if (count > MAX_ROOMS)
        count = 0;
    for (size_t i = 0; i < count; i++) {
        spaces[i] = tl_project_space_by_id(tl_web_project(), (int)tl_json_number_or(tl_json_at(ids, i), 0));
        if (!spaces[i])
            count = 0;
    }
    tl_json_free(ids);
    return count;
}

bool tl_web_set_tile_field(int space_id, const char *key, const char *value, double now_seconds)
{
    TlJson *parsed = tl_json_parse(value, NULL, 0);
    if (!parsed)
        return false;
    TlJson *fields = tl_json_object();
    tl_json_object_set(fields, key, parsed);
    char merge_key[64];
    snprintf(merge_key, sizeof merge_key, "tile_%d_%s", space_id, key);
    bool ok = set_fields(space_id, fields, merge_key, now_seconds);
    tl_json_free(fields);
    return ok;
}

bool tl_web_set_tile_fields(int space_id, const char *fields, double now_seconds)
{
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    bool ok = parsed && set_fields(space_id, parsed, "", now_seconds);
    tl_json_free(parsed);
    return ok;
}

bool tl_web_set_rooms_tile_fields(const char *space_ids, const char *fields, double now_seconds)
{
    TlSpace *spaces[MAX_ROOMS];
    size_t count = spaces_by_ids(space_ids, spaces);
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    char merge_key[160] = ""; /* одно поле — серия правок склеивается */
    size_t field_count, present = 0;
    const TlTileField *table = tl_tile_settings_fields(&field_count);
    const char *key = NULL;
    for (size_t i = 0; parsed && i < field_count; i++)
        if (tl_json_get(parsed, table[i].key)) {
            present++;
            key = table[i].key;
        }
    if (present == 1)
        snprintf(merge_key, sizeof merge_key, "tile_%s_%s", space_ids, key);
    bool ok = count > 0 && parsed && set_fields_of(spaces, count, parsed, merge_key, now_seconds);
    tl_json_free(parsed);
    return ok;
}

/* Зона комнаты по ключу: "border", "carpet" — пола; "wall_lower", "wall_frieze", "wall_main" — полосы
   стен; "area:N" — участок на части пола N. NULL — нет такой. */
static TlZone *space_zone(TlSpace *space, const char *key)
{
    if (strncmp(key, "area:", 5) == 0) {
        size_t area = tl_space_area_of_region(space, (size_t)atoi(key + 5));
        return area == SIZE_MAX ? NULL : &space->areas.items[area].zone;
    }
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++)
        if (strcmp(tl_zone_key((TlZoneKind)kind), key) == 0)
            return &space->zones[kind];
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++)
        if (strncmp(key, "wall_", 5) == 0 && strcmp(key + 5, tl_wall_band_key((TlWallBand)band)) == 0)
            return &space->walls.bands[band];
    return NULL;
}

/* Поля зоны из объекта: геометрия ("enabled", "width", "height", "dx", "dy") и поля плитки. */
static bool apply_zone_fields(TlZone *zone, const TlJson *fields)
{
    static const char *const NUMBERS[] = {"width", "height", "dx", "dy"};
    double *const targets[] = {&zone->width, &zone->height, &zone->dx, &zone->dy};
    bool *const flags[] = {&zone->enabled, &zone->fit};
    static const char *const FLAGS[] = {"enabled", "fit"};
    for (size_t i = 0; i < 2; i++) {
        const TlJson *value = tl_json_get(fields, FLAGS[i]);
        if (!value)
            continue;
        if (tl_json_type(value) != TL_JSON_BOOL)
            return false;
        *flags[i] = tl_json_bool_or(value, false);
    }
    for (size_t i = 0; i < 4; i++) {
        const TlJson *value = tl_json_get(fields, NUMBERS[i]);
        if (!value)
            continue;
        double number = tl_json_number_or(value, NAN);
        if (tl_json_type(value) != TL_JSON_NUMBER || !isfinite(number))
            return false;
        *targets[i] = number;
    }
    bool any_tile_field = false; /* "enabled" — у зоны свой, не плитки */
    size_t count;
    const TlTileField *table = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++)
        any_tile_field |= strcmp(table[i].key, "enabled") != 0 && tl_json_get(fields, table[i].key) != NULL;
    bool remembered = false, was_enabled = zone->tile.enabled;
    bool ok = !any_tile_field || apply_fields(&zone->tile, fields, &remembered);
    zone->tile.enabled = was_enabled;
    return ok;
}

bool tl_web_set_zone_fields(const char *space_ids, const char *zone, const char *fields, double now_seconds)
{
    TlSpace *spaces[MAX_ROOMS];
    size_t count = spaces_by_ids(space_ids, spaces);
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    bool ok = count > 0 && space_zone(spaces[0], zone) && parsed && tl_json_type(parsed) == TL_JSON_OBJECT;
    TlZone *changed = ok ? malloc(count * sizeof *changed) : NULL; /* в куче: стек WebAssembly мал */
    ok = ok && changed;
    for (size_t i = 0; ok && i < count; i++) {
        changed[i] = *space_zone(spaces[i], zone);
        ok = apply_zone_fields(&changed[i], parsed);
    }
    if (ok) { /* одно поле — серия правок склеивается */
        char merge_key[200] = "";
        if (tl_json_length(parsed) == 1)
            snprintf(merge_key, sizeof merge_key, "zone_%s_%s_%s", zone, space_ids, tl_json_key_at(parsed, 0));
        tl_web_checkpoint(merge_key, now_seconds);
        for (size_t i = 0; i < count; i++)
            *space_zone(spaces[i], zone) = changed[i];
    }
    free(changed);
    tl_json_free(parsed);
    return ok;
}

bool tl_web_set_area(int space_id, int region, const char *covering, const char *name, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    if (!space || region < 0 || (size_t)region >= space->regions.count)
        return false;
    size_t area = tl_space_area_of_region(space, (size_t)region);
    bool room = strcmp(covering, "room") == 0, own = strcmp(covering, "own") == 0, none = strcmp(covering, "none") == 0;
    bool rename = !covering[0] && name[0] && area != SIZE_MAX;
    if (!room && !own && !none && !rename)
        return false;
    tl_web_checkpoint(name[0] ? "area_name" : "", now_seconds);
    if (room) {
        tl_space_remove_area(space, area);
        return true;
    }
    if (own || none)
        area = tl_space_add_area(space, (size_t)region, own);
    if (name[0] && area != SIZE_MAX)
        snprintf(space->areas.items[area].name, sizeof space->areas.items[area].name, "%s", name);
    return true;
}

bool tl_web_set_walls(const char *space_ids, const char *fields, double now_seconds)
{
    TlSpace *spaces[MAX_ROOMS];
    size_t count = spaces_by_ids(space_ids, spaces);
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    const TlJson *room_height = tl_json_get(parsed, "room_height"), *enabled = tl_json_get(parsed, "enabled");
    const TlJson *height = tl_json_get(parsed, "height");
    bool ok = count > 0 && parsed && tl_json_type(parsed) == TL_JSON_OBJECT &&
              (!room_height || tl_json_number_or(room_height, 0) >= 500) &&
              (!height || tl_json_number_or(height, -1) >= 0) && (!enabled || tl_json_type(enabled) == TL_JSON_BOOL);
    if (ok) {
        char merge_key[200] = "";
        if (tl_json_length(parsed) == 1)
            snprintf(merge_key, sizeof merge_key, "walls_%s_%s", space_ids, tl_json_key_at(parsed, 0));
        tl_web_checkpoint(merge_key, now_seconds);
        for (size_t i = 0; i < count; i++) {
            if (room_height)
                spaces[i]->height = tl_json_number_or(room_height, spaces[i]->height);
            if (enabled)
                spaces[i]->walls.enabled = tl_json_bool_or(enabled, false);
            if (height)
                spaces[i]->walls.height = tl_json_number_or(height, 0);
        }
    }
    tl_json_free(parsed);
    return ok;
}

bool tl_web_swap_tiles(const char *space_ids, double now_seconds)
{
    TlSpace *spaces[MAX_ROOMS];
    size_t count = spaces_by_ids(space_ids, spaces);
    if (count == 0)
        return false;
    tl_web_checkpoint("", now_seconds);
    for (size_t i = 0; i < count; i++) {
        TlTileSettings *tile = tl_space_effective_tile(spaces[i]);
        bool done = false; /* общий пол: настройки у нескольких комнат одни */
        for (size_t k = 0; k < i; k++)
            done = done || tl_space_effective_tile(spaces[k]) == tile;
        if (done)
            continue;
        double width = tile->tile_width;
        tile->tile_width = tile->tile_height;
        tile->tile_height = width;
        double real = tile->real_width; /* и фактический размер */
        tile->real_width = tile->real_height;
        tile->real_height = real;
    }
    tl_tile_settings_remember_as_default(tl_space_effective_tile(spaces[0]));
    return true;
}

/* ---- раскладка комнаты ---------------------------------------------------------- */

static const struct {
    const char *mode;
    TlLayoutCorner corner;
    const char *name;
} CORNERS[] = {{"top_left", TL_CORNER_TOP_LEFT, "левый верхний"},
               {"top_right", TL_CORNER_TOP_RIGHT, "правый верхний"},
               {"bottom_left", TL_CORNER_BOTTOM_LEFT, "левый нижний"},
               {"bottom_right", TL_CORNER_BOTTOM_RIGHT, "правый нижний"}};

static const char *aligned(bool ok, const char *message)
{
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "ok", tl_json_bool(ok));
    tl_json_object_set(json, "message", tl_json_string(message));
    return tl_web_keep(&web.aligned, json);
}

/* Смещение сетки — как у настольной программы: к одному шагу сетки, до 0,1 мм. */
static void set_offsets(TlSpace *space, double x, double y, double now_seconds)
{
    tl_web_checkpoint("", now_seconds);
    TlTileSettings *tile = tl_space_effective_tile(space);
    tl_layout_normalize_offsets(tile, &x, &y);
    tile->offset_x = round(x * 10) / 10;
    tile->offset_y = round(y * 10) / 10;
}

/* "door:N", "ruler_joint:N" — правило с номером предмета N в проекте; без номера — не тот. */
static bool numbered_mode(const char *mode, const char *prefix, size_t *index)
{
    size_t length = strlen(prefix);
    if (strncmp(mode, prefix, length) != 0 || (mode[length] != ':' && mode[length] != '\0'))
        return false;
    *index = mode[length] == ':' ? (size_t)strtoul(mode + length + 1, NULL, 10) : SIZE_MAX;
    return true;
}

static const char *align_to_door(TlProject *project, TlSpace *space, size_t index, double now_seconds)
{
    if (index >= project->openings.count)
        return aligned(false, "Выберите дверь комнаты — у неё нет проёма с полом");
    const TlOpening *door = project->openings.items[index];
    char message[200];
    TlVec2 from, to;
    double x, y;
    if (!tl_project_space_door_edge(project, space, door, &from, &to))
        return aligned(false, "Этот проём — не дверь выбранной комнаты");
    bool ok = tl_layout_door_offsets(space, from, to, &x, &y);
    if (ok) {
        set_offsets(space, x, y, now_seconds);
        snprintf(message, sizeof message, "От двери «%s»: от порога целый ряд, проём — посередине", door->name);
    } else {
        snprintf(message, sizeof message, "%s", "Проём наискосок к плитке — поверните раскладку вдоль стены");
    }
    return aligned(ok, message);
}

static const char *align_to_ruler(TlProject *project, TlSpace *space, size_t index, TlLineAlign mode,
                                  double now_seconds)
{
    if (index >= project->rulers.count)
        return aligned(false, "Поставьте в комнате линейку по оси — ниши, унитаза, окна");
    const TlRuler *ruler = project->rulers.items[index];
    char message[200];
    double x, y;
    bool ok = tl_layout_line_offsets(space, ruler->start, ruler->end, mode, tl_space_label_point(space), &x, &y);
    if (ok) {
        set_offsets(space, x, y, now_seconds);
        snprintf(message, sizeof message, "По линейке «%s»: %s", ruler->name,
                 mode == TL_LINE_JOINT ? "шов по оси" : "середина плитки по оси");
    } else {
        snprintf(message, sizeof message, "Линейка «%s» должна идти вдоль рядов плитки", ruler->name);
    }
    return aligned(ok, message);
}

const char *tl_web_align_tiles(int space_id, const char *mode, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlSpace *space = tl_project_space_by_id(project, space_id);
    if (!space)
        return aligned(false, "Нет такой комнаты");
    char message[200];
    double x, y;
    for (size_t i = 0; i < sizeof CORNERS / sizeof CORNERS[0]; i++)
        if (strcmp(mode, CORNERS[i].mode) == 0) {
            tl_layout_corner_offsets(space, CORNERS[i].corner, &x, &y);
            set_offsets(space, x, y, now_seconds);
            snprintf(message, sizeof message, "Раскладка от угла: %s", CORNERS[i].name);
            return aligned(true, message);
        }
    if (strcmp(mode, "center") == 0 || strcmp(mode, "center_joint") == 0) {
        bool joint = strcmp(mode, "center_joint") == 0;
        tl_layout_center_offsets(space, joint, &x, &y);
        set_offsets(space, x, y, now_seconds);
        return aligned(true, joint ? "В центре комнаты — пересечение швов" : "В центре комнаты — центр плитки");
    }
    if (strcmp(mode, "min_narrow") == 0) {
        if (!tl_layout_optimize_narrow(space, &x, &y))
            return aligned(false, "Слишком много плиток для перебора");
        set_offsets(space, x, y, now_seconds);
        return aligned(true, "Смещение подобрано: меньше узких подрезок");
    }
    if (strcmp(mode, "min_tiles") == 0) {
        TlTileCountResult result;
        if (!tl_report_optimize_count(project, space, &result))
            return aligned(false, "Слишком много плиток для перебора");
        set_offsets(space, result.offset_x, result.offset_y, now_seconds);
        if (result.after.tiles < result.before.tiles)
            snprintf(message, sizeof message, "Минимум плиток: было %d, стало %d (без запаса)",
                     result.before.tiles, result.after.tiles);
        else if (result.after.narrow < result.before.narrow)
            snprintf(message, sizeof message,
                     "Плиток меньше не получится (%d), зато узких подрезок стало %d вместо %d",
                     result.after.tiles, result.after.narrow, result.before.narrow);
        else
            snprintf(message, sizeof message, "Раскладка уже оптимальна: %d плиток (без запаса)",
                     result.after.tiles);
        return aligned(true, message);
    }
    size_t index;
    if (numbered_mode(mode, "door", &index))
        return align_to_door(project, space, index, now_seconds);
    if (numbered_mode(mode, "ruler_joint", &index))
        return align_to_ruler(project, space, index, TL_LINE_JOINT, now_seconds);
    if (numbered_mode(mode, "ruler_center", &index))
        return align_to_ruler(project, space, index, TL_LINE_TILE_CENTER, now_seconds);
    return aligned(false, "Неизвестное правило раскладки");
}

bool tl_web_set_space_name(int space_id, const char *name, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    if (!space || !name[0])
        return false;
    tl_web_checkpoint("", now_seconds);
    tl_space_set_name(space, name);
    return true;
}

bool tl_web_set_space_source(int space_id, int source_id, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlSpace *space = tl_project_space_by_id(project, space_id);
    if (!space || source_id == space_id || (source_id && !tl_project_space_by_id(project, source_id)))
        return false;
    tl_web_checkpoint("", now_seconds);
    space->source_id = source_id;
    tl_project_update_spaces(project);
    return true;
}

/* ---- отмена ----------------------------------------------------------------- */

bool tl_web_undo(void) { return tl_history_undo(web.history, tl_web_project()); }
bool tl_web_redo(void) { return tl_history_redo(web.history, tl_web_project()); }
int tl_web_undo_count(void) { return web.history ? (int)tl_history_undo_count(web.history) : 0; }
int tl_web_redo_count(void) { return web.history ? (int)tl_history_redo_count(web.history) : 0; }

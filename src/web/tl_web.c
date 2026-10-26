#include "tl_web.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_history.h"
#include "tl_project_io.h"
#include "tl_web_internal.h"

static struct {
    TlProject *project;
    TlHistory *history;
    char error[256];
    char *saved, *tile;
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

/* ---- проект ---------------------------------------------------------------- */

static void replace_project(TlProject *project)
{
    tl_project_free(web.project);
    web.project = project;
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
    tl_json_object_set(result, "tile", tl_web_tile_json(tl_space_effective_tile(space)));
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
        size_t size = field->type == TL_FIELD_COLOR ? TL_COLOR_LENGTH : TL_TEXTURE_LENGTH;
        snprintf(member, size, "%s", tl_json_string_or(value, ""));
        return true;
    }
    }
    return false;
}

/* Поля из объекта fields (только известные ключи) — одним шагом отмены. */
static bool set_fields(int space_id, const TlJson *fields, const char *merge_key, double now_seconds)
{
    TlSpace *space = tl_project_space_by_id(tl_web_project(), space_id);
    if (!space || tl_json_type(fields) != TL_JSON_OBJECT)
        return false;
    TlTileSettings changed = *tl_space_effective_tile(space);
    size_t count;
    const TlTileField *table = tl_tile_settings_fields(&count);
    bool remembered = false, any = false;
    for (size_t i = 0; i < count; i++) {
        const TlJson *value = tl_json_get(fields, table[i].key);
        if (!value)
            continue;
        if (!assign(&changed, &table[i], value))
            return false;
        any = true;
        remembered = remembered || table[i].remembered_for_new_rooms;
    }
    if (!any)
        return false;
    tl_history_checkpoint(web.history, web.project, merge_key, now_seconds);
    *tl_space_effective_tile(space) = changed;
    if (remembered)
        tl_tile_settings_remember_as_default(&changed);
    return true;
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

/* ---- отмена ----------------------------------------------------------------- */

bool tl_web_undo(void) { return tl_history_undo(web.history, tl_web_project()); }
bool tl_web_redo(void) { return tl_history_redo(web.history, tl_web_project()); }
int tl_web_undo_count(void) { return web.history ? (int)tl_history_undo_count(web.history) : 0; }
int tl_web_redo_count(void) { return web.history ? (int)tl_history_redo_count(web.history) : 0; }

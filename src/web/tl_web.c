#include "tl_web.h"

#include <math.h>
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
    char error[256];
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

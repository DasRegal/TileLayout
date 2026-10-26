/*
 * Сводка к покупке для браузера:
 *
 * {"floor_area": м², "tiled_area": м²,
 *  "types": [{"name": "600×300 мм", "tile": {настройки}, "rooms": ["Кухня", ...],
 *             "count": плиток без запаса, "whole": целых, "to_buy": с запасом,
 *             "area": м², "shared_saving": шт., "offcut_saving": шт.}],
 *  "rooms": [{"id", "name", "count", "glue_kg", "clips", "floor_area", "tiled_area", "tiled",
 *             "tile_name", "color", "texture", "source": "имя" | null, "rotated", "shared_tiles"}],
 *  "glue": {"total_kg": кг, "packs": [{"size": кг в мешке, "count": мешков}]},
 *  "clips": {"total": шт., "packs": [{"size": шт. в упаковке, "count": упаковок}]}}
 */
#include <math.h>

#include "tl_report.h"
#include "tl_web.h"
#include "tl_web_internal.h"

static TlJson *rounded(double value, double step) { return tl_json_number(round(value / step) * step); }

static TlJson *packs(const TlPackLineList *lines)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < lines->count; i++) {
        TlJson *pack = tl_json_object();
        tl_json_object_set(pack, "size", tl_json_number(lines->items[i].unit_size));
        tl_json_object_set(pack, "count", tl_json_number(lines->items[i].units));
        tl_json_array_push(array, pack);
    }
    return array;
}

static TlJson *type_json(const TlTileTypeSummary *type)
{
    TlJson *json = tl_json_object();
    char name[64];
    tl_report_tile_name(type->tile, name, sizeof name);
    tl_json_object_set(json, "name", tl_json_string(name));
    tl_json_object_set(json, "tile", tl_web_tile_json(type->tile));
    TlJson *rooms = tl_json_array();
    for (size_t i = 0; i < type->spaces.count; i++)
        tl_json_array_push(rooms, tl_json_string(type->spaces.items[i]->name));
    tl_json_object_set(json, "rooms", rooms);
    tl_json_object_set(json, "count", tl_json_number(type->count));
    tl_json_object_set(json, "whole", tl_json_number(type->whole));
    tl_json_object_set(json, "to_buy", tl_json_number(type->to_buy));
    tl_json_object_set(json, "area", rounded(type->area / 1e6, 0.01));
    tl_json_object_set(json, "shared_saving", tl_json_number(type->shared_saving));
    tl_json_object_set(json, "offcut_saving", tl_json_number(type->offcut_saving));
    return json;
}

static TlJson *room_json(const TlRoomSummary *room)
{
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "id", tl_json_number(room->space->id));
    tl_json_object_set(json, "name", tl_json_string(room->space->name));
    bool counted = room->tiled && room->layout && !room->layout->too_many;
    tl_json_object_set(json, "count", tl_json_number(counted ? tl_layout_total(room->layout) : 0));
    tl_json_object_set(json, "glue_kg", rounded(room->glue_kg, 0.1));
    tl_json_object_set(json, "clips", tl_json_number(room->clip_count));
    tl_json_object_set(json, "floor_area", rounded(tl_space_area(room->space) / 1e6, 0.01));
    tl_json_object_set(json, "tiled_area", rounded(counted ? room->layout->floor_area / 1e6 : 0, 0.01));
    tl_json_object_set(json, "tiled", tl_json_bool(room->tiled));
    const TlTileSettings *tile = tl_space_effective_tile(room->space);
    char name[64];
    tl_report_tile_name(tile, name, sizeof name);
    tl_json_object_set(json, "tile_name", tl_json_string(name));
    tl_json_object_set(json, "color", tl_json_string(tile->color));
    tl_json_object_set(json, "texture", tl_json_string(tile->texture));
    tl_json_object_set(json, "source", tl_space_has_shared_floor(room->space)
                                           ? tl_json_string(room->space->layout_source->name)
                                           : tl_json_null());
    tl_json_object_set(json, "rotated", tl_json_bool(tile->rotation_degrees != 0));
    tl_json_object_set(json, "shared_tiles", tl_json_number(room->shared_tiles));
    return json;
}

const char *tl_web_summary(void)
{
    static char *text;
    TlSummary summary = tl_report_summarize(tl_web_project());
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "floor_area", rounded(summary.floor_area / 1e6, 0.01));
    tl_json_object_set(json, "tiled_area", rounded(summary.tiled_area / 1e6, 0.01));
    TlJson *types = tl_json_array();
    for (size_t i = 0; i < summary.types.count; i++)
        tl_json_array_push(types, type_json(&summary.types.items[i]));
    tl_json_object_set(json, "types", types);
    TlJson *rooms = tl_json_array();
    for (size_t i = 0; i < summary.rooms.count; i++)
        tl_json_array_push(rooms, room_json(&summary.rooms.items[i]));
    tl_json_object_set(json, "rooms", rooms);
    TlJson *glue = tl_json_object();
    tl_json_object_set(glue, "total_kg", rounded(summary.glue_total_kg, 0.1));
    tl_json_object_set(glue, "packs", packs(&summary.glue));
    tl_json_object_set(json, "glue", glue);
    TlJson *clips = tl_json_object();
    tl_json_object_set(clips, "total", tl_json_number(summary.clip_total));
    tl_json_object_set(clips, "packs", packs(&summary.clips));
    tl_json_object_set(json, "clips", clips);
    tl_summary_free(&summary);
    return tl_web_keep(&text, json);
}

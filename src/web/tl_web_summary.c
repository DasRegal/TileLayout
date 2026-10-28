/*
 * Сводка к покупке для браузера:
 *
 * {"floor_area": м² (с полом под дверьми), "room_area": м² (комнат, без проёмов), "tiled_area": м²,
 *  "wall_area": м² облицовки стен,
 *  "types": [{"name": "600×300 мм", "color", "textured", "buy_area": м² к покупке, "tile": {настройки первой комнаты}, "rooms": ["Кухня", ...],
 *             "count": плиток без запаса, "whole": целых, "to_buy": с запасом, "packs": упаковок или 0,
 *             "area": м², "shared_saving": шт., "offcut_saving": шт.}],
 *  "rooms": [{"id", "name", "count", "glue_kg", "clips", "floor_area", "tiled_area", "wall_area", "tiled",
 *             "grout_kg", "primer_l", "joint_length": м,
 *             "tile_name", "pattern": "Ёлочка", "color", "texture", "source": "имя" | null, "rotated",
 *             "shared_tiles", "specs": [{"name", "color", "count"}] — плиток каждого вида рисунка}],
 *  "glue": {"total_kg": кг, "packs": [{"size": кг в мешке, "count": мешков, "price": ₽}]},
 *  "clips": {"total": шт., "packs": [...]}, "grout": {"total_kg", "packs"}, "primer": {"total_l", "packs"},
 *  "crosses": {"total", "packs"}, "waterproof": {"total_kg", "packs", "tape_m"},
 *  "profiles": {"thresholds_m", "thresholds": [планки], "corners_m", "corners": [...]}, "cut_m": пог. м резов,
 *  "estimate": {"lines": [{"name", "group": "materials" | "work", "color", "quantity", "unit", "price", "cost"}],
 *               "materials": ₽, "work": ₽, "total": ₽}}
 */
#include <math.h>

#include "tl_estimate.h"
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
        tl_json_object_set(pack, "price", tl_json_number(lines->items[i].price));
        tl_json_array_push(array, pack);
    }
    return array;
}

static TlJson *type_json(const TlTileTypeSummary *type)
{
    TlJson *json = tl_json_object();
    char name[64];
    tl_tile_spec_name(&type->spec, name, sizeof name);
    tl_json_object_set(json, "name", tl_json_string(name));
    tl_json_object_set(json, "color", tl_json_string(type->spec.color));
    tl_json_object_set(json, "textured", tl_json_bool(type->spec.textured));
    tl_json_object_set(json, "buy_area", rounded(type->to_buy * tl_tile_spec_area_m2(&type->spec), 0.01));
    tl_json_object_set(json, "tile", tl_web_tile_json(type->tile));
    TlJson *rooms = tl_json_array();
    for (size_t i = 0; i < type->spaces.count; i++)
        tl_json_array_push(rooms, tl_json_string(type->spaces.items[i]->name));
    tl_json_object_set(json, "rooms", rooms);
    tl_json_object_set(json, "count", tl_json_number(type->count));
    tl_json_object_set(json, "whole", tl_json_number(type->whole));
    tl_json_object_set(json, "to_buy", tl_json_number(type->to_buy));
    tl_json_object_set(json, "packs", tl_json_number(type->packs));
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
    tl_json_object_set(json, "count", tl_json_number(counted ? tl_space_tile_total(room->space) : 0));
    tl_json_object_set(json, "glue_kg", rounded(room->glue_kg, 0.1));
    tl_json_object_set(json, "clips", tl_json_number(room->clip_count));
    tl_json_object_set(json, "grout_kg", rounded(room->grout_kg, 0.01));
    tl_json_object_set(json, "primer_l", rounded(room->primer_l, 0.1));
    tl_json_object_set(json, "joint_length", rounded(counted ? tl_layout_joint_length(room->layout) / 1000 : 0, 0.1));
    tl_json_object_set(json, "floor_area", rounded(tl_space_room_area(room->space) / 1e6, 0.01));
    tl_json_object_set(json, "tiled_area", rounded(counted ? room->layout->floor_area / 1e6 : 0, 0.01));
    tl_json_object_set(json, "wall_area", rounded(room->wall_area / 1e6, 0.01));
    tl_json_object_set(json, "tiled", tl_json_bool(room->tiled));
    const TlTileSettings *tile = tl_space_effective_tile(room->space);
    char name[64];
    tl_report_tile_name(tile, name, sizeof name);
    tl_json_object_set(json, "tile_name", tl_json_string(name));
    tl_json_object_set(json, "pattern", tl_json_string(tl_pattern_of(tile)->name));
    TlJson *specs = tl_json_array();
    for (size_t s = 0; counted && s < room->layout->pattern.spec_count; s++) {
        const TlTileSpec *spec = &room->layout->pattern.specs[s];
        int count = 0;
        for (size_t t = 0; t < room->layout->tiles.count; t++)
            count += room->layout->tiles.items[t].spec == (int)s;
        TlJson *spec_json = tl_json_object();
        tl_tile_spec_name(spec, name, sizeof name);
        tl_json_object_set(spec_json, "name", tl_json_string(name));
        tl_json_object_set(spec_json, "color", tl_json_string(spec->color));
        tl_json_object_set(spec_json, "count", tl_json_number(count));
        tl_json_array_push(specs, spec_json);
    }
    tl_json_object_set(json, "specs", specs);
    tl_json_object_set(json, "color", tl_json_string(tile->color));
    tl_json_object_set(json, "texture", tl_json_string(tile->texture));
    tl_json_object_set(json, "source", tl_space_has_shared_floor(room->space)
                                           ? tl_json_string(room->space->layout_source->name)
                                           : tl_json_null());
    tl_json_object_set(json, "rotated", tl_json_bool(tile->rotation_degrees != 0));
    tl_json_object_set(json, "shared_tiles", tl_json_number(room->shared_tiles));
    return json;
}

static TlJson *estimate_json(const TlSummary *summary)
{
    TlEstimate estimate = tl_estimate_from_summary(summary);
    TlJson *json = tl_json_object(), *lines = tl_json_array();
    for (size_t i = 0; i < estimate.lines.count; i++) {
        const TlEstimateLine *line = &estimate.lines.items[i];
        TlJson *line_json = tl_json_object();
        tl_json_object_set(line_json, "name", tl_json_string(line->name));
        tl_json_object_set(line_json, "group", tl_json_string(line->group == TL_ESTIMATE_WORK ? "work" : "materials"));
        tl_json_object_set(line_json, "color", tl_json_string(line->color));
        tl_json_object_set(line_json, "quantity", rounded(line->quantity, 0.01));
        tl_json_object_set(line_json, "unit", tl_json_string(line->unit));
        tl_json_object_set(line_json, "price", tl_json_number(line->price));
        tl_json_object_set(line_json, "cost", rounded(line->cost, 0.01));
        tl_json_array_push(lines, line_json);
    }
    tl_json_object_set(json, "lines", lines);
    tl_json_object_set(json, "materials", rounded(estimate.materials, 0.01));
    tl_json_object_set(json, "work", rounded(estimate.work, 0.01));
    tl_json_object_set(json, "total", rounded(estimate.total, 0.01));
    tl_estimate_free(&estimate);
    return json;
}

const char *tl_web_summary(void)
{
    static char *text;
    TlSummary summary = tl_report_summarize(tl_web_project());
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "floor_area", rounded(summary.floor_area / 1e6, 0.01));
    tl_json_object_set(json, "room_area", rounded(summary.room_area / 1e6, 0.01));
    tl_json_object_set(json, "tiled_area", rounded(summary.tiled_area / 1e6, 0.01));
    tl_json_object_set(json, "wall_area", rounded(summary.wall_area / 1e6, 0.01));
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
    TlJson *grout = tl_json_object();
    tl_json_object_set(grout, "total_kg", rounded(summary.grout_total_kg, 0.01));
    tl_json_object_set(grout, "packs", packs(&summary.grout));
    tl_json_object_set(json, "grout", grout);
    TlJson *primer = tl_json_object();
    tl_json_object_set(primer, "total_l", rounded(summary.primer_total_l, 0.1));
    tl_json_object_set(primer, "packs", packs(&summary.primer));
    tl_json_object_set(json, "primer", primer);
    TlJson *crosses = tl_json_object();
    tl_json_object_set(crosses, "total", tl_json_number(summary.cross_total));
    tl_json_object_set(crosses, "packs", packs(&summary.crosses));
    tl_json_object_set(json, "crosses", crosses);
    TlJson *waterproof = tl_json_object();
    tl_json_object_set(waterproof, "total_kg", rounded(summary.waterproof_total_kg, 0.1));
    tl_json_object_set(waterproof, "packs", packs(&summary.waterproof));
    tl_json_object_set(waterproof, "tape_m", rounded(summary.tape_total_m, 0.1));
    tl_json_object_set(json, "waterproof", waterproof);
    TlJson *profiles = tl_json_object();
    tl_json_object_set(profiles, "thresholds_m", rounded(summary.threshold_total_mm / 1000, 0.01));
    tl_json_object_set(profiles, "thresholds", packs(&summary.thresholds));
    tl_json_object_set(profiles, "corners_m", rounded(summary.corner_total_mm / 1000, 0.01));
    tl_json_object_set(profiles, "corners", packs(&summary.corners));
    tl_json_object_set(json, "profiles", profiles);
    tl_json_object_set(json, "cut_m", rounded(summary.cut_total_mm / 1000, 0.1));
    tl_json_object_set(json, "estimate", estimate_json(&summary));
    tl_summary_free(&summary);
    return tl_web_keep(&text, json);
}

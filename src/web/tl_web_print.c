/*
 * Для печати и плана с номерами: номера плиток (сквозные, как в программе) и
 * карточки чертежей подрезок.
 *
 * tl_web_tile_numbers:
 * [{"space": id, "numbers": [x, y, номер, ширина куска, высота куска, …]}] — по
 * раскладке комнаты (основной пол и зоны): центр куска в мм плана, габарит
 * куска по осям плитки; "rotation" — поворот раскладки, °.
 *
 * tl_web_cut_cards:
 * [{"kind": "single" | "shared" | "offcut" | "drilled", "title", "subtitle", "narrow": bool,
 *   "tile": {"w", "h", "color", "min_cut"},
 *   "outline": [x0, y0, …] | null — плитка-многоугольник (шеврон, вставка) от угла габарита,
 *   "parts": [{"room", "number", "shapes": [[x0, y0, …], …], "drills": [[x, y, диаметр], …]}]}] —
 * контуры и центры отверстий от угла плитки, мм; первый контур части — внешний, далее дыры.
 */
#include <math.h>

#include "tl_report.h"
#include "tl_web.h"
#include "tl_web_internal.h"

/* Номера одной раскладки комнаты: у бордюра — поворот вдоль своей стены. */
static TlJson *layout_numbers(const TlSpace *space, const TlLayout *layout)
{
    TlJson *numbers = tl_json_array();
    for (size_t i = 0; i < layout->tiles.count; i++) {
        const TlTile *t = &layout->tiles.items[i];
        TlVec2 world = tl_affine_apply(&layout->local_to_world, t->center);
        bool whole = t->kind == TL_TILE_WHOLE;
        tl_json_array_push(numbers, tl_web_number(world.x));
        tl_json_array_push(numbers, tl_web_number(world.y));
        tl_json_array_push(numbers, tl_json_number(t->number));
        tl_json_array_push(numbers, tl_web_number(whole ? tl_rect_width(t->rect) : t->piece_width));
        tl_json_array_push(numbers, tl_web_number(whole ? tl_rect_height(t->rect) : t->piece_height));
    }
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "space", tl_json_number(space->id));
    const TlAffine *m = &layout->local_to_world;
    tl_json_object_set(json, "rotation", tl_json_number(atan2(m->b, m->a) * 180 / TL_PI));
    tl_json_object_set(json, "numbers", numbers);
    return json;
}

const char *tl_web_tile_numbers(void)
{
    static char *text;
    TlProject *project = tl_web_project();
    tl_report_assign_numbers(project);
    TlJson *layouts = tl_json_array();
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        if (!tl_space_effective_tile(space)->enabled)
            continue;
        for (size_t l = 0; l < tl_space_layout_count(space); l++) { /* основной пол и зоны */
            const TlLayout *layout = tl_space_layout_at(space, l);
            if (!layout->too_many)
                tl_json_array_push(layouts, layout_numbers(space, layout));
        }
    }
    return tl_web_keep(&text, layouts);
}

static const char *card_kind(TlCutCardKind kind)
{
    switch (kind) {
    case TL_CARD_SHARED_TILE: return "shared";
    case TL_CARD_OFFCUT_PAIR: return "offcut";
    case TL_CARD_DRILLED: return "drilled";
    default: return "single";
    }
}

const char *tl_web_cut_cards(void)
{
    static char *text;
    TlCutCardList cards = tl_report_cut_cards(tl_web_project());
    TlJson *list = tl_json_array();
    for (size_t c = 0; c < cards.count; c++) {
        const TlCutCard *card = &cards.items[c];
        TlJson *json = tl_json_object();
        tl_json_object_set(json, "kind", tl_json_string(card_kind(card->kind)));
        tl_json_object_set(json, "title", tl_json_string(card->title));
        tl_json_object_set(json, "subtitle", tl_json_string(card->subtitle));
        tl_json_object_set(json, "narrow", tl_json_bool(card->narrow));
        TlJson *tile = tl_json_object();
        tl_json_object_set(tile, "w", tl_json_number(card->width));
        tl_json_object_set(tile, "h", tl_json_number(card->height));
        tl_json_object_set(tile, "color", tl_json_string(card->color));
        tl_json_object_set(tile, "min_cut", tl_json_number(card->tile->min_cut_width));
        tl_json_object_set(json, "tile", tile);
        tl_json_object_set(json, "outline", card->outline.count ? tl_web_polygon(&card->outline) : tl_json_null());
        TlJson *parts = tl_json_array();
        for (size_t p = 0; p < card->parts.count; p++) {
            const TlCutPart *part = &card->parts.items[p];
            TlJson *part_json = tl_json_object();
            tl_json_object_set(part_json, "room", tl_json_string(part->room));
            tl_json_object_set(part_json, "number", tl_json_number(part->number));
            TlJson *shapes = tl_json_array();
            for (size_t i = 0; i < part->shapes.count; i++)
                tl_json_array_push(shapes, tl_web_polygon(&part->shapes.items[i]));
            tl_json_object_set(part_json, "shapes", shapes);
            TlJson *drills = tl_json_array();
            for (size_t i = 0; i < part->drills.count; i++) {
                TlJson *drill = tl_json_array();
                tl_json_array_push(drill, tl_web_number(part->drills.items[i].center.x));
                tl_json_array_push(drill, tl_web_number(part->drills.items[i].center.y));
                tl_json_array_push(drill, tl_json_number(part->drills.items[i].diameter));
                tl_json_array_push(drills, drill);
            }
            tl_json_object_set(part_json, "drills", drills);
            tl_json_array_push(parts, part_json);
        }
        tl_json_object_set(json, "parts", parts);
        tl_json_array_push(list, json);
    }
    tl_cut_card_list_free_all(&cards);
    return tl_web_keep(&text, list);
}

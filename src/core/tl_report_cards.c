/*
 * Карточки для чертежей подрезок. Плитка, разрезанная стеной между
 * комнатами с общей раскладкой, и две подрезки из одной плитки (хоть из
 * разных комнат) — на одной карточке; остальные подрезки — по одной. Целая
 * плитка с отверстиями под трубы — тоже карточка: где сверлить.
 */
#include "tl_report_groups.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct DoneKey {
    const void *owner;   /* источник группы или комната */
    size_t layout;       /* раскладка: основной пол или зона */
    TlCell cell;
} DoneKey;

TL_ARRAY_DEFINE(DoneKeyList, DoneKey, done_key_list)

static bool take_once(DoneKeyList *done, const void *owner, size_t layout, TlCell cell)
{
    for (size_t i = 0; i < done->count; i++)
        if (done->items[i].owner == owner && done->items[i].layout == layout &&
            tl_compare_cells(done->items[i].cell, cell) == 0)
            return false;
    done_key_list_push(done, (DoneKey){owner, layout, cell});
    return true;
}

static TlCutPart make_part(const char *room, int number, const TlTile *tile)
{
    TlCutPart part = {.number = number, .shapes = tl_polygon_list_deep_copy(&tile->shapes),
                      .drills = tl_circle_list_copy(&tile->drills), .miters = tl_polygon_list_deep_copy(&tile->miters)};
    if (part.shapes.count == 0) /* целая плитка — контур во всю плитку */
        tl_polygon_list_push(&part.shapes, tile->outline.count ? tl_polygon_copy(&tile->outline)
                                                               : tl_polygon_from_rect((TlRect){0, 0, tl_rect_width(tile->rect),
                                                                                               tl_rect_height(tile->rect)}));
    snprintf(part.room, sizeof part.room, "%s", room);
    return part;
}

/* Карточка по плитке, как она лежит: размер и цвет её вида. */
static TlCutCard card_for(TlCutCardKind kind, int number, const TlTileSettings *settings, const TlLayout *layout,
                          const TlTile *tile)
{
    TlCutCard card = {.kind = kind, .number = number, .tile = settings, .width = tl_rect_width(tile->rect),
                      .height = tl_rect_height(tile->rect), .outline = tl_polygon_copy(&tile->outline)};
    snprintf(card.color, sizeof card.color, "%s", tl_layout_spec(layout, tile)->color);
    return card;
}

static TlCutPart strip_part(const char *room, int number, TlRect rect)
{
    TlCutPart part = {.number = number};
    snprintf(part.room, sizeof part.room, "%s", room);
    tl_polygon_list_push(&part.shapes, tl_polygon_from_rect(rect));
    return part;
}

/* Две подрезки из одной плитки — каждая у своего края (у углового куска — в
   углу), отход между ними. across_width — бок о бок по ширине плитки. */
static void add_pair_parts(TlCutCard *card, bool across_width, const char *first_room, const TlTile *first,
                           double w2, double h2, const char *second_room, int second_number)
{
    double w = card->width, h = card->height, w1, h1;
    tl_tile_piece_size(first, &w1, &h1);
    if (fabs(w - h) < 0.5) { /* у квадрата куски поворачиваются: узкой стороной — по ширине */
        double s1 = fmin(w1, h1), l1 = fmax(w1, h1), s2 = fmin(w2, h2), l2 = fmax(w2, h2);
        w1 = s1, h1 = l1, w2 = s2, h2 = l2;
    }
    TlRect first_rect = {0, 0, w1, h1};
    TlRect second_rect = across_width ? (TlRect){w - w2, 0, w, h2} : (TlRect){0, h - h2, w2, h};
    tl_cut_part_list_push(&card->parts, strip_part(first_room, first->number, first_rect));
    tl_cut_part_list_push(&card->parts, strip_part(second_room, second_number, second_rect));
}

static bool lies_lengthwise(const TlTileSpec *spec) { return spec->laid_width >= spec->laid_height; }

static bool same_piece(const TlPieceRef *ref, const TlSpace *space, size_t layout, TlCell cell)
{
    return ref->space == space && ref->layout == layout && tl_compare_cells(ref->cell, cell) == 0;
}

static const TlOffcutPair *partner_of(const TlSummary *summary, const TlSpace *space, size_t layout, TlCell cell,
                                      TlPieceRef *mate)
{
    for (size_t i = 0; i < summary->offcut_pairs.count; i++) {
        const TlOffcutPair *pair = &summary->offcut_pairs.items[i];
        if (same_piece(&pair->first, space, layout, cell)) {
            *mate = pair->second;
            return pair;
        }
        if (same_piece(&pair->second, space, layout, cell)) {
            *mate = pair->first;
            return pair;
        }
    }
    return NULL;
}

/* Группа раскладки: основной пол — по комнате-источнику, зона — своя у комнаты. */
static TlLayoutGroup *group_of(TlLayoutGroupList *groups, const TlSpace *space, size_t layout)
{
    const TlSpace *source = layout == 0 ? space->layout_source : space;
    for (size_t i = 0; i < groups->count; i++)
        if (groups->items[i].source == source && groups->items[i].layout == layout)
            return &groups->items[i];
    return NULL;
}

/* «Кухня», «Кухня · бордюр», «Кухня · стена 2» или «Кухня · стена 2, фриз» — подпись части на карточке. */
static void layout_name(TlSpace *space, size_t layout, char *text, size_t size)
{
    size_t wall;
    TlWallBand band;
    int step;
    bool tread;
    if (tl_space_layout_wall(space, layout, &wall, &band) &&
        tl_space_wall_layout_stairs(space, layout - tl_space_floor_layout_count(space), &step, &tread) != SIZE_MAX) {
        snprintf(text, size, "%s · лестница, ступень %d, %s", space->name, step + 1, tread ? "проступь" : "подступенок");
        return;
    }
    if (tl_space_layout_wall(space, layout, &wall, &band)) {
        size_t reveal = tl_space_wall_layout_reveal(space, layout - tl_space_floor_layout_count(space));
        const TlWallHole *hole = reveal == SIZE_MAX ? NULL : tl_space_wall_hole_at(space, reveal);
        if (hole)
            snprintf(text, size, "%s · стена %zu, откос", space->name, wall + 1);
        else if (band == TL_WALL_MAIN)
            snprintf(text, size, "%s · стена %zu", space->name, wall + 1);
        else
            snprintf(text, size, "%s · стена %zu, %s", space->name, wall + 1, tl_wall_band_name(band));
        return;
    }
    size_t area = tl_space_layout_area(space, layout);
    if (area != SIZE_MAX) {
        snprintf(text, size, "%s · %s", space->name, space->areas.items[area].name);
        return;
    }
    TlZoneKind zone = tl_space_layout_zone(space, layout);
    if (zone == TL_ZONE_COUNT) {
        snprintf(text, size, "%s", space->name);
        return;
    }
    char lower[32];
    snprintf(lower, sizeof lower, "%s", zone == TL_ZONE_BORDER ? "бордюр" : "ковёр");
    snprintf(text, size, "%s · %s", space->name, lower);
}

static TlCutCard shared_tile_card(const TlLayoutGroup *group, const TlLayout *layout, const TlTile *tile,
                                  const TlTileSettings *settings)
{
    TlCutCard card = card_for(TL_CARD_SHARED_TILE, tile->number, settings, layout, tile);
    size_t first, owners = tl_group_cell_owners(group, tile->cell, &first);
    char rooms[200] = "";
    for (size_t k = 0; k < owners; k++) {
        const TlCellOwner *owner = &group->owners.items[first + k];
        tl_cut_part_list_push(&card.parts, make_part(owner->space->name, 0, owner->tile));
        size_t used = strlen(rooms);
        snprintf(rooms + used, sizeof rooms - used, "%s%s", k ? " + " : "", owner->space->name);
        card.narrow = card.narrow || owner->tile->kind == TL_TILE_NARROW;
    }
    snprintf(card.title, sizeof card.title, "№%d  %s", tile->number, rooms);
    snprintf(card.subtitle, sizeof card.subtitle, "одна плитка на %zu комнаты", owners);
    return card;
}

/* «, отверстия: Ø50, Ø110» — к подписи карточки. */
static void append_drills(char *text, size_t size, const TlTile *tile)
{
    for (size_t i = 0; i < tile->drills.count; i++) {
        size_t used = strlen(text);
        snprintf(text + used, size - used, "%sØ%g", i == 0 ? (used ? ", отверстия: " : "отверстия: ") : ", ",
                 tile->drills.items[i].diameter);
    }
}

static TlCutCard single_card(const char *name, const TlLayout *layout, const TlTile *tile,
                             const TlTileSettings *settings)
{
    bool whole = tile->kind == TL_TILE_WHOLE;
    TlCutCard card = card_for(whole ? TL_CARD_DRILLED : TL_CARD_SINGLE, tile->number, settings, layout, tile);
    card.narrow = tile->kind == TL_TILE_NARROW;
    tl_cut_part_list_push(&card.parts, make_part(name, tile->number, tile));
    snprintf(card.title, sizeof card.title, "№%d  %s", tile->number, name);
    if (whole)
        snprintf(card.subtitle, sizeof card.subtitle, "%s", "целая");
    else
        snprintf(card.subtitle, sizeof card.subtitle, "%.0f×%.0f, %s", tile->piece_width, tile->piece_height,
                 tile->figured ? "фигурная" : "прямая");
    append_drills(card.subtitle, sizeof card.subtitle, tile);
    return card;
}

static int compare_cards(const void *left, const void *right)
{
    const TlCutCard *a = left, *b = right;
    return a->number < b->number ? -1 : a->number > b->number;
}

static void add_layout_cards(TlCutCardList *cards, const TlSummary *summary, TlLayoutGroupList *groups,
                             DoneKeyList *done, TlSpace *space, size_t index)
{
    TlLayout *layout = tl_space_layout_at(space, index);
    TlLayoutGroup *group = group_of(groups, space, index);
    if (!layout || !group)
        return;
    const TlTileSettings *settings = tl_space_layout_tile(space, index);
    char name[128];
    layout_name(space, index, name, sizeof name);
    for (size_t t = 0; t < layout->tiles.count; t++) {
        const TlTile *tile = &layout->tiles.items[t];
        if (tile->kind == TL_TILE_WHOLE && tile->drills.count == 0)
            continue;
        if (tl_group_cell_owners(group, tile->cell, NULL) > 1) {
            if (take_once(done, group->source, index, tile->cell))
                tl_cut_card_list_push(cards, shared_tile_card(group, layout, tile, settings));
            continue;
        }
        TlPieceRef mate;
        const TlOffcutPair *pair = partner_of(summary, space, index, tile->cell, &mate);
        if (!pair) {
            tl_cut_card_list_push(cards, single_card(name, layout, tile, settings));
            continue;
        }
        if (!take_once(done, space, index, tile->cell))
            continue;
        take_once(done, mate.space, mate.layout, mate.cell);
        TlLayout *mate_layout = tl_space_layout_at(mate.space, mate.layout);
        const TlTile *mate_tile = mate_layout ? tl_layout_tile_at(mate_layout, mate.cell) : NULL;
        if (!mate_tile)
            continue;
        /* Карточка — в осях вида плитки; в другой комнате та же плитка может лежать
           повёрнутой: 300×600 вместо 600×300. */
        const TlTileSpec *spec = tl_layout_spec(layout, tile);
        double mate_w, mate_h;
        tl_tile_piece_size(mate_tile, &mate_w, &mate_h);
        if (lies_lengthwise(spec) != lies_lengthwise(tl_layout_spec(mate_layout, mate_tile))) {
            double swap = mate_w;
            mate_w = mate_h;
            mate_h = swap;
        }
        char mate_name[128];
        layout_name(mate.space, mate.layout, mate_name, sizeof mate_name);
        TlCutCard card = card_for(TL_CARD_OFFCUT_PAIR, tile->number < mate_tile->number ? tile->number : mate_tile->number,
                                  settings, layout, tile);
        card.width = spec->laid_width;
        card.height = spec->laid_height;
        card.narrow = tile->kind == TL_TILE_NARROW || mate_tile->kind == TL_TILE_NARROW;
        add_pair_parts(&card, pair->across_width, name, tile, mate_w, mate_h, mate_name, mate_tile->number);
        if (strcmp(name, mate_name) == 0)
            snprintf(card.title, sizeof card.title, "№%d + №%d  %s", tile->number, mate_tile->number, name);
        else
            snprintf(card.title, sizeof card.title, "№%d + №%d  %s + %s", tile->number, mate_tile->number, name,
                     mate_name);
        snprintf(card.subtitle, sizeof card.subtitle, "%s", "две подрезки из одной плитки");
        tl_cut_card_list_push(cards, card);
    }
}

TlCutCardList tl_report_cut_cards(TlProject *project)
{
    TlCutCardList cards = {0};
    TlSummary summary = tl_report_summarize(project); /* заодно сквозные номера */
    TlLayoutGroupList groups = tl_report_build_groups(project, NULL, NULL);
    DoneKeyList done = {0};

    for (size_t r = 0; r < summary.rooms.count; r++) {
        const TlRoomSummary *room = &summary.rooms.items[r];
        for (size_t l = 0; room->tiled && l < tl_space_layout_count(room->space); l++)
            if (tl_space_layout_counted(room->space, l))
                add_layout_cards(&cards, &summary, &groups, &done, room->space, l);
    }
    done_key_list_free(&done);
    tl_report_free_groups(&groups);
    tl_summary_free(&summary);
    if (cards.count > 1) /* без подрезок массива нет */
        qsort(cards.items, cards.count, sizeof(TlCutCard), compare_cards);
    return cards;
}

void tl_cut_card_list_free_all(TlCutCardList *cards)
{
    for (size_t i = 0; i < cards->count; i++) {
        for (size_t p = 0; p < cards->items[i].parts.count; p++) {
            tl_polygon_list_free_all(&cards->items[i].parts.items[p].shapes);
            tl_circle_list_free(&cards->items[i].parts.items[p].drills);
            tl_polygon_list_free_all(&cards->items[i].parts.items[p].miters);
        }
        tl_cut_part_list_free(&cards->items[i].parts);
        tl_polygon_free(&cards->items[i].outline);
    }
    tl_cut_card_list_free(cards);
}

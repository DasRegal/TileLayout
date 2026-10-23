/*
 * Карточки для чертежей подрезок. Плитка, разрезанная стеной между
 * комнатами с общей раскладкой, и две подрезки из одной плитки (хоть из
 * разных комнат) — на одной карточке; остальные подрезки — по одной.
 */
#include "tl_report_groups.h"

#include <stdio.h>
#include <string.h>

typedef struct DoneKey {
    const void *owner;   /* источник группы или комната */
    TlCell cell;
} DoneKey;

TL_ARRAY_DEFINE(DoneKeyList, DoneKey, done_key_list)

static bool take_once(DoneKeyList *done, const void *owner, TlCell cell)
{
    for (size_t i = 0; i < done->count; i++)
        if (done->items[i].owner == owner && tl_compare_cells(done->items[i].cell, cell) == 0)
            return false;
    done_key_list_push(done, (DoneKey){owner, cell});
    return true;
}

static TlCutPart make_part(const char *room, int number, const TlPolygonList *shapes)
{
    TlCutPart part = {.number = number, .shapes = tl_polygon_list_deep_copy(shapes)};
    snprintf(part.room, sizeof part.room, "%s", room);
    return part;
}

static TlCutPart strip_part(const char *room, int number, TlRect rect)
{
    TlCutPart part = {.number = number};
    snprintf(part.room, sizeof part.room, "%s", room);
    tl_polygon_list_push(&part.shapes, tl_polygon_from_rect(rect));
    return part;
}

static bool strip_across_width(const TlTileSettings *tile, double width, double height)
{
    return height >= tile->tile_height - 0.5 && width < tile->tile_width - 0.5;
}

/* Две полосы из одной плитки — у противоположных краёв, отход между ними. */
static void add_strip_parts(TlCutCard *card, const TlTileSettings *tile, const char *first_room,
                            const TlTile *first, const char *second_room, const TlTile *second)
{
    bool first_across = strip_across_width(tile, first->piece_width, first->piece_height);
    bool second_across = strip_across_width(tile, second->piece_width, second->piece_height);
    double w = tile->tile_width, h = tile->tile_height;
    if (first_across || second_across || tl_tile_settings_is_square(tile)) {
        double first_size = first_across ? first->piece_width : first->piece_height;
        double second_size = second_across ? second->piece_width : second->piece_height;
        tl_cut_part_list_push(&card->parts, strip_part(first_room, first->number, (TlRect){0, 0, first_size, h}));
        tl_cut_part_list_push(&card->parts, strip_part(second_room, second->number, (TlRect){w - second_size, 0, w, h}));
    } else {
        tl_cut_part_list_push(&card->parts, strip_part(first_room, first->number, (TlRect){0, 0, w, first->piece_height}));
        tl_cut_part_list_push(&card->parts, strip_part(second_room, second->number, (TlRect){0, h - second->piece_height, w, h}));
    }
}

static bool lies_lengthwise(const TlTileSettings *tile) { return tile->tile_width >= tile->tile_height; }

static const TlOffcutPair *partner_of(const TlSummary *summary, const TlSpace *space, TlCell cell,
                                      TlPieceRef *mate)
{
    for (size_t i = 0; i < summary->offcut_pairs.count; i++) {
        const TlOffcutPair *pair = &summary->offcut_pairs.items[i];
        if (pair->first.space == space && tl_compare_cells(pair->first.cell, cell) == 0) {
            *mate = pair->second;
            return pair;
        }
        if (pair->second.space == space && tl_compare_cells(pair->second.cell, cell) == 0) {
            *mate = pair->first;
            return pair;
        }
    }
    return NULL;
}

static TlLayoutGroup *group_of(TlLayoutGroupList *groups, const TlSpace *space)
{
    for (size_t i = 0; i < groups->count; i++)
        if (groups->items[i].source == space->layout_source)
            return &groups->items[i];
    return NULL;
}

static TlCutCard shared_tile_card(const TlLayoutGroup *group, const TlTile *tile, const TlTileSettings *settings)
{
    TlCutCard card = {.kind = TL_CARD_SHARED_TILE, .number = tile->number, .tile = settings};
    size_t first, owners = tl_group_cell_owners(group, tile->cell, &first);
    char rooms[200] = "";
    for (size_t k = 0; k < owners; k++) {
        const TlCellOwner *owner = &group->owners.items[first + k];
        tl_cut_part_list_push(&card.parts, make_part(owner->space->name, 0, &owner->tile->shapes));
        size_t used = strlen(rooms);
        snprintf(rooms + used, sizeof rooms - used, "%s%s", k ? " + " : "", owner->space->name);
        card.narrow = card.narrow || owner->tile->kind == TL_TILE_NARROW;
    }
    snprintf(card.title, sizeof card.title, "№%d  %s", tile->number, rooms);
    snprintf(card.subtitle, sizeof card.subtitle, "одна плитка на %zu комнаты", owners);
    return card;
}

static TlCutCard single_card(const TlSpace *space, const TlTile *tile, const TlTileSettings *settings)
{
    TlCutCard card = {.kind = TL_CARD_SINGLE, .number = tile->number, .tile = settings,
                      .narrow = tile->kind == TL_TILE_NARROW};
    tl_cut_part_list_push(&card.parts, make_part(space->name, tile->number, &tile->shapes));
    snprintf(card.title, sizeof card.title, "№%d  %s", tile->number, space->name);
    snprintf(card.subtitle, sizeof card.subtitle, "%.0f×%.0f, %s", tile->piece_width, tile->piece_height,
             tile->figured ? "фигурная" : "прямая");
    return card;
}

static int compare_cards(const void *left, const void *right)
{
    const TlCutCard *a = left, *b = right;
    return a->number < b->number ? -1 : a->number > b->number;
}

TlCutCardList tl_report_cut_cards(TlProject *project)
{
    TlCutCardList cards = {0};
    TlSummary summary = tl_report_summarize(project); /* заодно сквозные номера */
    TlLayoutGroupList groups = tl_report_build_groups(project, NULL, NULL);
    DoneKeyList done = {0};

    for (size_t r = 0; r < summary.rooms.count; r++) {
        const TlRoomSummary *room = &summary.rooms.items[r];
        TlLayoutGroup *group = group_of(&groups, room->space);
        if (!room->tiled || !group)
            continue;
        const TlTileSettings *settings = tl_space_effective_tile(room->space);
        for (size_t t = 0; t < room->layout->tiles.count; t++) {
            const TlTile *tile = &room->layout->tiles.items[t];
            if (tile->kind == TL_TILE_WHOLE)
                continue;
            if (tl_group_cell_owners(group, tile->cell, NULL) > 1) {
                if (take_once(&done, group->source, tile->cell))
                    tl_cut_card_list_push(&cards, shared_tile_card(group, tile, settings));
                continue;
            }
            TlPieceRef mate;
            if (partner_of(&summary, room->space, tile->cell, &mate)) {
                if (!take_once(&done, room->space, tile->cell))
                    continue;
                take_once(&done, mate.space, mate.cell);
                const TlTile *mate_tile = tl_layout_tile_at(tl_space_layout(mate.space), mate.cell);
                if (!mate_tile)
                    continue;
                /* В другой комнате та же плитка может лежать повёрнутой: 300×600 вместо 600×300. */
                TlTile turned = *mate_tile;
                if (lies_lengthwise(settings) != lies_lengthwise(tl_space_effective_tile(mate.space))) {
                    turned.piece_width = mate_tile->piece_height;
                    turned.piece_height = mate_tile->piece_width;
                    mate_tile = &turned;
                }
                TlCutCard card = {.kind = TL_CARD_OFFCUT_PAIR, .tile = settings,
                                  .number = tile->number < mate_tile->number ? tile->number : mate_tile->number,
                                  .narrow = tile->kind == TL_TILE_NARROW || mate_tile->kind == TL_TILE_NARROW};
                add_strip_parts(&card, settings, room->space->name, tile, mate.space->name, mate_tile);
                if (mate.space == room->space)
                    snprintf(card.title, sizeof card.title, "№%d + №%d  %s", tile->number,
                             mate_tile->number, room->space->name);
                else
                    snprintf(card.title, sizeof card.title, "№%d + №%d  %s + %s", tile->number,
                             mate_tile->number, room->space->name, mate.space->name);
                snprintf(card.subtitle, sizeof card.subtitle, "%s", "две подрезки из одной плитки");
                tl_cut_card_list_push(&cards, card);
                continue;
            }
            tl_cut_card_list_push(&cards, single_card(room->space, tile, settings));
        }
    }
    done_key_list_free(&done);
    tl_report_free_groups(&groups);
    tl_summary_free(&summary);
    qsort(cards.items, cards.count, sizeof(TlCutCard), compare_cards);
    return cards;
}

void tl_cut_card_list_free_all(TlCutCardList *cards)
{
    for (size_t i = 0; i < cards->count; i++) {
        for (size_t p = 0; p < cards->items[i].parts.count; p++)
            tl_polygon_list_free_all(&cards->items[i].parts.items[p].shapes);
        tl_cut_part_list_free(&cards->items[i].parts);
    }
    tl_cut_card_list_free(cards);
}

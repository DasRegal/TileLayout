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

static TlCutPart make_part(const char *room, int number, const TlTile *tile)
{
    TlCutPart part = {.number = number, .shapes = tl_polygon_list_deep_copy(&tile->shapes),
                      .drills = tl_circle_list_copy(&tile->drills)};
    if (part.shapes.count == 0) /* целая плитка — контур во всю плитку */
        tl_polygon_list_push(&part.shapes, tl_polygon_from_rect((TlRect){0, 0, tl_rect_width(tile->rect),
                                                                         tl_rect_height(tile->rect)}));
    snprintf(part.room, sizeof part.room, "%s", room);
    return part;
}

/* Карточка по плитке, как она лежит: размер и цвет её вида. */
static TlCutCard card_for(TlCutCardKind kind, int number, const TlTileSettings *settings, const TlLayout *layout,
                          const TlTile *tile)
{
    TlCutCard card = {.kind = kind, .number = number, .tile = settings, .width = tl_rect_width(tile->rect),
                      .height = tl_rect_height(tile->rect)};
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

static TlCutCard single_card(const TlSpace *space, const TlLayout *layout, const TlTile *tile,
                             const TlTileSettings *settings)
{
    bool whole = tile->kind == TL_TILE_WHOLE;
    TlCutCard card = card_for(whole ? TL_CARD_DRILLED : TL_CARD_SINGLE, tile->number, settings, layout, tile);
    card.narrow = tile->kind == TL_TILE_NARROW;
    tl_cut_part_list_push(&card.parts, make_part(space->name, tile->number, tile));
    snprintf(card.title, sizeof card.title, "№%d  %s", tile->number, space->name);
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
            if (tile->kind == TL_TILE_WHOLE && tile->drills.count == 0)
                continue;
            if (tl_group_cell_owners(group, tile->cell, NULL) > 1) {
                if (take_once(&done, group->source, tile->cell))
                    tl_cut_card_list_push(&cards, shared_tile_card(group, room->layout, tile, settings));
                continue;
            }
            TlPieceRef mate;
            const TlOffcutPair *pair = partner_of(&summary, room->space, tile->cell, &mate);
            if (pair) {
                if (!take_once(&done, room->space, tile->cell))
                    continue;
                take_once(&done, mate.space, mate.cell);
                TlLayout *mate_layout = tl_space_layout(mate.space);
                const TlTile *mate_tile = tl_layout_tile_at(mate_layout, mate.cell);
                if (!mate_tile)
                    continue;
                /* Карточка — в осях вида плитки; в другой комнате та же плитка может лежать
                   повёрнутой: 300×600 вместо 600×300. */
                const TlTileSpec *spec = tl_layout_spec(room->layout, tile);
                double mate_w, mate_h;
                tl_tile_piece_size(mate_tile, &mate_w, &mate_h);
                if (lies_lengthwise(spec) != lies_lengthwise(tl_layout_spec(mate_layout, mate_tile))) {
                    double swap = mate_w;
                    mate_w = mate_h;
                    mate_h = swap;
                }
                TlCutCard card = card_for(TL_CARD_OFFCUT_PAIR,
                                          tile->number < mate_tile->number ? tile->number : mate_tile->number,
                                          settings, room->layout, tile);
                card.width = spec->laid_width;
                card.height = spec->laid_height;
                card.narrow = tile->kind == TL_TILE_NARROW || mate_tile->kind == TL_TILE_NARROW;
                add_pair_parts(&card, pair->across_width, room->space->name, tile, mate_w, mate_h, mate.space->name,
                               mate_tile->number);
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
            tl_cut_card_list_push(&cards, single_card(room->space, room->layout, tile, settings));
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
        for (size_t p = 0; p < cards->items[i].parts.count; p++) {
            tl_polygon_list_free_all(&cards->items[i].parts.items[p].shapes);
            tl_circle_list_free(&cards->items[i].parts.items[p].drills);
        }
        tl_cut_part_list_free(&cards->items[i].parts);
    }
    tl_cut_card_list_free(cards);
}

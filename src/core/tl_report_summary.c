/* Сквозная нумерация, пары обрезков и сводный расчёт по проекту. */
#include "tl_report_groups.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"

/* ---- нумерация ------------------------------------------------------------- */

void tl_report_assign_numbers(TlProject *project)
{
    TlLayoutGroupList groups = tl_report_build_groups(project, NULL, NULL);
    int number = 0;
    for (size_t g = 0; g < groups.count; g++) {
        TlCellOwnerList *owners = &groups.items[g].owners; /* уже по рядам и столбцам */
        for (size_t i = 0; i < owners->count; i++) {
            bool new_cell = i == 0 || tl_compare_cells(owners->items[i].cell, owners->items[i - 1].cell) != 0;
            if (new_cell)
                number++;
            owners->items[i].tile->number = number;
        }
    }
    tl_report_free_groups(&groups);
}

/* ---- пары обрезков ----------------------------------------------------------
 *
 * Из одной плитки — два куска, каждый у своего края плитки: так у каждого
 * остаются заводские кромки к швам, а рез идёт к стене. Полоса на всю высоту
 * плитки встаёт в пару по ширине, полоса на всю ширину — по высоте; угловой
 * кусок (обрезан с двух сторон) — у угла плитки, ему подходит и то и другое.
 * Пары — жадно: самая широкая полоса с самой узкой, если вместе с запасом на
 * рез они помещаются в плитку. */

typedef struct Strip {
    double size;
    size_t piece;  /* номер в списке кусков */
} Strip;

TL_ARRAY_DEFINE(StripList, Strip, strip_list)

/* Список кусков, по которому сортируются полосы (для одинакового порядка на всех платформах). */
static const TlPieceList *sorting_pieces;

static int compare_strips(const void *left, const void *right)
{
    const Strip *a = left, *b = right;
    if (a->size != b->size)
        return a->size < b->size ? -1 : 1;
    const TlPieceRef *ra = &sorting_pieces->items[a->piece].ref, *rb = &sorting_pieces->items[b->piece].ref;
    if (ra->space->id != rb->space->id)
        return ra->space->id < rb->space->id ? -1 : 1;
    return tl_compare_cells(ra->cell, rb->cell);
}

static void pair_strips(const TlPieceList *pieces, StripList *strips, double full_length, double allowance,
                        bool across_width, bool *used, TlOffcutPairList *pairs)
{
    if (strips->count < 2)
        return;
    sorting_pieces = pieces;
    qsort(strips->items, strips->count, sizeof(Strip), compare_strips);
    size_t narrowest = 0, widest = strips->count - 1;
    while (narrowest < widest) {
        if (strips->items[narrowest].size + strips->items[widest].size + allowance <= full_length + 1e-6) {
            size_t a = strips->items[narrowest].piece, b = strips->items[widest].piece;
            TlOffcutPair pair = {pieces->items[a].ref, pieces->items[b].ref, across_width};
            tl_offcut_pair_list_push(pairs, pair);
            used[a] = used[b] = true;
            narrowest++;
        }
        widest--;
    }
}

typedef enum PieceShape { PIECE_WHOLE, PIECE_ACROSS_WIDTH, PIECE_ACROSS_HEIGHT, PIECE_CORNER } PieceShape;

static PieceShape piece_shape(const TlPiece *piece, double tile_width, double tile_height)
{
    bool full_height = piece->height >= tile_height - 0.5, full_width = piece->width >= tile_width - 0.5;
    if (full_height && full_width)
        return PIECE_WHOLE;      /* фигурный кусок во всю плитку — из остатка не выйдет */
    if (full_height)
        return PIECE_ACROSS_WIDTH;
    if (full_width)
        return PIECE_ACROSS_HEIGHT;
    return PIECE_CORNER;
}

/* Пары: сначала по одному направлению (угловые куски — туда), потом по другому
   (оставшиеся угловые). width_first — сначала по ширине плитки. */
static TlOffcutPairList pair_in_order(const TlPieceList *pieces, double tile_width, double tile_height,
                                      double allowance, bool square, bool width_first)
{
    TlOffcutPairList pairs = {0};
    bool *used = tl_calloc(pieces->count + 1, sizeof *used);
    for (int pass = 0; pass < 2; pass++) {
        bool across_width = (pass == 0) == width_first;
        StripList strips = {0};
        for (size_t i = 0; i < pieces->count; i++) {
            const TlPiece *piece = &pieces->items[i];
            PieceShape shape = piece_shape(piece, tile_width, tile_height);
            if (used[i] || shape == PIECE_WHOLE)
                continue;
            if (square) { /* у квадрата куски поворачиваются: всё — по одному направлению */
                if (pass == 0)
                    strip_list_push(&strips, (Strip){fmin(piece->width, piece->height), i});
                continue;
            }
            bool fits = shape == PIECE_CORNER || shape == (across_width ? PIECE_ACROSS_WIDTH : PIECE_ACROSS_HEIGHT);
            if (fits)
                strip_list_push(&strips, (Strip){across_width ? piece->width : piece->height, i});
        }
        pair_strips(pieces, &strips, across_width || square ? tile_width : tile_height, allowance,
                    across_width || square, used, &pairs);
        strip_list_free(&strips);
    }
    free(used);
    return pairs;
}

static TlOffcutPairList pair_pieces(const TlPieceList *pieces, double tile_width, double tile_height,
                                    double allowance, bool square)
{
    TlOffcutPairList width_first = pair_in_order(pieces, tile_width, tile_height, allowance, square, true);
    TlOffcutPairList height_first = pair_in_order(pieces, tile_width, tile_height, allowance, square, false);
    if (height_first.count > width_first.count) {
        tl_offcut_pair_list_free(&width_first);
        return height_first;
    }
    tl_offcut_pair_list_free(&height_first);
    return width_first;
}

TlOffcutPairList tl_report_offcut_pairs(const TlTileSettings *tile, const TlPieceList *pieces)
{
    if (!tile->reuse_offcuts)
        return (TlOffcutPairList){0};
    return pair_pieces(pieces, tile->tile_width, tile->tile_height, tile->cut_allowance,
                       tl_tile_settings_is_square(tile));
}

/* ---- сводка ---------------------------------------------------------------- */

void tl_report_tile_name(const TlTileSettings *tile, char *text, size_t size)
{
    double longer = fmax(tile->tile_width, tile->tile_height);
    double shorter = fmin(tile->tile_width, tile->tile_height);
    snprintf(text, size, "%g×%g мм", longer, shorter);
}

static bool same_tile_type(const TlTileSettings *a, const TlTileSettings *b)
{
    double a_long = fmax(a->tile_width, a->tile_height), a_short = fmin(a->tile_width, a->tile_height);
    double b_long = fmax(b->tile_width, b->tile_height), b_short = fmin(b->tile_width, b->tile_height);
    if (fabs(a_long - b_long) > 0.05 || fabs(a_short - b_short) > 0.05)
        return false;
    for (size_t i = 0; i < TL_COLOR_LENGTH && (a->color[i] || b->color[i]); i++)
        if (tolower((unsigned char)a->color[i]) != tolower((unsigned char)b->color[i]))
            return false;
    return true;
}

static TlTileTypeSummary *type_for(TlTileTypeSummaryList *types, const TlTileSettings *tile)
{
    for (size_t i = 0; i < types->count; i++)
        if (same_tile_type(types->items[i].tile, tile))
            return &types->items[i];
    TlTileTypeSummary type = {.tile = tile};
    tl_tile_type_summary_list_push(types, type);
    return tl_tile_type_summary_list_last(types);
}

static void add_to_pack_line(TlPackLineList *lines, double unit_size, double amount)
{
    for (size_t i = 0; i < lines->count; i++)
        if (lines->items[i].unit_size == unit_size) {
            lines->items[i].amount += amount;
            return;
        }
    TlPackLine line = {unit_size, amount, 0};
    tl_pack_line_list_push(lines, line);
}

static int compare_pack_lines(const void *left, const void *right)
{
    const TlPackLine *a = left, *b = right;
    return a->unit_size < b->unit_size ? -1 : a->unit_size > b->unit_size;
}

static void finish_pack_lines(TlPackLineList *lines)
{
    qsort(lines->items, lines->count, sizeof(TlPackLine), compare_pack_lines);
    for (size_t i = 0; i < lines->count; i++) {
        TlPackLine *line = &lines->items[i];
        line->units = line->unit_size > 0 ? (int)ceil(line->amount / line->unit_size - 1e-9) : 0;
    }
}

static int compare_types_by_area(const void *left, const void *right)
{
    const TlTileTypeSummary *a = left, *b = right;
    return a->area > b->area ? -1 : a->area < b->area;
}

/* Что копится по виду плитки, пока обходятся группы: куски для пар обрезков
 * из всех комнат с этим видом и плитки с весом запаса каждой комнаты. */
typedef struct TypeTally {
    TlPieceList pieces;          /* в осях плитки вида (type->tile) */
    double allowance;            /* запас на рез — наибольший из комнат */
    double reserved;             /* Σ плиток × (1 + запас) */
} TypeTally;

TL_ARRAY_DEFINE(TypeTallyList, TypeTally, type_tally_list)

static bool lies_lengthwise(const TlTileSettings *tile) { return tile->tile_width >= tile->tile_height; }

static void summarize_group(TlSummary *summary, TypeTallyList *tallies, const TlLayoutGroup *group)
{
    const TlTileSettings *tile = &group->source->tile;
    int unique = (int)tl_group_unique_cells(group);
    int naive = 0, whole = 0;
    double area = 0.0;
    for (size_t i = 0; i < group->layouts.count; i++) {
        naive += tl_layout_total(group->layouts.items[i]);
        area += group->layouts.items[i]->floor_area;
    }
    for (size_t i = 0; i < group->owners.count; i++) {
        size_t first, owners = tl_group_cell_owners(group, group->owners.items[i].cell, &first);
        if (first == i && owners == 1 && group->owners.items[i].tile->kind == TL_TILE_WHOLE)
            whole++;
    }

    TlTileTypeSummary *type = type_for(&summary->types, tile);
    size_t index = (size_t)(type - summary->types.items);
    while (tallies->count <= index)
        type_tally_list_push(tallies, (TypeTally){0});
    TypeTally *tally = &tallies->items[index];
    for (size_t i = 0; i < group->members.count; i++)
        tl_space_list_push(&type->spaces, group->members.items[i]);
    type->count += unique;
    type->whole += whole;
    type->area += area;
    type->shared_saving += naive - unique;
    tally->reserved += unique * (1 + tile->reserve_percent / 100.0);

    /* Остатки подрезок идут в подрезки любой комнаты с той же плиткой. Плитка
     * 600×300 в одной комнате и 300×600 в другой — одна: куски поворачиваются. */
    if (tile->reuse_offcuts) {
        bool turned = lies_lengthwise(tile) != lies_lengthwise(type->tile);
        TlPieceList pieces = tl_group_unshared_pieces(group);
        for (size_t i = 0; i < pieces.count; i++) {
            TlPiece piece = pieces.items[i];
            if (turned)
                piece = (TlPiece){piece.ref, piece.height, piece.width};
            tl_piece_list_push(&tally->pieces, piece);
        }
        tl_piece_list_free(&pieces);
        if (tile->cut_allowance > tally->allowance)
            tally->allowance = tile->cut_allowance;
    }

    for (size_t m = 0; m < group->members.count; m++) {
        TlRoomSummary *room = (TlRoomSummary *)tl_summary_room(summary, group->members.items[m]);
        TlLayout *layout = group->layouts.items[m];
        for (size_t t = 0; t < layout->tiles.count; t++)
            if (tl_group_cell_owners(group, layout->tiles.items[t].cell, NULL) > 1)
                room->shared_tiles++;
    }
}

/* Пары обрезков и запас — по виду плитки целиком: запас округляется один раз,
 * а не в каждой комнате. */
static void finish_type(TlSummary *summary, TlTileTypeSummary *type, TypeTally *tally)
{
    const TlTileSettings *tile = type->tile;
    TlOffcutPairList pairs = pair_pieces(&tally->pieces, tile->tile_width, tile->tile_height, tally->allowance,
                                         tl_tile_settings_is_square(tile));
    double reserve = type->count > 0 ? tally->reserved / type->count : 1.0;
    type->offcut_saving = (int)pairs.count;
    type->count -= (int)pairs.count;
    type->to_buy = (int)ceil(type->count * reserve - 1e-9);
    for (size_t i = 0; i < pairs.count; i++)
        tl_offcut_pair_list_push(&summary->offcut_pairs, pairs.items[i]);
    tl_offcut_pair_list_free(&pairs);
    tl_piece_list_free(&tally->pieces);
}

TlSummary tl_report_summarize(TlProject *project)
{
    TlSummary summary = {0};
    tl_report_assign_numbers(project);
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        TlLayout *layout = tl_space_layout(space);
        TlRoomSummary room = {space, layout, tl_space_effective_tile(space)->enabled && !layout->too_many, 0, 0, 0};
        summary.floor_area += tl_space_area(space);
        summary.room_area += tl_space_room_area(space);
        if (room.tiled) {
            const TlTileSettings *tile = tl_space_effective_tile(space);
            room.glue_kg = tl_layout_glue_kg(space, layout);
            room.clip_count = (int)layout->clip_points.count;
            summary.tiled_area += layout->floor_area;
            add_to_pack_line(&summary.glue, tile->glue_bag_kg, room.glue_kg);
            add_to_pack_line(&summary.clips, tile->clips_per_pack, room.clip_count);
            summary.glue_total_kg += room.glue_kg;
            summary.clip_total += room.clip_count;
        }
        tl_room_summary_list_push(&summary.rooms, room);
    }
    finish_pack_lines(&summary.glue);
    finish_pack_lines(&summary.clips);

    TlLayoutGroupList groups = tl_report_build_groups(project, NULL, NULL);
    TypeTallyList tallies = {0};
    for (size_t g = 0; g < groups.count; g++)
        summarize_group(&summary, &tallies, &groups.items[g]);
    for (size_t i = 0; i < summary.types.count; i++)
        finish_type(&summary, &summary.types.items[i], &tallies.items[i]);
    type_tally_list_free(&tallies);
    tl_report_free_groups(&groups);
    qsort(summary.types.items, summary.types.count, sizeof(TlTileTypeSummary), compare_types_by_area);
    return summary;
}

const TlRoomSummary *tl_summary_room(const TlSummary *summary, const TlSpace *space)
{
    for (size_t i = 0; i < summary->rooms.count; i++)
        if (summary->rooms.items[i].space == space)
            return &summary->rooms.items[i];
    return NULL;
}

void tl_summary_free(TlSummary *summary)
{
    for (size_t i = 0; i < summary->types.count; i++)
        tl_space_list_free(&summary->types.items[i].spaces);
    tl_room_summary_list_free(&summary->rooms);
    tl_tile_type_summary_list_free(&summary->types);
    tl_pack_line_list_free(&summary->glue);
    tl_pack_line_list_free(&summary->clips);
    tl_offcut_pair_list_free(&summary->offcut_pairs);
}

/* Сквозная нумерация, пары обрезков и сводный расчёт по проекту. */
#include "tl_report_groups.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

/* ---- пары обрезков ---------------------------------------------------------- */

typedef struct Strip {
    double size;
    TlPieceRef ref;
} Strip;

TL_ARRAY_DEFINE(StripList, Strip, strip_list)

static int compare_strips(const void *left, const void *right)
{
    const Strip *a = left, *b = right;
    if (a->size != b->size)
        return a->size < b->size ? -1 : 1;
    if (a->ref.space->id != b->ref.space->id) /* для одинакового результата на всех платформах */
        return a->ref.space->id < b->ref.space->id ? -1 : 1;
    return tl_compare_cells(a->ref.cell, b->ref.cell);
}

static void pair_strips(StripList *strips, double full_length, double allowance, TlOffcutPairList *pairs)
{
    qsort(strips->items, strips->count, sizeof(Strip), compare_strips);
    if (strips->count < 2)
        return;
    size_t narrowest = 0, widest = strips->count - 1;
    while (narrowest < widest) {
        if (strips->items[narrowest].size + strips->items[widest].size + allowance <= full_length + 1e-6) {
            TlOffcutPair pair = {strips->items[narrowest].ref, strips->items[widest].ref};
            tl_offcut_pair_list_push(pairs, pair);
            narrowest++;
        }
        widest--;
    }
}

TlOffcutPairList tl_report_offcut_pairs(const TlTileSettings *tile, const TlPieceList *pieces)
{
    TlOffcutPairList pairs = {0};
    if (!tile->reuse_offcuts)
        return pairs;
    StripList across_width = {0}, across_height = {0};
    for (size_t i = 0; i < pieces->count; i++) {
        const TlPiece *piece = &pieces->items[i];
        bool full_height = piece->height >= tile->tile_height - 0.5;
        bool full_width = piece->width >= tile->tile_width - 0.5;
        if (full_height && !full_width)        /* полоса на всю высоту плитки */
            strip_list_push(&across_width, (Strip){piece->width, piece->ref});
        else if (full_width && !full_height)   /* полоса на всю ширину */
            strip_list_push(&across_height, (Strip){piece->height, piece->ref});
    }
    if (tl_tile_settings_is_square(tile)) {    /* у квадрата полосы взаимозаменяемы */
        for (size_t i = 0; i < across_height.count; i++)
            strip_list_push(&across_width, across_height.items[i]);
        across_height.count = 0;
    }
    pair_strips(&across_width, tile->tile_width, tile->cut_allowance, &pairs);
    pair_strips(&across_height, tile->tile_height, tile->cut_allowance, &pairs);
    strip_list_free(&across_width);
    strip_list_free(&across_height);
    return pairs;
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

static void summarize_group(TlSummary *summary, const TlLayoutGroup *group)
{
    const TlTileSettings *tile = &group->source->tile;
    TlPieceList pieces = tl_group_unshared_pieces(group);
    TlOffcutPairList pairs = tl_report_offcut_pairs(tile, &pieces);
    tl_piece_list_free(&pieces);

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
    int count = unique - (int)pairs.count;
    for (size_t i = 0; i < group->members.count; i++)
        tl_space_list_push(&type->spaces, group->members.items[i]);
    type->count += count;
    type->whole += whole;
    type->to_buy += (int)ceil(count * (1 + tile->reserve_percent / 100.0) - 1e-9);
    type->area += area;
    type->shared_saving += naive - unique;
    type->offcut_saving += (int)pairs.count;
    for (size_t i = 0; i < pairs.count; i++)
        tl_offcut_pair_list_push(&summary->offcut_pairs, pairs.items[i]);
    tl_offcut_pair_list_free(&pairs);

    for (size_t m = 0; m < group->members.count; m++) {
        TlRoomSummary *room = (TlRoomSummary *)tl_summary_room(summary, group->members.items[m]);
        TlLayout *layout = group->layouts.items[m];
        for (size_t t = 0; t < layout->tiles.count; t++)
            if (tl_group_cell_owners(group, layout->tiles.items[t].cell, NULL) > 1)
                room->shared_tiles++;
    }
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
    for (size_t g = 0; g < groups.count; g++)
        summarize_group(&summary, &groups.items[g]);
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

/* Карта раскроя: что резать стопкой и какие две подрезки — из одной плитки. */
#include "tl_report.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static int piece_number(const TlPieceRef *ref)
{
    TlLayout *layout = tl_space_layout_at(ref->space, ref->layout);
    const TlTile *tile = layout ? tl_layout_tile_at(layout, ref->cell) : NULL;
    return tile ? tile->number : 0;
}

static bool same_ref(const TlPieceRef *a, const TlPieceRef *b)
{
    return a->space == b->space && a->layout == b->layout && tl_compare_cells(a->cell, b->cell) == 0;
}

static int compare_ints(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int compare_stacks(const void *a, const void *b)
{
    const TlCutStack *x = a, *y = b;
    if (x->numbers.count != y->numbers.count)
        return x->numbers.count > y->numbers.count ? -1 : 1;
    if (x->width != y->width)
        return x->width > y->width ? -1 : 1;
    return (x->height < y->height) - (x->height > y->height);
}

/* Ширина полосы: сторона куска, которая короче стороны плитки (у полосы на всю длину — другая). */
static double strip_size(const TlPiece *piece, const TlTileSpec *spec)
{
    return piece->width >= spec->laid_width - 0.5 ? piece->height : piece->width;
}

TlCutMapTypeList tl_report_cut_map(const TlSummary *summary)
{
    TlCutMapTypeList map = {0};
    for (size_t t = 0; t < summary->types.count; t++) {
        const TlTileTypeSummary *type = &summary->types.items[t];
        const TlPieceList *pieces = type->pieces;
        if (!pieces || pieces->count == 0)
            continue;
        TlCutMapType entry = {.type = type};
        bool *paired = calloc(pieces->count, sizeof *paired);
        for (size_t p = 0; p < summary->offcut_pairs.count; p++) { /* пары — у кусков этого вида */
            const TlOffcutPair *pair = &summary->offcut_pairs.items[p];
            size_t first = SIZE_MAX, second = SIZE_MAX;
            for (size_t i = 0; i < pieces->count; i++) {
                if (same_ref(&pieces->items[i].ref, &pair->first))
                    first = i;
                if (same_ref(&pieces->items[i].ref, &pair->second))
                    second = i;
            }
            if (first == SIZE_MAX || second == SIZE_MAX)
                continue;
            paired[first] = paired[second] = true;
            tl_cut_map_pair_list_push(&entry.pairs, (TlCutMapPair){piece_number(&pair->first), piece_number(&pair->second),
                                                                    round(strip_size(&pieces->items[first], &type->spec)),
                                                                    round(strip_size(&pieces->items[second], &type->spec)),
                                                                    pair->across_width});
        }
        for (size_t i = 0; i < pieces->count; i++) { /* остальные — по размеру куска, до мм */
            if (paired[i])
                continue;
            double width = round(pieces->items[i].width), height = round(pieces->items[i].height);
            TlCutStack *stack = NULL;
            for (size_t k = 0; k < entry.stacks.count && !stack; k++)
                if (entry.stacks.items[k].width == width && entry.stacks.items[k].height == height)
                    stack = &entry.stacks.items[k];
            if (!stack) {
                tl_cut_stack_list_push(&entry.stacks, (TlCutStack){width, height, {0}});
                stack = tl_cut_stack_list_last(&entry.stacks);
            }
            int number = piece_number(&pieces->items[i].ref);
            bool known = false;
            for (size_t n = 0; n < stack->numbers.count; n++)
                known |= stack->numbers.items[n] == number;
            if (!known)
                tl_int_array_push(&stack->numbers, number);
        }
        for (size_t k = 0; k < entry.stacks.count; k++)
            qsort(entry.stacks.items[k].numbers.items, entry.stacks.items[k].numbers.count, sizeof(int), compare_ints);
        if (entry.stacks.count > 1)
            qsort(entry.stacks.items, entry.stacks.count, sizeof(TlCutStack), compare_stacks);
        free(paired);
        tl_cut_map_type_list_push(&map, entry);
    }
    return map;
}

void tl_cut_map_free(TlCutMapTypeList *map)
{
    for (size_t t = 0; t < map->count; t++) {
        for (size_t k = 0; k < map->items[t].stacks.count; k++)
            tl_int_array_free(&map->items[t].stacks.items[k].numbers);
        tl_cut_stack_list_free(&map->items[t].stacks);
        tl_cut_map_pair_list_free(&map->items[t].pairs);
    }
    tl_cut_map_type_list_free(map);
}

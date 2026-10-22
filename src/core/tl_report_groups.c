#include "tl_report_groups.h"

#include <stdlib.h>

int tl_compare_cells(TlCell a, TlCell b)
{
    if (a.row != b.row)
        return a.row < b.row ? -1 : 1;
    return a.column < b.column ? -1 : a.column > b.column;
}

static int compare_owners(const void *left, const void *right)
{
    return tl_compare_cells(((const TlCellOwner *)left)->cell, ((const TlCellOwner *)right)->cell);
}

static bool space_is_tiled(TlSpace *space, TlLayout *layout)
{
    return tl_space_effective_tile(space)->enabled && !layout->too_many;
}

static TlLayoutGroup *group_for(TlLayoutGroupList *groups, TlSpace *source, bool owns_layouts)
{
    for (size_t i = 0; i < groups->count; i++)
        if (groups->items[i].source == source)
            return &groups->items[i];
    TlLayoutGroup group = {.source = source, .owns_layouts = owns_layouts};
    tl_layout_group_list_push(groups, group);
    return tl_layout_group_list_last(groups);
}

TlLayoutGroupList tl_report_build_groups(TlProject *project, const TlLayoutOptions *options,
                                         TlSpace *only_source)
{
    TlLayoutGroupList groups = {0};
    bool fresh = options != NULL;
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        if (only_source && space->layout_source != only_source)
            continue;
        if (!tl_space_effective_tile(space)->enabled)
            continue;
        TlLayout *layout = fresh ? tl_layout_compute(space, options) : tl_space_layout(space);
        if (!space_is_tiled(space, layout)) {
            if (fresh)
                tl_layout_free(layout);
            continue;
        }
        TlLayoutGroup *group = group_for(&groups, space->layout_source, fresh);
        tl_space_list_push(&group->members, space);
        tl_layout_ptr_list_push(&group->layouts, layout);
        for (size_t t = 0; t < layout->tiles.count; t++) {
            TlCellOwner owner = {layout->tiles.items[t].cell, space, &layout->tiles.items[t]};
            tl_cell_owner_list_push(&group->owners, owner);
        }
    }
    for (size_t g = 0; g < groups.count; g++)
        qsort(groups.items[g].owners.items, groups.items[g].owners.count, sizeof(TlCellOwner),
              compare_owners);
    return groups;
}

void tl_report_free_groups(TlLayoutGroupList *groups)
{
    for (size_t g = 0; g < groups->count; g++) {
        TlLayoutGroup *group = &groups->items[g];
        if (group->owns_layouts)
            for (size_t i = 0; i < group->layouts.count; i++)
                tl_layout_free(group->layouts.items[i]);
        tl_space_list_free(&group->members);
        tl_layout_ptr_list_free(&group->layouts);
        tl_cell_owner_list_free(&group->owners);
    }
    tl_layout_group_list_free(groups);
}

size_t tl_group_cell_owners(const TlLayoutGroup *group, TlCell cell, size_t *first)
{
    size_t low = 0, high = group->owners.count;
    while (low < high) { /* первый владелец с ячейкой >= cell */
        size_t middle = (low + high) / 2;
        if (tl_compare_cells(group->owners.items[middle].cell, cell) < 0)
            low = middle + 1;
        else
            high = middle;
    }
    size_t count = 0;
    while (low + count < group->owners.count &&
           tl_compare_cells(group->owners.items[low + count].cell, cell) == 0)
        count++;
    if (first)
        *first = low;
    return count;
}

size_t tl_group_unique_cells(const TlLayoutGroup *group)
{
    size_t unique = 0;
    for (size_t i = 0; i < group->owners.count; i++)
        if (i == 0 || tl_compare_cells(group->owners.items[i].cell, group->owners.items[i - 1].cell) != 0)
            unique++;
    return unique;
}

TlPieceList tl_group_unshared_pieces(const TlLayoutGroup *group)
{
    TlPieceList pieces = {0};
    for (size_t i = 0; i < group->owners.count; i++) {
        const TlCellOwner *owner = &group->owners.items[i];
        if (owner->tile->kind == TL_TILE_WHOLE || tl_group_cell_owners(group, owner->cell, NULL) > 1)
            continue;
        TlPiece piece = {{owner->space, owner->cell}, owner->tile->piece_width, owner->tile->piece_height};
        tl_piece_list_push(&pieces, piece);
    }
    return pieces;
}

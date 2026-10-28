#include "tl_report_groups.h"

#include <stdlib.h>

static int compare_owners(const void *left, const void *right)
{
    return tl_compare_cells(((const TlCellOwner *)left)->cell, ((const TlCellOwner *)right)->cell);
}

static TlLayoutGroup *group_for(TlLayoutGroupList *groups, TlSpace *source, size_t layout,
                                const TlTileSettings *tile, bool owns_layouts)
{
    for (size_t i = 0; i < groups->count; i++)
        if (groups->items[i].source == source && groups->items[i].layout == layout)
            return &groups->items[i];
    TlLayoutGroup group = {.source = source, .layout = layout, .tile = tile, .owns_layouts = owns_layouts};
    tl_layout_group_list_push(groups, group);
    return tl_layout_group_list_last(groups);
}

static void add_member(TlLayoutGroup *group, TlSpace *space, size_t index, TlLayout *layout)
{
    tl_space_list_push(&group->members, space);
    tl_layout_ptr_list_push(&group->layouts, layout);
    for (size_t t = 0; t < layout->tiles.count; t++) {
        TlCellOwner owner = {layout->tiles.items[t].cell, space, index, &layout->tiles.items[t]};
        tl_cell_owner_list_push(&group->owners, owner);
    }
}

TlLayoutGroupList tl_report_build_groups(TlProject *project, const TlLayoutOptions *options,
                                         TlSpace *only_source)
{
    TlLayoutGroupList groups = {0};
    bool fresh = options != NULL; /* перебор смещений — только основной пол */
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        if (only_source && space->layout_source != only_source)
            continue;
        bool floor = tl_space_effective_tile(space)->enabled; /* без плитки на полу — участки и стены */
        if (!floor && fresh)
            continue;
        TlLayout *layout = fresh ? tl_layout_compute(space, options) : tl_space_layout(space);
        if (!floor || layout->too_many) {
            if (fresh)
                tl_layout_free(layout);
        } else {
            TlLayoutGroup *group = group_for(&groups, space->layout_source, 0, tl_space_effective_tile(space), fresh);
            add_member(group, space, 0, layout);
        }
        for (size_t z = 1; !fresh && z < tl_space_layout_count(space); z++) {
            TlLayout *zone = tl_space_layout_at(space, z);
            if (tl_space_layout_counted(space, z))
                add_member(group_for(&groups, space, z, tl_space_layout_tile(space, z), false), space, z, zone);
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
        /* кусок с отверстием режут из своей плитки — так проще разметить */
        if (owner->tile->kind == TL_TILE_WHOLE || owner->tile->drills.count > 0 ||
            tl_group_cell_owners(group, owner->cell, NULL) > 1)
            continue;
        TlPiece piece = {{owner->space, owner->layout, owner->cell}, 0, 0, owner->tile->spec};
        tl_tile_piece_size(owner->tile, &piece.width, &piece.height);
        tl_piece_list_push(&pieces, piece);
    }
    return pieces;
}

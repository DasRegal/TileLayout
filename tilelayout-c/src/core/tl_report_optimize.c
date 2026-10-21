/* Подбор смещения сетки с минимумом плиток к покупке. */
#include "tl_report_groups.h"

#define COUNT_STEPS 24

bool tl_report_group_count(TlProject *project, TlSpace *space, const TlLayoutOptions *options,
                           TlTileCount *count)
{
    TlLayoutOptions fast = options ? *options : (TlLayoutOptions){0};
    fast.fast = true;
    TlLayoutGroupList groups = tl_report_build_groups(project, &fast, space->layout_source);
    bool ok = groups.count == 1;
    if (ok) {
        TlLayoutGroup *group = &groups.items[0];
        TlPieceList pieces = tl_group_unshared_pieces(group);
        TlOffcutPairList pairs = tl_report_offcut_pairs(&group->source->tile, &pieces);
        count->tiles = (int)tl_group_unique_cells(group) - (int)pairs.count;
        count->narrow = 0;
        for (size_t i = 0; i < group->layouts.count; i++)
            count->narrow += group->layouts.items[i]->narrow_count;
        tl_offcut_pair_list_free(&pairs);
        tl_piece_list_free(&pieces);
    }
    tl_report_free_groups(&groups);
    return ok;
}

static bool fewer(TlTileCount a, TlTileCount b)
{
    return a.tiles < b.tiles || (a.tiles == b.tiles && a.narrow < b.narrow);
}

bool tl_report_optimize_count(TlProject *project, TlSpace *space, TlTileCountResult *result)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    if (!tl_report_group_count(project, space, NULL, &result->before))
        return false;
    result->after = result->before;
    result->offset_x = tile->offset_x;
    result->offset_y = tile->offset_y;
    double pitch_x = tile->tile_width + tile->joint_width;
    double pitch_y = tile->tile_height + tile->joint_width;
    for (int a = 0; a < COUNT_STEPS; a++)
        for (int b = 0; b < COUNT_STEPS; b++) {
            TlLayoutOptions options = {true, true, a * pitch_x / COUNT_STEPS, b * pitch_y / COUNT_STEPS};
            TlTileCount count;
            if (tl_report_group_count(project, space, &options, &count) && fewer(count, result->after)) {
                result->after = count;
                result->offset_x = options.offset_x;
                result->offset_y = options.offset_y;
            }
        }
    return true;
}

/* Подбор смещения сетки с минимумом плиток к покупке. */
#include "tl_report_groups.h"

#define COUNT_STEPS 24

/* Пары обрезков группы — по каждому виду плитки рисунка отдельно. */
static int group_offcut_pairs(const TlLayoutGroup *group)
{
    const TlTileSettings *tile = group->tile;
    if (!tile->reuse_offcuts)
        return 0;
    const TlPatternUnit *pattern = &group->layouts.items[0]->pattern;
    TlPieceList pieces = tl_group_unshared_pieces(group);
    int pairs_count = 0;
    for (size_t s = 0; s < pattern->spec_count; s++) {
        if (!pattern->specs[s].rectangular)
            continue;
        TlPieceList of_spec = {0};
        for (size_t i = 0; i < pieces.count; i++)
            if (pieces.items[i].spec == (int)s)
                tl_piece_list_push(&of_spec, pieces.items[i]);
        TlOffcutPairList pairs = tl_report_offcut_pairs(pattern->specs[s].laid_width, pattern->specs[s].laid_height,
                                                        tile->cut_allowance, &of_spec);
        pairs_count += (int)pairs.count;
        tl_offcut_pair_list_free(&pairs);
        tl_piece_list_free(&of_spec);
    }
    tl_piece_list_free(&pieces);
    return pairs_count;
}

bool tl_report_group_count(TlProject *project, TlSpace *space, const TlLayoutOptions *options,
                           TlTileCount *count)
{
    TlLayoutOptions fast = options ? *options : (TlLayoutOptions){0};
    fast.fast = true;
    TlLayoutGroupList groups = tl_report_build_groups(project, &fast, space->layout_source);
    bool ok = groups.count == 1;
    if (ok) {
        TlLayoutGroup *group = &groups.items[0];
        count->tiles = (int)tl_group_unique_cells(group) - group_offcut_pairs(group);
        count->narrow = 0;
        for (size_t i = 0; i < group->layouts.count; i++)
            count->narrow += group->layouts.items[i]->narrow_count;
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
    TlPatternUnit unit;
    if (!tl_pattern_unit(tile, &unit))
        return true;
    for (int a = 0; a < COUNT_STEPS; a++)
        for (int b = 0; b < COUNT_STEPS; b++) {
            TlVec2 offset = tl_pattern_lattice_point(&unit, (double)a / COUNT_STEPS, (double)b / COUNT_STEPS);
            TlLayoutOptions options = {true, true, offset.x, offset.y};
            TlTileCount count;
            if (tl_report_group_count(project, space, &options, &count) && fewer(count, result->after)) {
                result->after = count;
                result->offset_x = options.offset_x;
                result->offset_y = options.offset_y;
            }
        }
    return true;
}

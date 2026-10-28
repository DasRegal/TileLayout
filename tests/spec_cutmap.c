#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_layout.h"
#include "tl_project.h"
#include "tl_report.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

void spec_cutmap(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Карта раскроя")
    {
        IT("одинаковые подрезки — стопкой, с номерами на плане; две узкие полосы — из одной плитки")
        {
            TlProject *project = room_project(2000, 1300);
            TlTileSettings *tile = &project->spaces.items[0]->tile;
            tile->tile_width = tile->tile_height = 300;
            tile->joint_width = 2;
            tile->row_shift = 0;
            tile->offset_x = tile->offset_y = 0;
            tile->wall_gap = 0;
            tile->pattern[0] = '\0';
            tile->reuse_offcuts = true;
            TlSummary summary = tl_report_summarize(project);
            TlCutMapTypeList map = tl_report_cut_map(&summary);
            EXPECT_INT(map.count, 1);
            const TlCutMapType *type = &map.items[0];
            /* 2000 = 6 × 302 + 188: полосы 188 справа; 1300 = 4 × 302 + 92: полосы 92 снизу. Плитка
               квадратная: полоса 188 и полоса 92 режутся из одной плитки (188 + 92 + рез < 300) */
            int mixed = 0, numbers = 0;
            for (size_t k = 0; k < type->pairs.count; k++) {
                const TlCutMapPair *pair = &type->pairs.items[k];
                EXPECT(pair->first > 0 && pair->second > 0 && pair->first != pair->second);
                mixed += fabs(pair->first_size + pair->second_size - 280) < 1;
            }
            EXPECT(mixed >= 4);
            for (size_t k = 0; k < type->stacks.count; k++)
                numbers += (int)type->stacks.items[k].numbers.count;
            EXPECT(numbers + 2 * (int)type->pairs.count >= 10); /* все подрезки — в карте */
            /* без использования остатков — только стопки: полосы 188 вместе */
            tl_cut_map_free(&map);
            tl_summary_free(&summary);
            tile->reuse_offcuts = false;
            summary = tl_report_summarize(project);
            map = tl_report_cut_map(&summary);
            EXPECT_INT(map.items[0].pairs.count, 0);
            EXPECT(fabs(map.items[0].stacks.items[0].width - 188) < 1 || fabs(map.items[0].stacks.items[0].height - 92) < 1);
            EXPECT(map.items[0].stacks.items[0].numbers.count >= 4);
            tl_cut_map_free(&map);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
    }
}

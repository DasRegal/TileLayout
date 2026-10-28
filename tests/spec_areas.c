#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_drag.h"
#include "tl_layout.h"
#include "tl_project.h"
#include "tl_project_io.h"
#include "tl_report.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

static TlSpace *only_space(TlProject *project) { return project->spaces.items[0]; }

static TlExpansionJoint *add_divider(TlProject *project, TlVec2 a, TlVec2 b)
{
    TlExpansionJoint *line = tl_expansion_joint_new(a, b, TL_DIVIDER_DEFAULT_GAP);
    line->kind = TL_LINE_DIVIDER;
    tl_project_add_expansion_joint(project, line);
    tl_project_update_spaces(project);
    return line;
}

static double floor_layouts_area(TlSpace *space)
{
    double area = 0;
    for (size_t i = 0; i < tl_space_floor_layout_count(space); i++)
        area += tl_space_layout_at(space, i)->floor_area;
    return area;
}

static void drag_to(TlProject *project, TlExpansionJoint *line, int part, TlVec2 from, TlVec2 to)
{
    TlDrag drag;
    TlDragTarget target = {.kind = TL_DRAG_JOINT, .joint = line, .index = part};
    if (!tl_drag_begin(&drag, project, target, from))
        return;
    TlSnapContext snap = {.project = project, .tolerance = 1, .skip_vertex = -1};
    tl_drag_move(&drag, project, to, &snap, true, NULL);
    tl_drag_end(&drag, project);
    tl_drag_free(&drag);
}

void spec_areas(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Линии раздела и участки пола")
    {
        IT("линия от стены до стены делит пол на две части; участок — своя плитка, основной пол его обходит")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            EXPECT_INT(space->regions.count, 1);
            add_divider(project, tl_vec2(1000, 0), tl_vec2(1000, 2000));
            EXPECT_INT(space->regions.count, 2);
            size_t left = tl_space_region_at(space, tl_vec2(500, 1000));
            EXPECT(left != SIZE_MAX && left != tl_space_region_at(space, tl_vec2(2000, 1000)));
            double main_before = tl_space_layout(space)->floor_area;
            size_t area = tl_space_add_area(space, left, true);
            space->areas.items[area].zone.tile.tile_width = space->areas.items[area].zone.tile.tile_height = 50;
            EXPECT(tl_space_area_of_region(space, left) == area);
            EXPECT(tl_space_layout(space)->floor_area < main_before - 1.5e6); /* участок ~1×2 м — не основной пол */
            size_t found = SIZE_MAX;
            for (size_t i = 0; i < tl_space_floor_layout_count(space); i++)
                if (tl_space_layout_area(space, i) == area)
                    found = i;
            EXPECT(found != SIZE_MAX);
            EXPECT(fabs(tl_space_layout_tile(space, found)->tile_width - 50) < 1e-9);
            /* весь пол — основной и участок (полосы швов в площадь под плитку входят, как у деформационных) */
            EXPECT(fabs(floor_layouts_area(space) - 6e6) < 3000);
            tl_project_free(project);
        }
        IT("участок без плитки: там ничего не кладётся")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            add_divider(project, tl_vec2(2000, 0), tl_vec2(2000, 2000));
            tl_space_add_area(space, tl_space_region_at(space, tl_vec2(2500, 1000)), false);
            EXPECT(fabs(floor_layouts_area(space) - 2000 * 2000) < 3000);
            TlSummary summary = tl_report_summarize(project);
            EXPECT(fabs(summary.tiled_area / 1e6 - 4) < 0.01);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("замкнутый контур из линий — участок внутри комнаты; линии сочленены")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            TlExpansionJoint *top = add_divider(project, tl_vec2(1000, 500), tl_vec2(2000, 500));
            add_divider(project, tl_vec2(2000, 500), tl_vec2(2000, 1500));
            add_divider(project, tl_vec2(2000, 1500), tl_vec2(1000, 1500));
            TlExpansionJoint *left = add_divider(project, tl_vec2(1000, 1500), tl_vec2(1000, 500));
            EXPECT_INT(space->regions.count, 2);
            size_t inner = tl_space_region_at(space, tl_vec2(1500, 1000));
            tl_space_add_area(space, inner, false);
            EXPECT(fabs(floor_layouts_area(space) - (6e6 - 1e6)) < 20000);
            /* тянем верхнюю линию вверх целиком: концы левой и правой — за ней, участок — тоже */
            drag_to(project, top, 0, tl_vec2(1500, 500), tl_vec2(1500, 300));
            EXPECT(fabs(left->end.y - 300) < 1e-9);
            EXPECT(fabs(project->expansion_joints.items[1]->start.y - 300) < 1e-9);
            EXPECT_INT(space->regions.count, 2);
            EXPECT(fabs(floor_layouts_area(space) - (6e6 - 1.2e6)) < 20000);
            /* тянем конец линии — сочленённый конец соседней следует за ним */
            drag_to(project, top, 2, tl_vec2(2000, 300), tl_vec2(2200, 300));
            EXPECT(fabs(project->expansion_joints.items[1]->start.x - 2200) < 1e-9);
            tl_project_free(project);
        }
        IT("внешняя часть со своей плиткой, внутренняя — как в комнате: обе застелены, ничего не пропало")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            add_divider(project, tl_vec2(1000, 500), tl_vec2(2000, 500));
            add_divider(project, tl_vec2(2000, 500), tl_vec2(2000, 1500));
            add_divider(project, tl_vec2(2000, 1500), tl_vec2(1000, 1500));
            add_divider(project, tl_vec2(1000, 1500), tl_vec2(1000, 500));
            size_t outer = tl_space_region_at(space, tl_vec2(300, 300));
            EXPECT_INT(space->regions.items[outer].holes.count, 1);
            size_t area = tl_space_add_area(space, outer, true);
            space->areas.items[area].zone.tile.tile_width = space->areas.items[area].zone.tile.tile_height = 100;
            /* основной пол — ровно внутренняя часть (~1 м²), участок — всё вокруг (~5 м²) */
            EXPECT(fabs(tl_space_layout(space)->floor_area - 1e6) < 20000);
            EXPECT(fabs(floor_layouts_area(space) - 6e6) < 30000);
            bool inner_tiled = false;
            TlLayout *main = tl_space_layout(space);
            for (size_t t = 0; t < main->tiles.count; t++) {
                TlVec2 c = tl_affine_apply(&main->local_to_world, main->tiles.items[t].center);
                inner_tiled |= c.x > 1000 && c.x < 2000 && c.y > 500 && c.y < 1500;
            }
            EXPECT(inner_tiled);
            tl_project_free(project);
        }
        IT("вид линии и участки сохраняются; после загрузки участок снова на своей части")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            add_divider(project, tl_vec2(1000, 0), tl_vec2(1000, 2000));
            size_t area = tl_space_add_area(space, tl_space_region_at(space, tl_vec2(500, 1000)), true);
            snprintf(space->areas.items[area].name, sizeof space->areas.items[area].name, "%s", "Душевая");
            space->areas.items[area].zone.tile.tile_width = 23;
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            TlSpace *copy = only_space(loaded);
            EXPECT_INT(loaded->expansion_joints.items[0]->kind, TL_LINE_DIVIDER);
            EXPECT(fabs(loaded->expansion_joints.items[0]->gap - TL_DIVIDER_DEFAULT_GAP) < 1e-9);
            EXPECT_INT(copy->areas.count, 1);
            EXPECT_STR(copy->areas.items[0].name, "Душевая");
            EXPECT(fabs(copy->areas.items[0].zone.tile.tile_width - 23) < 1e-9);
            EXPECT(copy->areas.items[0].region.outline.count >= 3);
            EXPECT(tl_space_area_of_region(copy, tl_space_region_at(copy, tl_vec2(500, 1000))) == 0);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

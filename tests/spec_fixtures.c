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

/* Ванна в углу (0, 0): спинкой к верхней стене, торцом — к левой. */
static TlFixture corner_bath(void)
{
    TlFixture bath = tl_fixture_default(TL_FIXTURE_BATH, tl_vec2(850, 375));
    return bath;
}

void spec_fixtures(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Предметы в комнате: ванна, поддон, инсталляция, тумба, кухня")
    {
        IT("ванна 1700×750: без плитки под ней пол её обходит; с плиткой — пол целиком")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            double before = tl_space_layout(space)->floor_area;
            tl_fixture_list_push(&project->fixtures, corner_bath());
            tl_project_update_spaces(project);
            EXPECT_INT(space->fixture_floor.count, 1);
            EXPECT(fabs(before - tl_space_layout(space)->floor_area - 1700 * 750) < 1);
            project->fixtures.items[0].under = true;
            tl_project_update_spaces(project);
            EXPECT_INT(space->fixture_floor.count, 0);
            EXPECT(fabs(tl_space_layout(space)->floor_area - before) < 1);
            tl_project_free(project);
        }
        IT("ванна у стен закрывает их до своей высоты: на развёртке там нет облицовки")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            double before = tl_space_walls_area(space);
            tl_fixture_list_push(&project->fixtures, corner_bath());
            tl_project_update_spaces(project);
            int covers = 0;
            for (size_t i = 0; i < tl_space_wall_hole_count(space); i++)
                covers += tl_space_wall_hole_at(space, i)->fixture == 0;
            EXPECT_INT(covers, 2); /* спинка — к верхней стене, торец — к левой */
            EXPECT(fabs(before - tl_space_walls_area(space) - (1700 + 750) * 600) < 1000);
            project->fixtures.items[0].behind = true;
            tl_project_update_spaces(project);
            EXPECT(fabs(tl_space_walls_area(space) - before) < 1);
            tl_project_free(project);
        }
        IT("поставить к стене: спинка вплотную, поворот — по стене")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            TlFixture cabinet = tl_fixture_default(TL_FIXTURE_CABINET, tl_vec2(2900, 1000)); /* у правой стены */
            TlPolygon walls = tl_space_wall_outline(space);
            EXPECT(tl_fixture_snap_to_walls(&cabinet, &walls, 200));
            tl_polygon_free(&walls);
            EXPECT(fabs(cabinet.center.x - (3000 - 225)) < 1e-6);
            TlPolygon outline = tl_fixture_outline(&cabinet);
            for (size_t i = 0; i < 2; i++) /* спинка — на стене x = 3000 */
                EXPECT(fabs(outline.items[i].x - 3000) < 1e-6);
            tl_polygon_free(&outline);
            tl_project_free(project);
        }
        IT("предмет тянут: у стены встаёт к ней спинкой, пол под ним пересчитан")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            tl_fixture_list_push(&project->fixtures, tl_fixture_default(TL_FIXTURE_SHOWER_TRAY, tl_vec2(1500, 1000)));
            tl_project_update_spaces(project);
            TlDrag drag;
            EXPECT(tl_drag_begin(&drag, project, (TlDragTarget){.kind = TL_DRAG_FIXTURE, .index = 0}, tl_vec2(1500, 1000)));
            TlSnapContext snap = {.project = project, .tolerance = 20, .grid_step = 10, .skip_vertex = -1};
            tl_drag_move(&drag, project, tl_vec2(1500, 1520), &snap, false, NULL); /* к нижней стене */
            tl_drag_end(&drag, project);
            tl_drag_free(&drag);
            const TlFixture *tray = &project->fixtures.items[0];
            EXPECT(fabs(tray->center.y - (2000 - 450)) < 1e-6);
            TlRect bounds = tl_polygon_bounds(&space->fixture_floor.items[0]);
            EXPECT(fabs(bounds.bottom - 2000) < 1e-6);
            tl_project_free(project);
        }
        IT("лестница: ступени — на развёртке комнаты, подступенок и проступь по очереди, как пол")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = only_space(project);
            TlFixture stairs = tl_fixture_default(TL_FIXTURE_STAIRS, tl_vec2(2000, 1500));
            EXPECT_INT(stairs.steps, 3);
            tl_fixture_list_push(&project->fixtures, stairs);
            tl_project_update_spaces(project);
            EXPECT(!space->walls.enabled); /* ступени — и без облицовки стен */
            EXPECT_INT(tl_space_stairs_count(space), 1);
            size_t floors = tl_space_floor_layout_count(space);
            double area = 0;
            int parts = 0, mitered_treads = 0;
            for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
                int step;
                bool tread;
                if (tl_space_wall_layout_stairs(space, i, &step, &tread) != 0)
                    continue;
                EXPECT(step == parts / 2 && tread == (parts % 2 == 1)); /* снизу вверх: подступенок, проступь… */
                EXPECT(!tl_space_layout_on_wall(space, floors + i));
                const TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, NULL);
                area += layout->floor_area;
                for (size_t t = 0; tread && t < layout->tiles.count; t++)
                    if (layout->tiles.items[t].miters.count > 0) {
                        mitered_treads++;
                        break;
                    }
                parts++;
            }
            EXPECT_INT(parts, 6);
            EXPECT_INT(mitered_treads, 3); /* на ребре каждой ступени — запил */
            EXPECT(fabs(area - 900 * (510 + 840)) < 900 * 1350 * 0.02);
            EXPECT(fabs(tl_space_walls_area(space)) < 1e-9); /* в облицовку стен не входят */
            TlRect column, bounds = tl_space_walls_bounds(space);
            EXPECT(tl_space_stairs_column(space, 0, &column));
            EXPECT(fabs(tl_rect_height(column) - 1350) < 1e-9);
            EXPECT(bounds.top <= column.top && bounds.right >= column.right);
            project->fixtures.items[0].steps = 5;
            tl_project_update_spaces(project);
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->fixtures.items[0].steps, 5);
            EXPECT_INT(tl_space_wall_layout_count(loaded->spaces.items[0]), 10);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("предметы сохраняются в файле проекта")
        {
            TlProject *project = room_project(3000, 2000);
            TlFixture kitchen = tl_fixture_default(TL_FIXTURE_KITCHEN, tl_vec2(1500, 300));
            kitchen.angle = 90;
            kitchen.behind = true;
            tl_fixture_list_push(&project->fixtures, kitchen);
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->fixtures.count, 1);
            EXPECT_INT(loaded->fixtures.items[0].kind, TL_FIXTURE_KITCHEN);
            EXPECT(fabs(loaded->fixtures.items[0].angle - 90) < 1e-9);
            EXPECT(loaded->fixtures.items[0].behind && !loaded->fixtures.items[0].under);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

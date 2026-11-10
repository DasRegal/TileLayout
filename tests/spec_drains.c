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

/* Есть ли треугольный кусок плитки: подрезка, у которой контур — не прямоугольник. */
static int figured_pieces(TlSpace *space)
{
    int count = 0;
    TlLayout *layout = tl_space_layout(space);
    for (size_t t = 0; t < layout->tiles.count; t++)
        count += layout->tiles.items[t].figured;
    return count;
}

void spec_drains(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Трап и уклоны")
    {
        IT("точечный трап: конверт — разрезы от трапа к четырём углам, плитка режется по диагоналям")
        {
            TlProject *project = room_project(1200, 1200);
            TlSpace *space = only_space(project);
            double before = tl_space_layout(space)->floor_area;
            int figured_before = figured_pieces(space);
            tl_drain_list_push(&project->drains, tl_drain_default(TL_DRAIN_POINT, tl_vec2(600, 600)));
            tl_project_update_spaces(project);
            EXPECT_INT(space->slope_lines.count, 4);
            EXPECT_INT(space->regions.count, 1); /* конверт не делит пол на участки */
            EXPECT(figured_pieces(space) > figured_before);
            EXPECT(fabs(before - tl_space_layout(space)->floor_area - 150 * 150) < 1); /* решётка — без плитки */
            const TlDrain *drain = &project->drains.items[0];
            EXPECT(fabs(tl_drain_drop(drain, &space->outline) - hypot(600, 600) * 0.015) < 1e-6);
            project->drains.items[0].envelope = false;
            tl_project_update_spaces(project);
            EXPECT_INT(space->slope_lines.count, 0);
            tl_project_free(project);
        }
        IT("линейный трап у стены: лоток вдоль неё, без разрезов; перепад — поперёк")
        {
            TlProject *project = room_project(2000, 1200);
            TlSpace *space = only_space(project);
            TlDrain drain = tl_drain_default(TL_DRAIN_LINEAR, tl_vec2(1000, 1150));
            drain.length = 1800;
            TlPolygon walls = tl_space_wall_outline(space);
            EXPECT(tl_drain_snap_to_walls(&drain, &walls, 200));
            tl_polygon_free(&walls);
            EXPECT(fabs(drain.center.y - (1200 - 35)) < 1e-6);
            tl_drain_list_push(&project->drains, drain);
            tl_project_update_spaces(project);
            EXPECT_INT(space->slope_lines.count, 0);
            EXPECT(fabs(tl_drain_drop(&project->drains.items[0], &space->outline) - (1200 - 35) * 0.015) < 1e-6);
            tl_project_free(project);
        }
        IT("трап тянут: конверт — от нового места; трапы сохраняются в файле")
        {
            TlProject *project = room_project(1200, 1200);
            tl_drain_list_push(&project->drains, tl_drain_default(TL_DRAIN_POINT, tl_vec2(600, 600)));
            tl_project_update_spaces(project);
            TlDrag drag;
            EXPECT(tl_drag_begin(&drag, project, (TlDragTarget){.kind = TL_DRAG_DRAIN, .index = 0}, tl_vec2(600, 600)));
            TlSnapContext snap = {.project = project, .tolerance = 20, .grid_step = 10, .skip_vertex = -1};
            tl_drag_move(&drag, project, tl_vec2(803, 497), &snap, false, NULL);
            tl_drag_end(&drag, project);
            tl_drag_free(&drag);
            EXPECT(fabs(project->drains.items[0].center.x - 800) < 1e-9 && fabs(project->drains.items[0].center.y - 500) < 1e-9);
            const TlPolygon *line = &only_space(project)->slope_lines.items[0];
            EXPECT(tl_distance(line->items[0], tl_vec2(800, 500)) < 1e-9);
            project->drains.items[0].slope = 2;
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->drains.count, 1);
            EXPECT(fabs(loaded->drains.items[0].slope - 2) < 1e-9);
            EXPECT_INT(only_space(loaded)->slope_lines.count, 4);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

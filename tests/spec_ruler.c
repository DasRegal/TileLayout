#include <stdio.h>
#include <stdlib.h>

#include "spec.h"
#include "specs.h"

#include "tl_drag.h"
#include "tl_measure_anchor.h"
#include "tl_measure_snap.h"
#include "tl_project.h"
#include "tl_project_io.h"
#include "tl_ruler.h"

#define SNAP_RADIUS 100.0

/* Дом 4000×3000 по внутренней стороне, стены 200 мм; плитка 600×600, шов 2,
   без сдвига рядов — швы по осям проходят через 601 + k·602. */
static TlProject *house_with_tiles(bool tiles)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    TlTileSettings *tile = &project->spaces.items[0]->tile;
    *tile = tl_tile_settings_factory();
    tile->enabled = tiles;
    tile->tile_width = tile->tile_height = 600;
    tile->joint_width = 2;
    tile->row_shift = 0;
    tile->offset_x = tile->offset_y = 0;
    tile->rotation_degrees = 0;
    tile->wall_gap = 0;
    return project;
}

static TlSnapPoint snap(TlProject *project, double x, double y)
{
    TlSnapPoint result = {{0, 0}, TL_SNAP_NONE, {0, 0}};
    tl_measure_snap(project, tl_vec2(x, y), SNAP_RADIUS, TL_SNAP_TO_ALL, &result);
    return result;
}

void spec_ruler(void)
{
    DESCRIBE("Линейка")
    {
        IT("показывает расстояние между концами")
        {
            TlRuler *ruler = tl_ruler_new(tl_vec2(1000, 1000), tl_vec2(4000, 5000));
            EXPECT_NEAR(tl_ruler_length(ruler), 5000, 1e-9);
            tl_ruler_free(ruler);
        }
        IT("двигается целиком и за каждый конец отдельно")
        {
            TlRuler *ruler = tl_ruler_new(tl_vec2(0, 0), tl_vec2(1000, 0));
            tl_ruler_translate(ruler, tl_vec2(10, 20));
            EXPECT_NEAR(ruler->start.x, 10, 1e-9);
            EXPECT_NEAR(ruler->end.y, 20, 1e-9);
            *tl_ruler_end_point(ruler, TL_RULER_END) = tl_vec2(10, 520);
            EXPECT_NEAR(tl_ruler_length(ruler), 500, 1e-9);
            tl_ruler_free(ruler);
        }
        IT("добавляется в любом количестве и удаляется по одной или все сразу")
        {
            TlProject *project = tl_project_new();
            TlRuler *first = tl_ruler_new(tl_vec2(0, 0), tl_vec2(100, 0));
            tl_project_add_ruler(project, first);
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(0, 0), tl_vec2(0, 100)));
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(5, 5), tl_vec2(50, 50)));
            EXPECT_INT(project->rulers.count, 3);
            tl_project_delete_ruler(project, first);
            EXPECT_INT(project->rulers.count, 2);
            tl_project_delete_all_rulers(project);
            EXPECT_INT(project->rulers.count, 0);
            tl_project_free(project);
        }
        IT("сохраняется в файле проекта и загружается обратно")
        {
            TlProject *project = house_with_tiles(true);
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(0, 0), tl_vec2(4000, 0)));
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(-200, 100), tl_vec2(-200, 2900.5)));
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->rulers.count, 2);
            EXPECT_NEAR(tl_ruler_length(loaded->rulers.items[0]), 4000, 1e-9);
            EXPECT_NEAR(loaded->rulers.items[1]->end.y, 2900.5, 1e-9);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }

    DESCRIBE("Примагничивание концов линейки")
    {
        IT("к углу стены — внутреннему и наружному")
        {
            TlProject *project = house_with_tiles(false);
            TlSnapPoint inner = snap(project, 30, 20);
            EXPECT_INT(inner.kind, TL_SNAP_CORNER);
            EXPECT_NEAR(inner.point.x, 0, 1e-6);
            EXPECT_NEAR(inner.point.y, 0, 1e-6);
            TlSnapPoint outer = snap(project, -170, -230);
            EXPECT_INT(outer.kind, TL_SNAP_CORNER);
            EXPECT_NEAR(outer.point.x, -200, 1e-6);
            EXPECT_NEAR(outer.point.y, -200, 1e-6);
            tl_project_free(project);
        }
        IT("к середине стены")
        {
            TlProject *project = house_with_tiles(false);
            TlSnapPoint middle = snap(project, 1960, 40);
            EXPECT_INT(middle.kind, TL_SNAP_WALL_MIDDLE);
            EXPECT_NEAR(middle.point.x, 2000, 1e-6);
            EXPECT_NEAR(middle.point.y, 0, 1e-6);
            tl_project_free(project);
        }
        IT("к краю стены — в ближайшую точку грани")
        {
            TlProject *project = house_with_tiles(false);
            TlSnapPoint edge = snap(project, 1000, 30);
            EXPECT_INT(edge.kind, TL_SNAP_WALL_EDGE);
            EXPECT_NEAR(edge.point.x, 1000, 1e-6);
            EXPECT_NEAR(edge.point.y, 0, 1e-6);
            tl_project_free(project);
        }
        IT("к краю и углу перегородки")
        {
            TlProject *project = house_with_tiles(false);
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            TlSnapPoint side = snap(project, 1930, 1210);
            EXPECT_INT(side.kind, TL_SNAP_WALL_EDGE);
            EXPECT_NEAR(side.point.x, 1950, 1e-6);
            EXPECT_NEAR(side.point.y, 1210, 1e-6);
            TlSnapPoint corner = snap(project, 2070, 2960);
            EXPECT_INT(corner.kind, TL_SNAP_CORNER);
            EXPECT_NEAR(corner.point.x, 2050, 1e-6);
            EXPECT_NEAR(corner.point.y, 3000, 1e-6);
            tl_project_free(project);
        }
        IT("к середине шва между плитками")
        {
            TlProject *project = house_with_tiles(true);
            TlSnapPoint joint = snap(project, 640, 900);
            EXPECT_INT(joint.kind, TL_SNAP_TILE_JOINT);
            EXPECT_NEAR(joint.point.x, 601, 1e-6);
            EXPECT_NEAR(joint.point.y, 900, 1e-6);
            tl_project_free(project);
        }
        IT("к пересечению швов — сильнее, чем к шву")
        {
            TlProject *project = house_with_tiles(true);
            TlSnapPoint crossing = snap(project, 620, 1190);
            EXPECT_INT(crossing.kind, TL_SNAP_JOINT_CROSSING);
            EXPECT_NEAR(crossing.point.x, 601, 1e-6);
            EXPECT_NEAR(crossing.point.y, 1203, 1e-6);
            tl_project_free(project);
        }
        IT("угол сильнее края стены, даже если край ближе")
        {
            TlProject *project = house_with_tiles(false);
            TlSnapPoint result = snap(project, 60, 5);
            EXPECT_INT(result.kind, TL_SNAP_CORNER);
            tl_project_free(project);
        }
        IT("без плитки не притягивает к швам, а дальше радиуса — никуда")
        {
            TlProject *project = house_with_tiles(false);
            EXPECT_INT(snap(project, 640, 900).kind, TL_SNAP_NONE);
            TlSnapPoint none;
            EXPECT(!tl_measure_snap(project, tl_vec2(1500, 1500), SNAP_RADIUS, TL_SNAP_TO_ALL, &none));
            tl_project_free(project);
        }
        IT("линейка и проём получают имена «Л1», «Д1»…; имена сохраняются в файле")
        {
            TlProject *project = tl_project_demo();
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(0, 0), tl_vec2(1000, 0)));
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(0, 100), tl_vec2(1000, 100)));
            EXPECT_STR(project->rulers.items[1]->name, "Л2");
            EXPECT_STR(project->openings.items[0]->name, "Д1");
            snprintf(project->rulers.items[0]->name, TL_NAME_LENGTH, "%s", "Ось унитаза");
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[64];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_STR(loaded->rulers.items[0]->name, "Ось унитаза");
            EXPECT_STR(loaded->rulers.items[1]->name, "Л2");
            tl_project_add_ruler(loaded, tl_ruler_new(tl_vec2(0, 200), tl_vec2(1000, 200)));
            EXPECT_STR(loaded->rulers.items[2]->name, "Л3");
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("конец линейки на стене едет со стеной, длина — настоящая; свободный конец стоит")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            TlRuler *ruler = tl_ruler_new(tl_vec2(0, 1500), tl_vec2(1950, 1500)); /* до левого бока перегородки */
            tl_project_add_ruler(project, ruler);
            TlRuler *loose = tl_ruler_new(tl_vec2(500, 500), tl_vec2(900, 500));
            tl_project_add_ruler(project, loose);
            TlDrag drag = {0};
            TlDragTarget target = {.kind = TL_DRAG_PARTITION, .partition = project->partitions.items[0]};
            EXPECT(tl_drag_begin(&drag, project, target, tl_vec2(2000, 1500)));
            TlSnapContext snap = {.project = project, .tolerance = 1, .skip_vertex = -1};
            tl_drag_move(&drag, project, tl_vec2(2300, 1500), &snap, true, NULL);
            tl_drag_end(&drag, project);
            EXPECT_NEAR(ruler->end.x, 2250, 0.01);
            EXPECT_NEAR(tl_ruler_length(ruler), 2250, 0.01);
            EXPECT_NEAR(ruler->start.x, 0, 1e-9);
            EXPECT_NEAR(loose->start.x, 500, 1e-9);

            /* стену дома — тоже: правый конец на внутренней грани правой стены */
            TlRuler *across = tl_ruler_new(tl_vec2(2350, 1000), tl_vec2(4000, 1000));
            tl_project_add_ruler(project, across);
            TlDrag wall = {0};
            TlDragTarget wall_target = {.kind = TL_DRAG_WALL, .outline = project->outlines.items[0], .index = 1};
            EXPECT(tl_drag_begin(&wall, project, wall_target, tl_vec2(4100, 1500)));
            tl_drag_move(&wall, project, tl_vec2(4600, 1500), &snap, true, NULL);
            tl_drag_end(&wall, project);
            EXPECT_NEAR(across->end.x, 4500, 0.01);
            EXPECT_NEAR(across->start.x, 2350, 0.01);
            tl_project_free(project);
        }
        IT("притягивается к трубе (центр и края по осям) и к углам и сторонам предмета")
        {
            TlProject *project = house_with_tiles(false);
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(1000, 1000), 110));
            tl_fixture_list_push(&project->fixtures, tl_fixture_default(TL_FIXTURE_CABINET, tl_vec2(3000, 2000)));
            TlSnapPoint center = snap(project, 1010, 1005);
            EXPECT_INT(center.kind, TL_SNAP_CORNER);
            EXPECT_NEAR(center.point.x, 1000, 1e-9);
            TlSnapPoint top = snap(project, 1003, 948);
            EXPECT_NEAR(top.point.y, 945, 1e-9);
            TlSnapPoint right = snap(project, 1050, 1002);
            EXPECT_NEAR(right.point.x, 1055, 1e-9);
            TlPolygon outline = tl_fixture_outline(&project->fixtures.items[0]);
            TlVec2 corner = outline.items[2];
            TlSnapPoint at_corner = snap(project, corner.x + 20, corner.y - 15);
            EXPECT_INT(at_corner.kind, TL_SNAP_CORNER);
            EXPECT_NEAR(at_corner.point.x, corner.x, 1e-9);
            EXPECT_NEAR(at_corner.point.y, corner.y, 1e-9);
            tl_polygon_free(&outline);
            tl_project_free(project);
        }
        IT("длина числом сдвигает трубу или предмет на конце, как мышкой; конец на стене — только линейку")
        {
            TlProject *project = house_with_tiles(false);
            TlPipe *pipe = tl_pipe_new(tl_vec2(1000, 1000), 110);
            tl_project_add_pipe(project, pipe);
            TlRuler *to_pipe = tl_ruler_new(tl_vec2(0, 1000), tl_vec2(945, 1000)); /* от стены до левого края трубы */
            tl_project_add_ruler(project, to_pipe);
            EXPECT(tl_measure_set_ruler_length(project, to_pipe, 1200));
            EXPECT_NEAR(pipe->center.x, 1255, 1e-6);
            EXPECT_NEAR(to_pipe->end.x, 1200, 1e-6);
            EXPECT_NEAR(to_pipe->start.x, 0, 1e-9);

            /* труба — в начале линейки, конец на стене: двигается начало и труба */
            TlRuler *from_pipe = tl_ruler_new(tl_vec2(1255, 1055), tl_vec2(1255, 3000));
            tl_project_add_ruler(project, from_pipe);
            EXPECT(tl_measure_set_ruler_length(project, from_pipe, 1500));
            EXPECT_NEAR(pipe->center.y, 1445, 1e-6);
            EXPECT_NEAR(from_pipe->end.y, 3000, 1e-9);
            EXPECT_NEAR(tl_ruler_length(from_pipe), 1500, 1e-6);
            EXPECT_NEAR(to_pipe->end.x, 1200, 1e-6); /* конец первой линейки — с трубой, на её левом краю */
            EXPECT_NEAR(to_pipe->end.y, 1445, 1e-6);

            tl_fixture_list_push(&project->fixtures, tl_fixture_default(TL_FIXTURE_CABINET, tl_vec2(3000, 2000)));
            TlPolygon outline = tl_fixture_outline(&project->fixtures.items[0]);
            TlVec2 corner = outline.items[0];
            tl_polygon_free(&outline);
            TlRuler *to_cabinet = tl_ruler_new(tl_vec2(4000, corner.y), corner);
            tl_project_add_ruler(project, to_cabinet);
            double before = project->fixtures.items[0].center.x;
            EXPECT(tl_measure_set_ruler_length(project, to_cabinet, tl_ruler_length(to_cabinet) + 100));
            EXPECT_NEAR(project->fixtures.items[0].center.x, before - 100, 1e-6);

            TlRuler *walls = tl_ruler_new(tl_vec2(0, 500), tl_vec2(4000, 500));
            tl_project_add_ruler(project, walls);
            EXPECT(tl_measure_set_ruler_length(project, walls, 3000));
            EXPECT_NEAR(walls->end.x, 3000, 1e-9);
            tl_project_free(project);
        }
        IT("притягивается к стыку полов в проёме; длина до угла проёма числом — сдвигает проём вдоль стены")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            TlOpening *door = tl_opening_new(tl_vec2(2000, 1500), 800);
            tl_opening_set_threshold(door, 0.5);
            tl_project_add_opening(project, door);
            tl_project_update_spaces(project);
            TlSnapPoint threshold = snap(project, 2003, 1530);
            EXPECT_NEAR(threshold.point.x, 2000, 1e-6);
            EXPECT_NEAR(threshold.point.y, 1500, 1e-6);
            TlRuler *to_door = tl_ruler_new(tl_vec2(1950, 0), tl_vec2(1950, 1100)); /* от стены до угла проёма */
            tl_project_add_ruler(project, to_door);
            EXPECT(tl_measure_set_ruler_length(project, to_door, 1300));
            EXPECT_NEAR(door->center.y, 1700, 1e-6);
            EXPECT_NEAR(to_door->end.y, 1300, 1e-6);
            tl_project_free(project);
        }
        IT("между перегородкой и трубой длина числом двигает трубу, а перегородка стоит")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            TlPipe *pipe = tl_pipe_new(tl_vec2(1000, 1500), 110);
            tl_project_add_pipe(project, pipe);
            TlRuler *ruler = tl_ruler_new(tl_vec2(1950, 1500), tl_vec2(1055, 1500)); /* от бока перегородки до трубы */
            tl_project_add_ruler(project, ruler);
            EXPECT(tl_measure_set_ruler_length(project, ruler, 1200));
            EXPECT_NEAR(project->partitions.items[0]->start.x, 2000, 1e-9);
            EXPECT_NEAR(pipe->center.x, 695, 1e-6);
            EXPECT_NEAR(ruler->start.x, 1950, 1e-9);
            EXPECT_NEAR(tl_ruler_length(ruler), 1200, 1e-6);

            /* обратная линейка — начало на трубе, конец на перегородке: тоже двигается труба */
            TlRuler *back = tl_ruler_new(tl_vec2(pipe->center.x + 55, 1500), tl_vec2(1950, 1500));
            tl_project_add_ruler(project, back);
            EXPECT(tl_measure_set_ruler_length(project, back, 1000));
            EXPECT_NEAR(project->partitions.items[0]->start.x, 2000, 1e-9);
            EXPECT_NEAR(pipe->center.x, 895, 1e-6);
            tl_project_free(project);
        }
        IT("трубу тянут мышкой — линейка от перегородки к ней не отцепляется и стоит под прямым углом")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            TlPipe *pipe = tl_pipe_new(tl_vec2(1000, 1500), 110);
            tl_project_add_pipe(project, pipe);
            TlRuler *ruler = tl_ruler_new(tl_vec2(1950, 1500), tl_vec2(1055, 1500));
            tl_project_add_ruler(project, ruler);
            TlDrag drag = {0};
            TlDragTarget target = {.kind = TL_DRAG_PIPE, .pipe = pipe};
            EXPECT(tl_drag_begin(&drag, project, target, tl_vec2(1000, 1500)));
            TlSnapContext snap = {.project = project, .tolerance = 1, .skip_vertex = -1};
            tl_drag_move(&drag, project, tl_vec2(1200, 1900), &snap, true, NULL);
            tl_drag_end(&drag, project);
            EXPECT_NEAR(ruler->end.x, 1255, 1e-6);
            EXPECT_NEAR(ruler->end.y, 1900, 1e-6);
            EXPECT_NEAR(ruler->start.x, 1950, 1e-6); /* по боку перегородки — вслед за трубой */
            EXPECT_NEAR(ruler->start.y, 1900, 1e-6);
            tl_project_free(project);
        }
        IT("тумба у стены: линейка к середине её стороны держится за тумбу, не за стену")
        {
            TlProject *project = house_with_tiles(false);
            TlFixture cabinet = tl_fixture_default(TL_FIXTURE_CABINET, tl_vec2(2000, 0));
            cabinet.center.y = cabinet.depth / 2; /* спинкой к верхней стене */
            tl_fixture_list_push(&project->fixtures, cabinet);
            double left = 2000 - cabinet.width / 2;
            TlRuler *ruler = tl_ruler_new(tl_vec2(0, cabinet.depth / 2), tl_vec2(left, cabinet.depth / 2));
            tl_project_add_ruler(project, ruler);
            EXPECT(tl_measure_set_ruler_length(project, ruler, left + 300));
            EXPECT_NEAR(project->fixtures.items[0].center.x, 2300, 1e-6);
            EXPECT_NEAR(ruler->start.x, 0, 1e-9);
            tl_project_free(project);
        }
    }
}

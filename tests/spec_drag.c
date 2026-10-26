#include "spec.h"
#include "specs.h"

#include <math.h>

#include "tl_drag.h"

/* Дом 6000×4000 (стены 200) и вертикальная перегородка по x = 3000. */
static TlProject *house_with_partition(void)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
    tl_project_add_contour(project, corners, 4, 200, 100);
    tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 4000), 100);
    return project;
}

static TlSnapContext snap_of(TlProject *project)
{
    return (TlSnapContext){.project = project, .tolerance = 20, .grid_step = 10, .skip_vertex = -1, .align = true};
}

/* Взять target в from, протащить в to, отпустить. */
static bool drag(TlProject *project, TlDragTarget target, TlVec2 from, TlVec2 to, bool free)
{
    TlDrag state;
    if (!tl_drag_begin(&state, project, target, from))
        return false;
    TlSnapContext snap = snap_of(project);
    tl_drag_move(&state, project, to, &snap, free, NULL);
    tl_drag_end(&state, project);
    return true;
}

void spec_drag(void)
{
    DESCRIBE("Перетаскивание (tl_drag)")
    {
        IT("угол дома — с привязкой к сетке; стена — параллельно себе; дом — целиком с перегородкой")
        {
            TlProject *project = house_with_partition();
            TlOutline *house = project->outlines.items[0];
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_VERTEX, .outline = house, .index = 2},
                        tl_vec2(6000, 4000), tl_vec2(6503, 4498), false));
            EXPECT_NEAR(house->inner.items[2].x, 6500, 1e-6);
            EXPECT_NEAR(house->inner.items[2].y, 4500, 1e-6);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_WALL, .outline = house, .index = 0},
                        tl_vec2(3000, 0), tl_vec2(3000, -304), false)); /* верхняя стена — вверх, по сетке */
            EXPECT_NEAR(house->inner.items[0].y, -300, 1e-6);
            EXPECT_NEAR(house->inner.items[1].y, -300, 1e-6);
            TlPartition *partition = project->partitions.items[0];
            EXPECT_NEAR(partition->start.y, -300, 1e-6); /* перегородка следует за стеной */
            TlExpansionJoint *joint = tl_expansion_joint_new(tl_vec2(1000, -300), tl_vec2(1000, 4000), 5);
            tl_project_add_expansion_joint(project, joint);
            TlRuler *ruler = tl_ruler_new(tl_vec2(100, 100), tl_vec2(900, 100));
            tl_project_add_ruler(project, ruler);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_HOUSE, .outline = house}, tl_vec2(1000, 1000),
                        tl_vec2(2000, 1000), false));
            EXPECT_NEAR(joint->start.x, 2000, 1e-6); /* шов режет плитку дома — едет с ним */
            EXPECT_NEAR(ruler->start.x, 100, 1e-6);  /* линейка — измерение, остаётся */
            EXPECT_NEAR(house->inner.items[0].x, 1000, 1e-6);
            EXPECT_NEAR(project->partitions.items[0]->start.x, 4000, 1e-6);
            EXPECT_INT((int)project->spaces.count, 2);
            tl_project_free(project);
        }
        IT("перегородка: за тело — только поперёк; за свободный конец — вдоль оси; за угол тела — форма")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            tl_project_add_contour(project, corners, 4, 200, 100);
            TlPartition *partition = tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 2000), 100);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_PARTITION, .partition = partition,
                                                .index = TL_DRAG_PARTITION_BODY},
                        tl_vec2(3000, 1000), tl_vec2(3500, 1300), false));
            EXPECT_NEAR(partition->start.x, 3500, 1e-6);
            EXPECT_NEAR(partition->end.y, 2000, 1e-6); /* вдоль себя не сдвинулась */
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_PARTITION, .partition = partition,
                                                .index = TL_DRAG_PARTITION_END},
                        tl_vec2(3500, 2000), tl_vec2(3504, 2600), false));
            EXPECT_NEAR(partition->end.x, 3500, 1e-6); /* у оси — растягивание по ней */
            EXPECT_NEAR(partition->end.y, 2600, 1e-6);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_PARTITION, .partition = partition,
                                                .index = TL_DRAG_PARTITION_FIRST_CORNER + TL_PARTITION_END_LEFT},
                        tl_vec2(3450, 2600), tl_vec2(3400, 2600), false));
            EXPECT(partition->corner_offsets[TL_PARTITION_END_LEFT] > 50);
            tl_project_free(project);
        }
        IT("проём — вдоль стены по сетке; заблокированное не тянется")
        {
            TlProject *project = house_with_partition();
            TlBand band;
            tl_project_find_band(project, tl_vec2(3000, 2000), 10, &band);
            double along;
            tl_band_local(&band, tl_vec2(3000, 2000), &along, NULL);
            TlOpening *door = tl_project_place_opening(project, &band, along, 800);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_OPENING, .opening = door}, tl_vec2(3000, 2000),
                        tl_vec2(3100, 2507), false));
            EXPECT_NEAR(door->center.x, 3000, 1e-6); /* остаётся на перегородке */
            EXPECT(fabs(door->center.y - 2500) < 10.5);
            door->locked = true;
            EXPECT(!drag(project, (TlDragTarget){.kind = TL_DRAG_OPENING, .opening = door}, door->center,
                         tl_vec2(3000, 1000), false));
            project->partitions.items[0]->locked = true;
            EXPECT(!drag(project, (TlDragTarget){.kind = TL_DRAG_HOUSE, .outline = project->outlines.items[0]},
                         tl_vec2(1000, 1000), tl_vec2(2000, 1000), false));
            tl_project_free(project);
        }
        IT("узел — целиком; примыкание за грань становится узлом, и его тянут дальше")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            tl_project_add_contour(project, corners, 4, 200, 100);
            TlPartition *a = tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 2000), 100);
            tl_project_draw_partition(project, tl_vec2(3000, 2000), tl_vec2(6000, 2000), 100);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_NODE, .partition = a, .end = TL_PARTITION_END},
                        tl_vec2(3000, 2000), tl_vec2(2500, 2500), true));
            EXPECT_NEAR(a->end.x, 2500, 1e-6);
            EXPECT_NEAR(a->end.y, 2500, 1e-6);
            tl_project_free(project);

            project = tl_project_new();
            tl_project_add_contour(project, corners, 4, 200, 100);
            TlPartition *host = tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 4000), 100);
            TlPartition *branch = tl_project_draw_partition(project, tl_vec2(3050, 2000), tl_vec2(6000, 2000), 100);
            TlTeeList tees = tl_project_tees(project);
            EXPECT_INT((int)tees.count, 1);
            tl_tee_list_free(&tees);
            TlDrag state;
            EXPECT(tl_drag_begin(&state, project, (TlDragTarget){.kind = TL_DRAG_TEE, .partition = branch,
                                                                 .end = TL_PARTITION_START},
                                 tl_vec2(3050, 2000)));
            TlSnapContext snap = snap_of(project);
            tl_drag_move(&state, project, tl_vec2(2500, 2000), &snap, true, NULL); /* за грань */
            EXPECT_INT(state.target.kind, TL_DRAG_NODE);
            tl_drag_move(&state, project, tl_vec2(2400, 2100), &snap, true, NULL);
            tl_drag_end(&state, project);
            EXPECT(host != NULL);
            EXPECT_INT((int)project->partitions.count, 3); /* хозяин поделён узлом */
            TlNodeList nodes = tl_project_nodes(project);
            bool moved = false;
            for (size_t i = 0; i < nodes.count; i++)
                moved = moved || (fabs(nodes.items[i].point.x - 2400) < 1 && fabs(nodes.items[i].point.y - 2100) < 1);
            EXPECT(moved);
            tl_node_list_free(&nodes);
            tl_project_free(project);
        }
        IT("стык в проёме: целиком — поперёк стены, за конец — наискосок; линейка и шов — целиком и за конец")
        {
            TlProject *project = house_with_partition();
            TlBand band;
            tl_project_find_band(project, tl_vec2(3000, 2000), 10, &band);
            double along;
            tl_band_local(&band, tl_vec2(3000, 2000), &along, NULL);
            TlOpening *door = tl_project_place_opening(project, &band, along, 800);
            TlVec2 from[2], to[2];
            EXPECT(tl_project_opening_jambs(project, door, from, to));
            TlVec2 middle = tl_vec2_midpoint(tl_vec2_midpoint(from[0], from[1]), tl_vec2_midpoint(to[0], to[1]));
            double before = door->threshold;
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_THRESHOLD, .opening = door, .index = TL_DRAG_THRESHOLD_LINE},
                        middle, tl_vec2_add(middle, tl_vec2(-100, 0)), true)); /* к одной из граней */
            EXPECT(fabs(door->threshold - before) > 0.4);
            EXPECT_NEAR(door->threshold, door->threshold_end, 1e-9); /* целиком — без наклона */
            TlVec2 other_side = door->threshold_end > 0.5 ? to[0] : to[1]; /* конец — к противоположной грани */
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_THRESHOLD, .opening = door, .index = TL_DRAG_THRESHOLD_TO},
                        tl_vec2_midpoint(to[0], to[1]), other_side, true));
            EXPECT(fabs(door->threshold - door->threshold_end) > 0.5); /* наискосок */

            TlRuler *ruler = tl_ruler_new(tl_vec2(500, 500), tl_vec2(1500, 500));
            tl_project_add_ruler(project, ruler);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_RULER, .ruler = ruler, .index = 0}, tl_vec2(1000, 500),
                        tl_vec2(1000, 800), false));
            EXPECT_NEAR(ruler->start.y, 800, 1e-9);
            TlDrag state;
            EXPECT(tl_drag_begin(&state, project, (TlDragTarget){.kind = TL_DRAG_RULER, .ruler = ruler, .index = 2},
                                 tl_vec2(1500, 800)));
            TlSnapContext snap = snap_of(project);
            snap.magnet = true;
            tl_drag_move(&state, project, tl_vec2(2948, 810), &snap, false, NULL); /* к грани перегородки */
            EXPECT_INT(state.measure.kind != TL_SNAP_NONE, 1);
            EXPECT_NEAR(ruler->end.x, 2950, 1e-6);
            tl_drag_end(&state, project);

            TlExpansionJoint *joint = tl_expansion_joint_new(tl_vec2(1000, 0), tl_vec2(1000, 4000), 5);
            tl_project_add_expansion_joint(project, joint);
            tl_project_update_spaces(project);
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_JOINT, .joint = joint, .index = 0}, tl_vec2(1000, 2000),
                        tl_vec2(1200, 2000), false));
            EXPECT_NEAR(joint->start.x, 1200, 1e-9);
            tl_project_free(project);
        }
        IT("сетка плитки комнаты — сдвиг раскладки целыми мм, в осях раскладки")
        {
            TlProject *project = house_with_partition();
            TlSpace *space = tl_project_space_at(project, tl_vec2(1000, 1000));
            TlTileSettings *tile = tl_space_effective_tile(space);
            tile->rotation_degrees = 90;
            EXPECT(drag(project, (TlDragTarget){.kind = TL_DRAG_TILES, .space = space}, tl_vec2(1000, 1000),
                        tl_vec2(1000, 1100.4), false));
            EXPECT_NEAR(tile->offset_x, 100, 1e-9); /* поворот 90°: вниз на экране — вдоль раскладки */
            EXPECT_NEAR(tile->offset_y, 0, 1e-9);
            tl_project_free(project);
        }
    }
}

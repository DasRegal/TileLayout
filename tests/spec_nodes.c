#include "spec.h"
#include "specs.h"

#include <math.h>
#include <stdlib.h>

#include "tl_project.h"
#include "tl_project_io.h"

/* Дом 4000×3000 (стены 200): вертикальная перегородка A по x = 2000 (толщина 100) и
   горизонтальная B от левой стены, упёршаяся торцом в левый бок A (x = 1950). */
static TlProject *tee_project(void)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
    tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 1000), tl_vec2(1950, 1000), 80));
    return project;
}

static double union_area(TlProject *project)
{
    TlPolygonList bodies = tl_project_partition_bodies(project, 0);
    double area = 0;
    for (size_t i = 0; i < bodies.count; i++)
        area += fabs(tl_polygon_signed_area(&bodies.items[i]));
    tl_polygon_list_free_all(&bodies);
    return area;
}

static bool single_node(TlProject *project, TlNode *node)
{
    TlNodeList nodes = tl_project_nodes(project);
    bool one = nodes.count == 1;
    if (one)
        *node = nodes.items[0];
    tl_node_list_free(&nodes);
    return one;
}

static bool near_point(TlVec2 a, double x, double y) { return fabs(a.x - x) < 1e-6 && fabs(a.y - y) < 1e-6; }

void spec_nodes(void)
{
    DESCRIBE("Примыкание: перегородка упирается в бок другой")
    {
        IT("та остаётся целой, точка примыкания — на её боку")
        {
            TlProject *project = tee_project();
            tl_project_update_spaces(project);
            EXPECT_INT(project->partitions.count, 2);
            TlTeeList tees = tl_project_tees(project);
            EXPECT_INT(tees.count, 1);
            if (tees.count == 1) {
                EXPECT(tees.items[0].host == project->partitions.items[0]);
                EXPECT(near_point(tees.items[0].point, 1950, 1000));
                EXPECT_INT(tees.items[0].side, +1); /* ось A — вверх по y, x = 1950 — слева */
            }
            tl_tee_list_free(&tees);
            TlPolygon body = tl_project_partition_body(project, project->partitions.items[1], 0);
            EXPECT_NEAR(tl_polygon_bounds(&body).right, 1950, 1e-6); /* торец — по боку */
            tl_polygon_free(&body);
            tl_project_free(project);
        }
        IT("точку тянут вдоль — конец скользит по боку, перегородка целая")
        {
            TlProject *project = tee_project();
            TlTee tee;
            EXPECT(tl_project_tee_of(project, project->partitions.items[1], TL_PARTITION_END, &tee));
            EXPECT(!tl_project_move_tee(project, &tee, tl_vec2(1990, 1500))); /* внутри тела A */
            EXPECT_INT(project->partitions.count, 2);
            EXPECT(near_point(project->partitions.items[1]->end, 1950, 1500));
            tl_project_free(project);
        }
        IT("увели за грань — перегородка делится, получается узел из трёх в этой точке")
        {
            TlProject *project = tee_project();
            TlTee tee;
            EXPECT(tl_project_tee_of(project, project->partitions.items[1], TL_PARTITION_END, &tee));
            EXPECT(tl_project_move_tee(project, &tee, tl_vec2(2300, 1200)));
            EXPECT_INT(project->partitions.count, 3);
            TlNode node = {0};
            EXPECT(single_node(project, &node));
            EXPECT_INT(node.count, 3);
            EXPECT(near_point(node.point, 2300, 1200));
            /* дальние концы A на месте */
            EXPECT(near_point(project->partitions.items[0]->start, 2000, 0));
            EXPECT(near_point(project->partitions.items[1]->end, 2000, 3000));
            tl_project_free(project);
        }
        IT("узел вернули на прямую — он исчезает, перегородка снова одна, а другая упирается в её бок")
        {
            TlProject *project = tee_project();
            TlTee tee;
            EXPECT(tl_project_tee_of(project, project->partitions.items[1], TL_PARTITION_END, &tee));
            EXPECT(tl_project_move_tee(project, &tee, tl_vec2(2300, 1200)));
            TlNode node = {0};
            EXPECT(single_node(project, &node));
            tl_project_move_node(project, &node, tl_vec2(2000, 1200));
            tl_project_merge_straight_nodes(project);
            EXPECT_INT(project->partitions.count, 2);
            TlPartition *host = project->partitions.items[0], *branch = project->partitions.items[1];
            EXPECT(near_point(host->start, 2000, 0) && near_point(host->end, 2000, 3000));
            /* Б повернулась за узлом вокруг дальнего конца (0, 1000) и упирается в бок по своей оси */
            EXPECT(near_point(branch->end, 1950, 1000 + 200 * 1950 / 2000.0));
            TlTeeList tees = tl_project_tees(project);
            EXPECT_INT(tees.count, 1);
            tl_tee_list_free(&tees);
            tl_project_free(project);
        }
        IT("узел с изломом и части разной толщины сами не сливаются")
        {
            TlProject *project = tee_project();
            TlTee tee;
            EXPECT(tl_project_tee_of(project, project->partitions.items[1], TL_PARTITION_END, &tee));
            EXPECT(tl_project_move_tee(project, &tee, tl_vec2(2300, 1200)));
            tl_project_merge_straight_nodes(project);
            EXPECT_INT(project->partitions.count, 3); /* излом */
            TlProject *steps = tl_project_new();
            tl_project_add_partition(steps, tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100));
            tl_project_add_partition(steps, tl_partition_new(tl_vec2(1000, 0), tl_vec2(2000, 0), 200));
            tl_project_merge_straight_nodes(steps);
            EXPECT_INT(steps->partitions.count, 2); /* ступенька */
            tl_project_free(steps);
            tl_project_free(project);
        }
        IT("узел удалили — самая прямая пара выпрямляется в одну перегородку")
        {
            TlProject *project = tee_project();
            TlTee tee;
            EXPECT(tl_project_tee_of(project, project->partitions.items[1], TL_PARTITION_END, &tee));
            EXPECT(tl_project_move_tee(project, &tee, tl_vec2(2300, 1200)));
            TlNode node = {0};
            EXPECT(single_node(project, &node));
            EXPECT(tl_project_dissolve_node(project, &node));
            EXPECT_INT(project->partitions.count, 2);
            TlPartition *host = project->partitions.items[0];
            EXPECT(near_point(host->start, 2000, 0) && near_point(host->end, 2000, 3000));
            EXPECT_NEAR(project->partitions.items[1]->end.x, 1950, 1e-6); /* упирается в бок */
            tl_project_free(project);
        }
        IT("правка: точку увели за грань — та ломается, её концы на стенах дома остаются на месте")
        {
            TlProject *project = tee_project();
            TlTee tee;
            EXPECT(tl_project_tee_of(project, project->partitions.items[1], TL_PARTITION_END, &tee));
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            EXPECT(tl_project_move_tee(project, &tee, tl_vec2(2500, 1500)));
            TlEditOptions options = {.frozen = tee.branch, .keep_ends = true};
            tl_project_end_edit(project, token, &options);
            EXPECT_INT(project->partitions.count, 3);
            EXPECT(near_point(project->partitions.items[0]->start, 2000, 0));
            EXPECT(near_point(project->partitions.items[0]->end, 2500, 1500));
            EXPECT(near_point(project->partitions.items[1]->end, 2000, 3000));
            TlNode node = {0};
            EXPECT(single_node(project, &node));
            EXPECT(tl_project_dissolve_node(project, &node));
            EXPECT(near_point(project->partitions.items[0]->start, 2000, 0) &&
                   near_point(project->partitions.items[0]->end, 2000, 3000));
            tl_project_free(project);
        }
        IT("тянут примкнувшую перегородку — её конец остаётся на боку той, в которую она упирается")
        {
            TlProject *project = tee_project();
            TlPartition *branch = project->partitions.items[1];
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            tl_partition_translate(branch, tl_vec2(0, 500));
            TlEditOptions options = {.master = branch};
            tl_project_end_edit(project, token, &options);
            EXPECT_INT(project->partitions.count, 2);
            EXPECT(near_point(branch->end, 1950, 1500));
            tl_project_free(project);
        }
        IT("примыкание сохраняется в файле как есть")
        {
            TlProject *project = tee_project();
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->partitions.count, 2);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }

    DESCRIBE("Узел: середина и углы")
    {
        IT("крест из четырёх перегородок: середина узла — квадрат между боками")
        {
            TlProject *project = tl_project_new();
            const TlVec2 far[] = {{1000, 0}, {0, 1000}, {-1000, 0}, {0, -1000}};
            for (int i = 0; i < 4; i++)
                tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 0), far[i], 100));
            TlNode node = {0};
            EXPECT(single_node(project, &node));
            EXPECT_INT(node.count, 4);
            TlPolygon core;
            EXPECT(tl_node_core(&node, &core));
            EXPECT_NEAR(tl_polygon_signed_area(&core), 100 * 100, 1e-6);
            tl_polygon_free(&core);
            EXPECT_NEAR(union_area(project), 4 * 100 * 950 + 100 * 100, 1e-6);
            tl_project_free(project);
        }
        IT("узел переносится целиком: все концы — в новую точку, дальние концы на месте")
        {
            TlProject *project = tl_project_new();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 1000), 100));
            TlNode node = {0};
            EXPECT(single_node(project, &node));
            tl_project_move_node(project, &node, tl_vec2(900, 100));
            TlPartition *first = project->partitions.items[0], *second = project->partitions.items[1];
            EXPECT(near_point(first->end, 900, 100) && near_point(second->start, 900, 100));
            EXPECT(near_point(first->start, 0, 0) && near_point(second->end, 1000, 1000));
            tl_project_free(project);
        }
        IT("наружный угол в новую точку: меняются углы обоих тел у узла, дальние — на месте")
        {
            TlProject *project = tl_project_new();
            TlPartition *first = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            TlPartition *second = tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 1000), 100);
            tl_project_add_partition(project, first);
            tl_project_add_partition(project, second);
            TlNode node = {0};
            TlNodeCorner corners[TL_NODE_MAX];
            EXPECT(single_node(project, &node));
            tl_node_corners(&node, corners);
            size_t outer = near_point(corners[0].first, 1050, -50) ? 0 : 1;
            EXPECT(near_point(corners[outer].first, 1050, -50));
            tl_project_set_node_corner(project, &node, outer, tl_vec2(1080, -70));
            EXPECT(single_node(project, &node));
            tl_node_corners(&node, corners);
            EXPECT(near_point(corners[outer].first, 1080, -70));
            /* дальние углы: у first начало — правый бок 50; у second конец — правый бок 50 */
            EXPECT_NEAR(first->corner_offsets[TL_PARTITION_START_RIGHT], 50, 1e-9);
            EXPECT_NEAR(second->corner_offsets[TL_PARTITION_END_RIGHT], 50, 1e-9);
            EXPECT(first->corner_offsets[TL_PARTITION_END_RIGHT] > 50);
            EXPECT(second->corner_offsets[TL_PARTITION_START_RIGHT] > 50);
            tl_project_free(project);
        }
    }

    DESCRIBE("Узел: угол за осью")
    {
        IT("угол увели за ось — бок ложится на ось, толщина не растёт обратно")
        {
            TlProject *project = tl_project_new();
            TlPartition *first = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            TlPartition *second = tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 1000), 100);
            tl_project_add_partition(project, first);
            tl_project_add_partition(project, second);
            TlNode node = {0};
            TlNodeCorner corners[TL_NODE_MAX];
            EXPECT(single_node(project, &node));
            tl_node_corners(&node, corners);
            size_t outer = near_point(corners[0].first, 1050, -50) ? 0 : 1;
            tl_project_set_node_corner(project, &node, outer, tl_vec2(950, 50)); /* за обе оси, внутрь угла */
            EXPECT_NEAR(first->corner_offsets[TL_PARTITION_END_RIGHT], 0, 1e-9);
            EXPECT_NEAR(second->corner_offsets[TL_PARTITION_START_RIGHT], 0, 1e-9);
            tl_project_free(project);
        }
    }

    DESCRIBE("Разбивка грани для размеров от проёма")
    {
        IT("размер от проёма — до грани примкнувшей перегородки, а не до конца стены")
        {
            TlProject *project = tee_project(); /* Б (толщина 80) примкнула слева к A на y = 1000 */
            TlOpening *door = tl_opening_new(tl_vec2(2000, 2000), 800);
            tl_project_add_opening(project, door);
            TlBand band;
            EXPECT(tl_project_opening_host(project, door, &band));
            double along, before = -1, after = -1;
            tl_band_local(&band, door->center, &along, NULL);
            double origin = band.origin.y; /* ось A идёт вверх по y */
            /* левый бок A (x = 1950) — там Б: до её грани y = 1040 */
            EXPECT(tl_project_opening_gaps(project, &band, +1, door, along - 400, along + 400, &before, &after));
            EXPECT_NEAR(origin + before, 1040, 1e-6);
            EXPECT_NEAR(origin + after, 3000, 1e-6);
            /* правый бок — без примыканий: A меряется целиком, до y = 0 */
            EXPECT(tl_project_opening_gaps(project, &band, -1, door, along - 400, along + 400, &before, &after));
            EXPECT_NEAR(origin + before, 0, 1e-6);
            tl_project_free(project);
        }
        IT("у стены — до грани перегородки, упёртой в стену, и до соседнего проёма")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            TlOpening *door = tl_opening_new(tl_vec2(1000, -100), 800);
            TlOpening *window = tl_opening_new(tl_vec2(3000, -100), 600);
            tl_project_add_opening(project, door);
            tl_project_add_opening(project, window);
            TlBand band;
            EXPECT(tl_project_opening_host(project, door, &band));
            double along, before = -1, after = -1;
            tl_band_local(&band, door->center, &along, NULL);
            EXPECT(tl_project_opening_gaps(project, &band, 0, door, along - 400, along + 400, &before, &after));
            TlVec2 at = tl_band_point(&band, after, 0);
            EXPECT_NEAR(at.x, 1950, 1e-6); /* грань перегородки */
            tl_band_local(&band, window->center, &along, NULL);
            EXPECT(tl_project_opening_gaps(project, &band, 0, window, along - 300, along + 300, &before, &after));
            EXPECT_NEAR(tl_band_point(&band, before, 0).x, 2050, 1e-6);
            EXPECT_NEAR(tl_band_point(&band, after, 0).x, 4000, 1e-6);
            tl_project_free(project);
        }
    }
}

#include "spec.h"
#include "specs.h"

#include <math.h>
#include <stdlib.h>

#include "tl_project.h"
#include "tl_project_io.h"

static TlProject *house(const TlVec2 *corners, size_t count)
{
    TlProject *project = tl_project_new();
    tl_project_add_outline(project, tl_outline_new(corners, count, 200));
    return project;
}

static bool has_vertex(const TlPolygon *polygon, double x, double y)
{
    for (size_t i = 0; i < polygon->count; i++)
        if (fabs(polygon->items[i].x - x) < 1e-6 && fabs(polygon->items[i].y - y) < 1e-6)
            return true;
    return false;
}

/* Углы единственного узла проекта; число углов (0 — узла нет). */
static size_t node_corners(TlProject *project, TlNodeCorner *corners, TlNode *node)
{
    TlNodeList nodes = tl_project_nodes(project);
    size_t count = 0;
    if (nodes.count == 1) {
        *node = nodes.items[0];
        count = tl_node_corners(node, corners);
    }
    tl_node_list_free(&nodes);
    return count;
}

static double bodies_area(TlProject *project)
{
    TlPolygonList bodies = tl_project_partition_bodies(project, 0);
    double area = 0;
    for (size_t i = 0; i < bodies.count; i++)
        area += fabs(tl_polygon_signed_area(&bodies.items[i]));
    tl_polygon_list_free_all(&bodies);
    return area;
}

static double end_thickness_after_loading(const char *text)
{
    TlProject *project = tl_project_new();
    char error[128] = "";
    double thickness = -1;
    if (tl_project_from_string(project, text, error, sizeof error))
        thickness = tl_partition_thickness_at(project->partitions.items[0], TL_PARTITION_END);
    tl_project_free(project);
    return thickness;
}

void spec_partition_shape(void)
{
    DESCRIBE("Форма перегородки: каждый угол тела — свой отступ от оси")
    {
        IT("по умолчанию — прямоугольник с осью посередине")
        {
            TlPartition *partition = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            EXPECT(tl_partition_is_uniform(partition));
            EXPECT_NEAR(tl_partition_thickness_at(partition, TL_PARTITION_START), 100, 1e-9);
            EXPECT_NEAR(partition->corner_offsets[TL_PARTITION_END_RIGHT], 50, 1e-9);
            tl_partition_free(partition);
        }
        IT("сдвиг одного угла меняет только этот угол, остальные на месте")
        {
            TlPartition *partition = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            partition->corner_offsets[TL_PARTITION_END_LEFT] = 30;
            TlPolygon body = tl_partition_body(partition, 0);
            EXPECT(has_vertex(&body, 0, 50));
            EXPECT(has_vertex(&body, 1000, 30));
            EXPECT(has_vertex(&body, 1000, -50));
            EXPECT(has_vertex(&body, 0, -50));
            EXPECT_NEAR(tl_polygon_area(&body), 90000, 1e-6);
            EXPECT_NEAR(tl_partition_thickness_at(partition, TL_PARTITION_END), 80, 1e-9);
            EXPECT(!tl_partition_is_uniform(partition));
            tl_polygon_free(&body);
            tl_partition_free(partition);
        }
        IT("знает, к какому концу и к какой стороне относится каждый угол")
        {
            EXPECT_INT(tl_partition_corner_end(TL_PARTITION_START_RIGHT), TL_PARTITION_START);
            EXPECT_INT(tl_partition_corner_end(TL_PARTITION_END_LEFT), TL_PARTITION_END);
            EXPECT_INT(tl_partition_corner_side(TL_PARTITION_END_LEFT), +1);
            EXPECT_INT(tl_partition_corner_side(TL_PARTITION_START_RIGHT), -1);
        }
        IT("толщина одним числом снова делает перегородку прямоугольной")
        {
            TlPartition *partition = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            partition->corner_offsets[TL_PARTITION_START_RIGHT] = 10;
            tl_partition_set_thickness(partition, 120);
            EXPECT(tl_partition_is_uniform(partition));
            EXPECT_NEAR(tl_partition_max_thickness(partition), 120, 1e-9);
            tl_partition_free(partition);
        }
        IT("бок перегородки — грань для других перегородок — проходит через её углы")
        {
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            TlProject *project = house(corners, 4);
            TlPartition *partition = tl_partition_new(tl_vec2(0, 1500), tl_vec2(1000, 1500), 100);
            partition->corner_offsets[TL_PARTITION_END_LEFT] = 30;
            tl_project_add_partition(project, partition);
            TlHost host = {TL_HOST_PARTITION, NULL, 0, partition};
            TlVec2 point, direction;
            EXPECT(tl_project_face_line(project, host, +1, &point, &direction));
            EXPECT_NEAR(point.y, 1550, 1e-9);
            EXPECT_NEAR(direction.y / direction.x, -20.0 / 1000.0, 1e-9);
            tl_project_free(project);
        }
        IT("поворачивается вокруг начала на заданный угол, длина сохраняется")
        {
            TlPartition *partition = tl_partition_new(tl_vec2(100, 100), tl_vec2(1100, 100), 100);
            tl_partition_set_angle(partition, 90);
            EXPECT_NEAR(tl_partition_angle(partition), 90, 1e-9);
            EXPECT_NEAR(tl_partition_length(partition), 1000, 1e-9);
            EXPECT_NEAR(partition->end.x, 100, 1e-9);
            tl_partition_free(partition);
        }
        IT("сохраняет все четыре угла в файле")
        {
            TlProject *project = tl_project_new();
            TlPartition *partition = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            partition->corner_offsets[TL_PARTITION_END_RIGHT] = 75;
            tl_project_add_partition(project, partition);
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_NEAR(loaded->partitions.items[0]->corner_offsets[TL_PARTITION_END_RIGHT], 75, 1e-9);
            EXPECT_NEAR(loaded->partitions.items[0]->corner_offsets[TL_PARTITION_END_LEFT], 50, 1e-9);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("открывает старые файлы: одна толщина или толщина у начала и у конца")
        {
            EXPECT_NEAR(end_thickness_after_loading("{\"partitions\": [{\"p1\": [0, 0], \"p2\": [500, 0], "
                                                    "\"thick\": 80}]}"),
                        80, 1e-9);
            EXPECT_NEAR(end_thickness_after_loading("{\"partitions\": [{\"p1\": [0, 0], \"p2\": [500, 0], "
                                                    "\"thick\": 80, \"thick_end\": 60}]}"),
                        60, 1e-9);
        }
    }

    DESCRIBE("Примыкание перегородки: торец срезан точно по грани")
    {
        IT("у наклонной стены торец идёт по стене — без щели и нахлёста")
        {
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 2000}};
            TlProject *project = house(corners, 4);
            TlPartition *partition = tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 2250), 100);
            tl_project_add_partition(project, partition);
            TlPolygon body = tl_project_partition_body(project, partition, 0);
            EXPECT(has_vertex(&body, 950, 2237.5));
            EXPECT(has_vertex(&body, 1050, 2262.5));
            EXPECT(has_vertex(&body, 950, 0));
            tl_polygon_free(&body);
            tl_project_free(project);
        }
        IT("у бока другой перегородки торец идёт по этому боку")
        {
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            TlProject *project = house(corners, 4);
            TlPartition *slanted = tl_partition_new(tl_vec2(0, 1000), tl_vec2(4000, 1000), 100);
            slanted->corner_offsets[TL_PARTITION_END_LEFT] = 250; /* бок от (0, 1050) к (4000, 1250) */
            tl_project_add_partition(project, slanted);
            TlPartition *branch = tl_partition_new(tl_vec2(2000, 1150), tl_vec2(2000, 3000), 100);
            tl_project_add_partition(project, branch);
            TlPolygon body = tl_project_partition_body(project, branch, 0);
            EXPECT(has_vertex(&body, 1950, 1147.5));
            EXPECT(has_vertex(&body, 2050, 1152.5));
            tl_polygon_free(&body);
            tl_project_free(project);
        }
        IT("угол «на ус» между перегородками следует за сдвинутым боком")
        {
            TlProject *project = tl_project_new();
            TlPartition *first = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            first->corner_offsets[TL_PARTITION_START_RIGHT] = 80;
            first->corner_offsets[TL_PARTITION_END_RIGHT] = 80; /* наружный бок — на 80 от оси */
            tl_project_add_partition(project, first);
            TlPartition *second = tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 1000), 100);
            tl_project_add_partition(project, second);
            TlPolygon first_body = tl_project_partition_body(project, first, 0);
            TlPolygon second_body = tl_project_partition_body(project, second, 0);
            EXPECT(has_vertex(&first_body, 1050, -80) && has_vertex(&second_body, 1050, -80));
            EXPECT(has_vertex(&first_body, 950, 50) && has_vertex(&second_body, 950, 50));
            tl_polygon_free(&first_body);
            tl_polygon_free(&second_body);
            tl_project_free(project);
        }
    }

    DESCRIBE("Угол «на ус» между двумя перегородками")
    {
        IT("тела сходятся по линии от внутреннего угла к наружному — без щели и нахлёста")
        {
            TlProject *project = tl_project_new();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 1000), 100));
            TlNode node = {0};
            TlNodeCorner corners[TL_NODE_MAX];
            EXPECT_INT(node_corners(project, corners, &node), 2);
            EXPECT(corners[0].joined && corners[1].joined);
            /* Г из полос 100: 1050 × 100 + 100 × 950 */
            EXPECT_NEAR(bodies_area(project), 1050 * 100 + 100 * 950, 1e-6);
            TlPolygonList bodies = tl_project_partition_bodies(project, 0);
            EXPECT_INT(bodies.count, 2); /* у двух перегородок середины узла нет */
            tl_polygon_list_free_all(&bodies);
            tl_project_free(project);
        }
        IT("при остром угле и сужающихся перегородках наружный угол не выходит за наружные бока")
        {
            TlProject *project = tl_project_new();
            TlVec2 joint = tl_vec2(3526, 2366);
            TlPartition *slanted = tl_partition_new(tl_vec2(3911, 0), joint, 365);
            slanted->corner_offsets[TL_PARTITION_START_LEFT] = slanted->corner_offsets[TL_PARTITION_START_RIGHT] = 417;
            TlPartition *tapered = tl_partition_new(joint, tl_vec2(6400, 2402), 365);
            tapered->corner_offsets[TL_PARTITION_END_LEFT] = tapered->corner_offsets[TL_PARTITION_END_RIGHT] = 60;
            tl_project_add_partition(project, slanted);
            tl_project_add_partition(project, tapered);
            TlNode node = {0};
            TlNodeCorner corners[TL_NODE_MAX];
            EXPECT_INT(node_corners(project, corners, &node), 2);
            /* наружная сторона поворота — слева и снизу от узла: там один из углов, и внутрь он не заходит */
            const TlNodeCorner *outer = corners[0].first.x < corners[1].first.x ? &corners[0] : &corners[1];
            EXPECT(outer->first.x <= joint.x + 1e-6 && outer->second.x <= joint.x + 1e-6);
            EXPECT(outer->first.y >= joint.y - 40 && outer->second.y >= joint.y - 40);
            tl_project_free(project);
        }
        IT("очень острый угол не тянется далеко за узел")
        {
            TlProject *project = tl_project_new();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(1000, 0), tl_vec2(0, 100), 100));
            TlNode node = {0};
            TlNodeCorner corners[TL_NODE_MAX];
            size_t count = node_corners(project, corners, &node);
            EXPECT_INT(count, 2);
            for (size_t k = 0; k < count; k++)
                EXPECT(tl_distance(corners[k].first, node.point) < 3 * 100 &&
                       tl_distance(corners[k].second, node.point) < 3 * 100);
            tl_project_free(project);
        }
        IT("все тела и середины узлов обходятся в одну сторону — иначе при заливке остаются дыры")
        {
            TlProject *project = tl_project_new();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(1000, 1000), tl_vec2(1000, 0), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(0, 2000), tl_vec2(0, 0), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(1000, 0), tl_vec2(2000, 0), 100));
            TlPolygonList bodies = tl_project_partition_bodies(project, 0);
            EXPECT_INT(bodies.count, 5); /* четыре тела и середина узла из трёх */
            for (size_t i = 0; i < bodies.count; i++)
                EXPECT(tl_polygon_signed_area(&bodies.items[i]) > 0);
            tl_polygon_list_free_all(&bodies);
            tl_project_free(project);
        }
    }

    DESCRIBE("Проём в стене и перегородке")
    {
        IT("в перегородке-трапеции вырез повторяет её форму, а не накрывает прямоугольником")
        {
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            TlProject *project = house(corners, 4);
            TlPartition *partition = tl_partition_new(tl_vec2(0, 1500), tl_vec2(2000, 1500), 100);
            partition->corner_offsets[TL_PARTITION_END_LEFT] = 150; /* бок от (0, 1550) к (2000, 1650) */
            tl_project_add_partition(project, partition);
            tl_project_add_opening(project, tl_opening_new(tl_vec2(1000, 1500), 800));
            TlPolygon cut;
            EXPECT(tl_project_opening_cut(project, project->openings.items[0], &cut));
            /* трапеция 800 × (130…170) и запас 1 мм за каждым боком, чтобы вырез закрыл обводку стены */
            EXPECT_NEAR(tl_polygon_area(&cut), 800 * (130 + 170) / 2.0 + 800 * 2, 2);
            TlRect bounds = tl_polygon_bounds(&cut);
            EXPECT_NEAR(bounds.left, 600, 1e-6);
            EXPECT_NEAR(bounds.right, 1400, 1e-6);
            EXPECT(bounds.bottom < 1622 && bounds.bottom > 1620); /* у правого края бок на 1620 */
            tl_polygon_free(&cut);
            tl_project_free(project);
        }
        IT("подпись толщины встаёт на самый длинный участок стены без проёмов")
        {
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            TlProject *project = house(corners, 4);
            tl_project_add_opening(project, tl_opening_new(tl_vec2(1000, -100), 800)); /* верхняя стена */
            TlBand band;
            TlHost top_wall = {TL_HOST_WALL, project->outlines.items[0], 0, NULL};
            EXPECT(tl_project_band_of(project, top_wall, &band));
            double from, to;
            tl_project_solid_stretch(project, &band, &from, &to);
            EXPECT_NEAR(from, 1400, 1e-6);
            EXPECT_NEAR(to, 4000, 1e-6);
            tl_project_free(project);
        }
        IT("стена без проёмов — целиком, перегородка с проёмом — в обход него")
        {
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            TlProject *project = house(corners, 4);
            TlPartition *partition = tl_partition_new(tl_vec2(0, 1500), tl_vec2(4000, 1500), 100);
            tl_project_add_partition(project, partition);
            tl_project_add_opening(project, tl_opening_new(tl_vec2(3000, 1500), 800));
            TlBand band;
            double from, to;
            TlHost left_wall = {TL_HOST_WALL, project->outlines.items[0], 3, NULL};
            EXPECT(tl_project_band_of(project, left_wall, &band));
            tl_project_solid_stretch(project, &band, &from, &to);
            EXPECT_NEAR(to - from, 3000, 1e-6);
            TlHost host = {TL_HOST_PARTITION, NULL, 0, partition};
            EXPECT(tl_project_band_of(project, host, &band));
            tl_project_solid_stretch(project, &band, &from, &to);
            EXPECT_NEAR(from, 0, 1e-6);
            EXPECT_NEAR(to, 2600, 1e-6);
            tl_project_free(project);
        }
    }
}

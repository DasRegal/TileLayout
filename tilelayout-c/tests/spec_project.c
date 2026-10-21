#include "spec.h"
#include "specs.h"

#include <string.h>

#include "tl_project.h"

static TlSpace *space_named(TlProject *project, const char *name)
{
    for (size_t i = 0; i < project->spaces.count; i++)
        if (strcmp(project->spaces.items[i]->name, name) == 0)
            return project->spaces.items[i];
    return NULL;
}

static double area_m2(const TlSpace *space) { return tl_space_area(space) / 1e6; }

static TlProject *single_room_project(void)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

void spec_project(void)
{
    DESCRIBE("Проект: комнаты образуются стенами и перегородками")
    {
        IT("в демо-проекте две комнаты: Гостиная 17,63 м² и Ванная 4,87 м²")
        {
            TlProject *project = tl_project_demo();
            EXPECT_INT(project->spaces.count, 2);
            TlSpace *living = space_named(project, "Гостиная");
            TlSpace *bath = space_named(project, "Ванная");
            EXPECT(living != NULL && bath != NULL);
            if (living && bath) {
                EXPECT_NEAR(area_m2(living), 17.628, 0.001);
                EXPECT_NEAR(area_m2(bath), 4.867, 0.001);
            }
            tl_project_free(project);
        }
        IT("перегородка от стены до стены создаёт новую комнату с плиткой «родителя»")
        {
            TlProject *project = single_room_project();
            project->spaces.items[0]->tile.tile_width = 450;
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            EXPECT_INT(project->spaces.count, 2);
            TlSpace *created = space_named(project, "Комната 2");
            EXPECT(created != NULL);
            if (created)
                EXPECT_NEAR(created->tile.tile_width, 450, 1e-9);
            tl_project_free(project);
        }
        IT("после удаления перегородки комнаты сливаются, остаётся большая")
        {
            TlProject *project = single_room_project();
            tl_space_set_name(project->spaces.items[0], "Зал");
            TlPartition *wall = tl_partition_new(tl_vec2(1000, 0), tl_vec2(1000, 3000), 100);
            tl_project_add_partition(project, wall);
            tl_project_update_spaces(project);
            EXPECT_INT(project->spaces.count, 2);
            tl_project_delete_partition(project, wall);
            tl_project_update_spaces(project);
            EXPECT_INT(project->spaces.count, 1);
            EXPECT_STR(project->spaces.items[0]->name, "Зал");
            tl_project_free(project);
        }
        IT("общий пол: комната берёт раскладку у источника, неверный источник сбрасывается")
        {
            TlProject *project = single_room_project();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            TlSpace *first = project->spaces.items[0], *second = project->spaces.items[1];
            second->source_id = first->id;
            tl_project_update_spaces(project);
            EXPECT(second->layout_source == first);
            EXPECT(tl_space_effective_tile(second) == &first->tile);
            second->source_id = 999;
            tl_project_update_spaces(project);
            EXPECT_INT(second->source_id, 0);
            EXPECT(second->layout_source == second);
            tl_project_free(project);
        }
    }

    DESCRIBE("Проект: привязки при изменении геометрии")
    {
        IT("конец перегородки, упёртый в стену, следует за стеной")
        {
            TlProject *project = single_room_project();
            TlPartition *wall = tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100);
            tl_project_add_partition(project, wall);
            TlOutline *house = project->outlines.items[0];
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            tl_outline_shift_wall(house, 2, tl_vec2(0, 500)); /* нижняя стена вниз */
            tl_project_end_edit(project, token, NULL);
            EXPECT_NEAR(wall->end.y, 3500, 1e-6);
            EXPECT_NEAR(wall->start.y, 0, 1e-6);
            tl_project_free(project);
        }
        IT("при перетаскивании правка применяется на каждом движении, итог — по последнему")
        {
            TlProject *project = single_room_project();
            TlPartition *wall = tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100);
            tl_project_add_partition(project, wall);
            TlOutline *house = project->outlines.items[0];
            TlPolygon original = tl_polygon_copy(&house->inner);
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            const double moves[3] = {200, 700, 400};
            for (int i = 0; i < 3; i++) {
                tl_polygon_free(&house->inner);
                house->inner = tl_polygon_copy(&original);
                tl_outline_shift_wall(house, 2, tl_vec2(0, moves[i]));
                tl_project_apply_edit(project, token, NULL);
            }
            tl_edit_token_free(token);
            EXPECT_NEAR(wall->end.y, 3400, 1e-6);
            tl_polygon_free(&original);
            tl_project_free(project);
        }
        IT("перемещение дома переносит перегородки и проёмы внутри него")
        {
            TlProject *project = tl_project_demo();
            TlOutline *house = project->outlines.items[0];
            TlPartition *wall = project->partitions.items[0];
            TlVec2 before = wall->start;
            TlVec2 opening_before = project->openings.items[0]->center;
            TlEditToken *token = tl_project_begin_edit(project, house);
            tl_outline_translate(house, tl_vec2(1000, 500));
            TlEditOptions options = {.translate = {1000, 500}, .has_translate = true};
            tl_project_end_edit(project, token, &options);
            EXPECT_NEAR(wall->start.x, before.x + 1000, 1e-6);
            EXPECT_NEAR(wall->start.y, before.y + 500, 1e-6);
            EXPECT_NEAR(project->openings.items[0]->center.x, opening_before.x + 1000, 1e-6);
            EXPECT_INT(project->spaces.count, 2);
            tl_project_free(project);
        }
        IT("соединённые концы перегородок (узел) движутся вместе с ведущей")
        {
            TlProject *project = single_room_project();
            TlPartition *vertical = tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 2000), 100);
            TlPartition *horizontal = tl_partition_new(tl_vec2(2000, 2000), tl_vec2(0, 2000), 100);
            tl_project_add_partition(project, vertical);
            tl_project_add_partition(project, horizontal);
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            tl_partition_translate(vertical, tl_vec2(300, 0));
            TlEditOptions options = {.master = vertical};
            tl_project_end_edit(project, token, &options);
            EXPECT_NEAR(horizontal->start.x, 2300, 1e-6);
            EXPECT_NEAR(horizontal->end.x, 0, 1e-6);
            tl_project_free(project);
        }
        IT("угол двух перегородок заполняется — комната в углу замыкается")
        {
            TlProject *project = single_room_project();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 2000), 100));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 2000), tl_vec2(0, 2000), 100));
            tl_project_update_spaces(project);
            EXPECT_INT(project->spaces.count, 2);
            TlPolygonList bodies = tl_project_partition_bodies(project, 0);
            EXPECT_INT(bodies.count, 3); /* два тела и заполнение угла */
            tl_polygon_list_free_all(&bodies);
            tl_project_free(project);
        }
        IT("заблокированная перегородка не следует за стеной")
        {
            TlProject *project = single_room_project();
            TlPartition *wall = tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100);
            wall->locked = true;
            tl_project_add_partition(project, wall);
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            tl_outline_shift_wall(project->outlines.items[0], 2, tl_vec2(0, 500));
            tl_project_end_edit(project, token, NULL);
            EXPECT_NEAR(wall->end.y, 3000, 1e-6);
            tl_project_free(project);
        }
        IT("проём в перегородке едет вместе с перегородкой")
        {
            TlProject *project = tl_project_demo();
            TlPartition *wall = project->partitions.items[0]; /* вертикальная, x = 4260 */
            TlOpening *door = project->openings.items[0];
            TlEditToken *token = tl_project_begin_edit(project, NULL);
            tl_partition_translate(wall, tl_vec2(-200, 0));
            TlEditOptions options = {.master = wall};
            tl_project_end_edit(project, token, &options);
            EXPECT_NEAR(door->center.x, 4060, 1e-6);
            tl_project_free(project);
        }
        IT("при удалении перегородки удаляются её проёмы")
        {
            TlProject *project = tl_project_demo();
            tl_project_delete_partition(project, project->partitions.items[0]);
            tl_project_remove_orphan_openings(project);
            EXPECT_INT(project->openings.count, 0);
            tl_project_free(project);
        }
    }

    DESCRIBE("Проект: соседние дома с общей стеной")
    {
        IT("находит смежную стену и двигает её вместе со своей")
        {
            TlProject *project = tl_project_new();
            const TlVec2 left[] = {{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}};
            const TlVec2 right[] = {{3200, 0}, {6000, 0}, {6000, 3000}, {3200, 3000}};
            TlOutline *a = tl_outline_new(left, 4, 200);
            TlOutline *b = tl_outline_new(right, 4, 200);
            b->thickness.items[3] = 0; /* общая стена принадлежит левому дому */
            tl_project_add_outline(project, a);
            tl_project_add_outline(project, b);
            TlWallRefList linked = tl_project_linked_walls(project, a, 1);
            EXPECT_INT(linked.count, 1);
            tl_wall_ref_list_free(&linked);
            tl_project_shift_wall(project, a, 1, tl_vec2(300, 0), true);
            EXPECT_NEAR(b->inner.items[0].x, 3500, 1e-6);
            tl_project_set_thickness(project, a, 1, 300, true);
            EXPECT_NEAR(b->inner.items[0].x, 3600, 1e-6);
            tl_project_free(project);
        }
    }
}

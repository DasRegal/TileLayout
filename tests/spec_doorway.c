#include "spec.h"
#include "specs.h"

#include <math.h>
#include <stdlib.h>

#include "tl_layout.h"
#include "tl_project.h"
#include "tl_project_io.h"

#define DIVIDER_LOSS 100.0 /* мм²: стык — тончайшая щель между комнатами */

/* Дом 4000×3000 (стены 200), перегородка x = 2000 толщиной 100 с дверью 800 посередине.
   Левый бок перегородки — x = 1950 (по направлению сверху вниз), правый — x = 2050. */
static TlProject *two_rooms_with_door(double threshold)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
    TlOpening *door = tl_opening_new(tl_vec2(2000, 1500), 800);
    door->threshold = threshold;
    tl_project_add_opening(project, door);
    tl_project_update_spaces(project);
    return project;
}

static double area_of_room_at(TlProject *project, double x, double y)
{
    TlSpace *space = tl_project_space_at(project, tl_vec2(x, y));
    return space ? tl_space_area(space) : -1;
}

void spec_doorway(void)
{
    DESCRIBE("Пол под дверью и стык комнат")
    {
        IT("пол проёма в перегородке делится стыком между двумя комнатами — комнаты не сливаются")
        {
            TlProject *project = two_rooms_with_door(0.5);
            EXPECT_INT(project->spaces.count, 2);
            EXPECT_NEAR(area_of_room_at(project, 1000, 1500), 1950 * 3000 + 50 * 800, DIVIDER_LOSS);
            EXPECT_NEAR(area_of_room_at(project, 3000, 1500), 1950 * 3000 + 50 * 800, DIVIDER_LOSS);
            tl_project_free(project);
        }
        IT("плитка ложится и под дверью — до стыка")
        {
            TlProject *project = two_rooms_with_door(0.5);
            TlSpace *left = tl_project_space_at(project, tl_vec2(1000, 1500));
            EXPECT(left && tl_space_contains(left, tl_vec2(1990, 1500)));
            EXPECT(left && !tl_space_contains(left, tl_vec2(2010, 1500)));
            tl_project_free(project);
        }
        IT("стык у края двери отдаёт весь проём одной комнате")
        {
            TlProject *project = two_rooms_with_door(0); /* у правого бока */
            EXPECT_NEAR(area_of_room_at(project, 1000, 1500), 1950 * 3000 + 100 * 800, DIVIDER_LOSS);
            EXPECT_NEAR(area_of_room_at(project, 3000, 1500), 1950 * 3000, DIVIDER_LOSS);
            tl_project_free(project);
        }
        IT("линия стыка — от косяка до косяка, вдоль полотна двери")
        {
            TlProject *project = two_rooms_with_door(0.25);
            TlVec2 from, to;
            EXPECT(tl_project_opening_threshold(project, project->openings.items[0], &from, &to));
            EXPECT_NEAR(from.x, 2025, 1e-9); /* четверть толщины от правого бока */
            EXPECT_NEAR(to.x, 2025, 1e-9);
            EXPECT_NEAR(fabs(to.y - from.y), 800, 1e-9);
            tl_project_free(project);
        }
        IT("сечение проёма посередине — от правого бока к левому, по нему двигается стык")
        {
            TlProject *project = two_rooms_with_door(0.5);
            TlVec2 right, left;
            EXPECT(tl_project_opening_section(project, project->openings.items[0], &right, &left));
            EXPECT_NEAR(right.x, 2050, 1e-9);
            EXPECT_NEAR(left.x, 1950, 1e-9);
            EXPECT_NEAR(right.y, 1500, 1e-9);
            tl_project_free(project);
        }
        IT("проём в наружной стене пола не даёт: за ним не комната")
        {
            TlProject *project = two_rooms_with_door(0.5);
            double before = area_of_room_at(project, 1000, 1500);
            tl_project_add_opening(project, tl_opening_new(tl_vec2(1000, -100), 800));
            tl_project_update_spaces(project);
            TlPolygon doorway;
            EXPECT(!tl_project_opening_doorway(project, project->openings.items[1], &doorway));
            EXPECT_NEAR(area_of_room_at(project, 1000, 1500), before, 1e-6);
            tl_project_free(project);
        }
        IT("шов двери — зазор по линии стыка, поровну в обе комнаты")
        {
            TlProject *project = two_rooms_with_door(0.5);
            project->openings.items[0]->threshold_gap = 10;
            tl_project_update_spaces(project);
            EXPECT_INT(project->spaces.count, 2);
            EXPECT_NEAR(area_of_room_at(project, 1000, 1500), 1950 * 3000 + 45 * 800, DIVIDER_LOSS);
            EXPECT_NEAR(area_of_room_at(project, 3000, 1500), 1950 * 3000 + 45 * 800, DIVIDER_LOSS);
            TlSpace *left = tl_project_space_at(project, tl_vec2(1000, 1500));
            EXPECT(left && tl_space_contains(left, tl_vec2(1994, 1500)));
            EXPECT(left && !tl_space_contains(left, tl_vec2(1996, 1500)));
            tl_project_free(project);
        }
        IT("отступ плитки от стен у стыка не действует: зазор там — только шов двери")
        {
            TlProject *project = two_rooms_with_door(0.5);
            TlSpace *left = tl_project_space_at(project, tl_vec2(1000, 1500));
            left->tile.wall_gap = 20;
            TlLayout *layout = tl_space_layout(left);
            /* комната 1950×3000 минус 20 по стенам; пол проёма — до стыка, от косяков тоже 20 */
            double expected = 1910.0 * 2960 + (2000 - 1930) * 760.0;
            EXPECT_NEAR(fabs(tl_polygon_area(&layout->floor_outline)), expected, DIVIDER_LOSS);
            tl_project_free(project);
        }
        IT("новый проём — со стыком посередине и без шва; стык и шов сохраняются в файле")
        {
            TlOpening *opening = tl_opening_new(tl_vec2(0, 0), 800);
            EXPECT_NEAR(opening->threshold, 0.5, 1e-12);
            EXPECT_NEAR(opening->threshold_gap, 0, 1e-12);
            tl_opening_free(opening);
            TlProject *project = two_rooms_with_door(0.2);
            project->openings.items[0]->threshold_gap = 3;
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_NEAR(loaded->openings.items[0]->threshold, 0.2, 1e-12);
            EXPECT_NEAR(loaded->openings.items[0]->threshold_gap, 3, 1e-12);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

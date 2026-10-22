#include "spec.h"
#include "specs.h"

#include <math.h>
#include <stdlib.h>

#include "tl_layout.h"
#include "tl_measure_snap.h"
#include "tl_project.h"
#include "tl_project_io.h"

/* Комната 1202×602 со стенами 200; плитка 600×300, шов 2: без швов — 4 целые плитки. */
static TlProject *room_with_joint(TlVec2 from, TlVec2 to, double gap)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {1202, 0}, {1202, 602}, {0, 602}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_add_expansion_joint(project, tl_expansion_joint_new(from, to, gap));
    tl_project_update_spaces(project);
    return project;
}

static TlLayout *layout_of(TlProject *project) { return tl_space_layout(project->spaces.items[0]); }

static const TlTile *tile_containing(const TlLayout *layout, TlVec2 point)
{
    for (size_t i = 0; i < layout->tiles.count; i++)
        if (tl_rect_contains_point(layout->tiles.items[i].rect, point))
            return &layout->tiles.items[i];
    return NULL;
}

static double pieces_area(const TlTile *tile)
{
    double area = 0;
    for (size_t i = 0; i < tile->shapes.count; i++)
        area += tl_polygon_area(&tile->shapes.items[i]);
    return area;
}

void spec_expansion_joint(void)
{
    DESCRIBE("Деформационный шов")
    {
        IT("отрезок с зазором; полоса шва — зазор поровну по обе стороны от линии")
        {
            TlExpansionJoint *joint = tl_expansion_joint_new(tl_vec2(0, 0), tl_vec2(1000, 0), 10);
            EXPECT_NEAR(tl_expansion_joint_length(joint), 1000, 1e-9);
            TlPolygon strip = tl_expansion_joint_strip(joint);
            TlRect bounds = tl_polygon_bounds(&strip);
            EXPECT_NEAR(bounds.top, -5, 1e-9);
            EXPECT_NEAR(bounds.bottom, 5, 1e-9);
            EXPECT(bounds.left < 0 && bounds.right > 1000); /* с запасом за концы — до стены наверняка */
            tl_polygon_free(&strip);
            tl_expansion_joint_free(joint);
        }
        IT("добавляется, удаляется и сохраняется в файле вместе с зазором")
        {
            TlProject *project = room_with_joint(tl_vec2(900, 0), tl_vec2(900, 602), 8);
            tl_project_add_expansion_joint(project, tl_expansion_joint_new(tl_vec2(0, 300), tl_vec2(1202, 300), 5));
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->expansion_joints.count, 2);
            EXPECT_NEAR(loaded->expansion_joints.items[0]->gap, 8, 1e-9);
            tl_project_delete_expansion_joint(loaded, loaded->expansion_joints.items[0]);
            EXPECT_INT(loaded->expansion_joints.count, 1);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("комната знает полосы швов, которые через неё проходят")
        {
            TlProject *project = room_with_joint(tl_vec2(900, 0), tl_vec2(900, 602), 10);
            EXPECT_INT(project->spaces.items[0]->joint_strips.count, 1);
            tl_project_free(project);
        }
    }

    DESCRIBE("Шов в раскладке плитки")
    {
        IT("плитку, через которую проходит шов, режет на два куска с зазором между ними")
        {
            TlProject *project = room_with_joint(tl_vec2(900, 0), tl_vec2(900, 602), 10);
            TlLayout *layout = layout_of(project);
            EXPECT_INT(layout->whole_count, 2);
            EXPECT_INT(layout->cut_count, 2);
            const TlTile *split = tile_containing(layout, tl_vec2(700, 100));
            EXPECT(split != NULL);
            if (split) {
                EXPECT_INT(split->shapes.count, 2);
                EXPECT_NEAR(pieces_area(split), 600 * 300 - 10 * 300, 1);
            }
            tl_project_free(project);
        }
        IT("плитку, которую шов не задевает, оставляет целой")
        {
            TlProject *project = room_with_joint(tl_vec2(900, 0), tl_vec2(900, 602), 10);
            const TlTile *whole = tile_containing(layout_of(project), tl_vec2(100, 100));
            EXPECT(whole && whole->kind == TL_TILE_WHOLE);
            tl_project_free(project);
        }
        IT("СВП в полосу шва не ставятся")
        {
            /* без шва СВП стоят на четвертях длинной стороны: x = 150, 450, 752, 1052 и в вертикальном шве */
            TlProject *project = room_with_joint(tl_vec2(752, 0), tl_vec2(752, 602), 10);
            TlLayout *layout = layout_of(project);
            EXPECT_INT(layout->clip_points.count, 5);
            for (size_t i = 0; i < layout->clip_points.count; i++)
                EXPECT(fabs(layout->clip_points.items[i].x - 752) > 5);
            tl_project_free(project);
        }
        IT("изменение зазора меняет куски плитки")
        {
            TlProject *project = room_with_joint(tl_vec2(900, 0), tl_vec2(900, 602), 10);
            project->expansion_joints.items[0]->gap = 20;
            tl_project_update_spaces(project);
            const TlTile *split = tile_containing(layout_of(project), tl_vec2(700, 100));
            EXPECT(split && fabs(pieces_area(split) - (600 * 300 - 20 * 300)) < 1);
            tl_project_free(project);
        }
    }

    DESCRIBE("Притягивание концов шва")
    {
        IT("только к поверхностям стен: не к оси стены и не к швам плитки")
        {
            TlProject *project = room_with_joint(tl_vec2(900, 0), tl_vec2(900, 602), 10);
            TlSnapPoint snap = {{0, 0}, TL_SNAP_NONE, {0, 0}};
            EXPECT(tl_measure_snap(project, tl_vec2(400, -90), 100, TL_SNAP_TO_ALL, &snap));
            EXPECT_INT(snap.kind, TL_SNAP_WALL_AXIS);
            EXPECT(tl_measure_snap(project, tl_vec2(400, -90), 100, TL_SNAP_TO_WALL_SURFACES, &snap));
            EXPECT_INT(snap.kind, TL_SNAP_WALL_EDGE);
            EXPECT_NEAR(snap.point.y, 0, 1e-6);
            EXPECT(!tl_measure_snap(project, tl_vec2(601, 150), 20, TL_SNAP_TO_WALL_SURFACES, &snap));
            tl_project_free(project);
        }
    }
}

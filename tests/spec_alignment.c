#include "spec.h"
#include "specs.h"

#include "tl_align.h"
#include "tl_measure_snap.h"
#include "tl_project.h"

#define SNAP_RADIUS 100.0

static const TlVec2 AXES[] = {{1, 0}, {0, 1}};

/* Дом 4000×3000 по внутренней стороне, стены 200 мм, без плитки. */
static TlProject *empty_house(void)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    project->spaces.items[0]->tile.enabled = false;
    return project;
}

static TlSnapPoint snap(TlProject *project, double x, double y)
{
    TlSnapPoint result = {{0, 0}, TL_SNAP_NONE, {0, 0}};
    tl_measure_snap(project, tl_vec2(x, y), SNAP_RADIUS, TL_SNAP_TO_ALL, &result);
    return result;
}

void spec_alignment(void)
{
    DESCRIBE("Примагничивание к середине толщины стены")
    {
        IT("к оси наружной стены, если она ближе граней")
        {
            TlProject *project = empty_house();
            TlSnapPoint axis = snap(project, 1000, -90);
            EXPECT_INT(axis.kind, TL_SNAP_WALL_AXIS);
            EXPECT_NEAR(axis.point.x, 1000, 1e-6);
            EXPECT_NEAR(axis.point.y, -100, 1e-6);
            tl_project_free(project);
        }
        IT("к середине стены и по длине, и по толщине")
        {
            TlProject *project = empty_house();
            TlSnapPoint centre = snap(project, 1990, -95);
            EXPECT_INT(centre.kind, TL_SNAP_WALL_MIDDLE);
            EXPECT_NEAR(centre.point.x, 2000, 1e-6);
            EXPECT_NEAR(centre.point.y, -100, 1e-6);
            tl_project_free(project);
        }
        IT("к оси перегородки")
        {
            TlProject *project = empty_house();
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            TlSnapPoint axis = snap(project, 2010, 1200);
            EXPECT_INT(axis.kind, TL_SNAP_WALL_AXIS);
            EXPECT_NEAR(axis.point.x, 2000, 1e-6);
            tl_project_free(project);
        }
        IT("притяжение к линии сообщает её направление — для выравнивания по ней")
        {
            TlProject *project = empty_house();
            TlSnapPoint edge = snap(project, 1000, 30);
            EXPECT_INT(edge.kind, TL_SNAP_WALL_EDGE);
            EXPECT_NEAR(edge.direction.y, 0, 1e-9);
            EXPECT_NEAR(edge.direction.x * edge.direction.x, 1, 1e-9);
            tl_project_free(project);
        }
    }

    DESCRIBE("Автовыравнивание по горизонтали и вертикали")
    {
        IT("почти горизонтальный отрезок становится горизонтальным")
        {
            TlVec2 aligned;
            size_t which;
            EXPECT(tl_align_direction(tl_vec2(0, 0), tl_vec2(1000, 30), AXES, 2, 50, &aligned, &which));
            EXPECT_NEAR(aligned.x, 1000, 1e-9);
            EXPECT_NEAR(aligned.y, 0, 1e-9);
            EXPECT_INT(which, 0);
        }
        IT("почти вертикальный — вертикальным, в любую сторону")
        {
            TlVec2 aligned;
            size_t which;
            EXPECT(tl_align_direction(tl_vec2(0, 0), tl_vec2(-20, -900), AXES, 2, 50, &aligned, &which));
            EXPECT_NEAR(aligned.x, 0, 1e-9);
            EXPECT_NEAR(aligned.y, -900, 1e-9);
            EXPECT_INT(which, 1);
        }
        IT("заметно наклонный и слишком короткий отрезки не трогает")
        {
            TlVec2 aligned;
            size_t which;
            EXPECT(!tl_align_direction(tl_vec2(0, 0), tl_vec2(1000, 80), AXES, 2, 50, &aligned, &which));
            EXPECT(!tl_align_direction(tl_vec2(0, 0), tl_vec2(30, 3), AXES, 2, 50, &aligned, &which));
        }
        IT("точка встаёт на одну линию с опорными по X и по Y — с указанием опорных")
        {
            const TlVec2 references[] = {{500, 500}, {3000, 1200}};
            TlTracking tracking = tl_align_tracking(tl_vec2(510, 1190), references, 2, 20);
            EXPECT(tracking.has_x && tracking.has_y);
            EXPECT_NEAR(tracking.point.x, 500, 1e-9);
            EXPECT_NEAR(tracking.point.y, 1200, 1e-9);
            EXPECT_NEAR(tracking.x_reference.y, 500, 1e-9);
            EXPECT_NEAR(tracking.y_reference.x, 3000, 1e-9);
        }
        IT("вдали от опорных точек ничего не меняет")
        {
            const TlVec2 references[] = {{500, 500}};
            TlTracking tracking = tl_align_tracking(tl_vec2(800, 900), references, 1, 20);
            EXPECT(!tracking.has_x && !tracking.has_y);
            EXPECT_NEAR(tracking.point.x, 800, 1e-9);
        }
    }
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_layout.h"
#include "tl_project.h"
#include "tl_room3d.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

static bool all_finite(const TlPaintList *paints)
{
    for (size_t i = 0; i < paints->count; i++)
        for (size_t c = 0; c < paints->items[i].contours.count; c++)
            for (size_t k = 0; k < paints->items[i].contours.items[c].count; k++) {
                TlVec2 p = paints->items[i].contours.items[c].items[k];
                if (!isfinite(p.x) || !isfinite(p.y))
                    return false;
            }
    return true;
}

/* Сколько закрасок с contours контурами (у стены с проёмом — два). */
static int with_contours(const TlPaintList *paints, size_t contours)
{
    int found = 0;
    for (size_t i = 0; i < paints->count; i++)
        found += paints->items[i].contours.count == contours;
    return found;
}

void spec_room3d(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Объёмный вид комнаты (3D)")
    {
        IT("кукольный домик: пол и две дальние стены; повернули на 180° — две другие")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = project->spaces.items[0];
            space->tile.enabled = false;
            TlCamera camera = tl_room3d_default_camera(project, space, 800, 600);
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT_INT(paints.count, 3);
            EXPECT(all_finite(&paints));
            tl_paint_list_free_all(&paints);
            camera.yaw += 180;
            paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT_INT(paints.count, 3);
            tl_paint_list_free_all(&paints);
            tl_project_free(project);
        }
        IT("комната целиком в кадре; плитка пола — по закраске на кусок, со швом темнее плитки")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = project->spaces.items[0];
            TlCamera camera = {.yaw = 235, .pitch = 38, .fov = 45}; /* distance = 0 — подобрать */
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT(camera.distance > 0);
            int tiles = 0;
            bool inside = true;
            for (size_t i = 0; i < paints.count; i++) {
                const TlPaint *paint = &paints.items[i];
                for (size_t k = 0; k < paint->contours.items[0].count; k++) {
                    TlVec2 p = paint->contours.items[0].items[k];
                    inside = inside && p.x > -1 && p.x < 801 && p.y > -1 && p.y < 601;
                }
                if (paint->stroke[0] && paint->stroke_width < 1) {
                    tiles++;
                    unsigned fill, stroke;
                    sscanf(paint->fill + 1, "%x", &fill);
                    sscanf(paint->stroke + 1, "%x", &stroke);
                    EXPECT(((stroke >> 16) & 255) < ((fill >> 16) & 255));
                }
            }
            EXPECT(inside);
            EXPECT_INT(tiles, tl_space_tile_total(space));
            tl_paint_list_free_all(&paints);
            tl_project_free(project);
        }
        IT("окно: в стене дыра, видны откосы и стекло; плитку, закрытую проёмом, не рисуют")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = project->spaces.items[0];
            space->tile.enabled = false;
            TlOpening *window = tl_opening_new(tl_vec2(2000, -100), 1200);
            tl_opening_set_kind(window, TL_OPENING_WINDOW);
            tl_project_add_opening(project, window);
            tl_project_update_spaces(project);
            TlCamera camera = tl_room3d_default_camera(project, space, 800, 600);
            camera.yaw = 270; /* с юга: верхняя стена плана — дальняя */
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT_INT(with_contours(&paints, 2), 1);
            EXPECT(paints.count >= 1 + 1 + 1 + 4); /* пол, стена, стекло, откосы (видимые) */
            tl_paint_list_free_all(&paints);
            space->walls.enabled = true;
            tl_project_update_spaces(project);
            paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT(all_finite(&paints));
            tl_paint_list_free_all(&paints);
            tl_project_free(project);
        }
        IT("камера внутри комнаты: всё, что за спиной, отсечено, координаты конечные")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = project->spaces.items[0];
            TlFixture stairs = tl_fixture_default(TL_FIXTURE_STAIRS, tl_vec2(2000, 1500));
            tl_fixture_list_push(&project->fixtures, stairs);
            tl_fixture_list_push(&project->fixtures, tl_fixture_default(TL_FIXTURE_BATH, tl_vec2(900, 400)));
            tl_project_update_spaces(project);
            TlCamera camera = tl_room3d_default_camera(project, space, 800, 600);
            camera.distance = 600;
            camera.pitch = 10;
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT(paints.count > 0);
            EXPECT(all_finite(&paints));
            tl_paint_list_free_all(&paints);
            tl_project_free(project);
        }
    }
}

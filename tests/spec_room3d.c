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

/* Тумба (её цвет — «#d8c8ad» со светом): у неё красный заметно больше синего. */
static bool cabinet_paint(const TlPaint *paint)
{
    unsigned value;
    sscanf(paint->fill + 1, "%x", &value);
    return (int)((value >> 16) & 255) - (int)(value & 255) > 24;
}

static bool point_in(const TlPolygonList *contours, TlVec2 p)
{
    bool inside = false;
    for (size_t c = 0; c < contours->count; c++) {
        const TlPolygon *poly = &contours->items[c];
        for (size_t i = 0, j = poly->count - 1; i < poly->count; j = i++) {
            TlVec2 a = poly->items[i], b = poly->items[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
    }
    return inside;
}

/* Сколько граней тумбы видно хотя бы серединой (сверху в этой точке — сама тумба). */
static int visible_cabinet_faces(const TlPaintList *paints)
{
    int visible = 0;
    for (size_t i = 0; i < paints->count; i++) {
        if (!cabinet_paint(&paints->items[i]))
            continue;
        const TlPolygon *outline = &paints->items[i].contours.items[0];
        TlVec2 middle = {0, 0};
        for (size_t k = 0; k < outline->count; k++)
            middle = tl_vec2_add(middle, tl_vec2_scale(outline->items[k], 1.0 / outline->count));
        size_t top = i;
        for (size_t j = i + 1; j < paints->count; j++)
            if (point_in(&paints->items[j].contours, middle))
                top = j;
        visible += cabinet_paint(&paints->items[top]);
    }
    return visible;
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
        IT("Г-образная комната: тумбу в дальнем колене закрывает стена внутреннего угла")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {2000, 0}, {2000, 2000}, {4000, 2000}, {4000, 4000}, {0, 4000}};
            tl_project_add_outline(project, tl_outline_new(corners, 6, 200));
            TlFixture cabinet = tl_fixture_default(TL_FIXTURE_CABINET, tl_vec2(1000, 1000));
            cabinet.height = 1400;
            tl_fixture_list_push(&project->fixtures, cabinet);
            tl_project_update_spaces(project);
            TlSpace *space = project->spaces.items[0];
            space->tile.enabled = false;
            tl_project_update_spaces(project);
            TlCamera camera = tl_room3d_default_camera(project, space, 800, 600);
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            EXPECT(visible_cabinet_faces(&paints) > 0); /* по умолчанию — видна */
            tl_paint_list_free_all(&paints);
            camera.yaw = 340; /* со стороны внутреннего угла, пониже */
            camera.pitch = 27;
            paints = tl_room3d_paint(project, space, &camera, 800, 600);
            int cabinet_faces = 0;
            for (size_t i = 0; i < paints.count; i++)
                cabinet_faces += cabinet_paint(&paints.items[i]);
            EXPECT(cabinet_faces > 0);                        /* грани тумбы есть… */
            EXPECT_INT(visible_cabinet_faces(&paints), 0);    /* …но их закрывает стена */
            tl_paint_list_free_all(&paints);
            tl_project_free(project);
        }
        IT("отверстия под трубы — на полу и на стенах; сдвиг комнаты; рисунок плитки — клетками")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = project->spaces.items[0];
            space->tile.enabled = false;
            space->walls.enabled = true;
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(2000, 1500), 110));
            tl_wall_pipe_list_push(&space->wall_pipes, (TlWallPipe){tl_space_wall_at(space, 0)->edge, {800, 1000}, 60});
            tl_project_update_spaces(project);
            TlCamera camera = tl_room3d_default_camera(project, space, 800, 600);
            camera.yaw = 270; /* с юга: стена 1 (верх плана) — дальняя, видна */
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            int holes = 0;
            for (size_t i = 0; i < paints.count; i++)
                holes += strcmp(paints.items[i].fill, "#000000") != 0 && paints.items[i].contours.items[0].count == 28;
            EXPECT(holes >= 1);
            tl_paint_list_free_all(&paints);
            TlVec3 before = camera.target;
            tl_room3d_pan(&camera, 100, 0, 600); /* комнату — вправо: точка, куда смотрим, — влево */
            EXPECT(fabs(camera.target.z - before.z) < 1e-6);
            EXPECT(tl_distance(tl_vec2(camera.target.x, camera.target.y), tl_vec2(before.x, before.y)) > 10);
            snprintf(space->tile.texture, sizeof space->tile.texture, "samples/kg-60x120-travertine");
            space->tile.enabled = true;
            space->tile.tile_width = 1200;
            space->tile.tile_height = 600;
            tl_project_update_spaces(project);
            camera = tl_room3d_default_camera(project, space, 800, 600);
            camera.distance /= 3; /* близко — плитка крупная, рисунок по нескольким клеткам */
            paints = tl_room3d_paint(project, space, &camera, 800, 600);
            size_t most = 0;
            for (size_t i = 0; i < paints.count; i++)
                if (paints.items[i].texture[0] && paints.items[i].cell_count > most)
                    most = paints.items[i].cell_count;
            EXPECT(most > 1);
            tl_paint_list_free_all(&paints);
            tl_project_free(project);
        }
        IT("плитка, разрезанная деформационным швом, — обоими кусками")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = project->spaces.items[0];
            tl_project_add_expansion_joint(project, tl_expansion_joint_new(tl_vec2(1950, 0), tl_vec2(1950, 3000), 8));
            tl_project_update_spaces(project);
            TlCamera camera = tl_room3d_default_camera(project, space, 800, 600);
            camera.pitch = 89; /* сверху — пол целиком */
            TlPaintList paints = tl_room3d_paint(project, space, &camera, 800, 600);
            int tiles = 0, split = 0;
            for (size_t i = 0; i < paints.count; i++)
                if (paints.items[i].stroke[0] && paints.items[i].stroke_width < 1) {
                    tiles++;
                    split += paints.items[i].contours.count == 2;
                }
            EXPECT_INT(tiles, tl_space_tile_total(space));
            EXPECT(split > 0);
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

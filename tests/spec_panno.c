#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_estimate.h"
#include "tl_layout.h"
#include "tl_project.h"
#include "tl_project_io.h"
#include "tl_report.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

static TlSpace *only_space(TlProject *project) { return project->spaces.items[0]; }

static double walls_area(TlSpace *space)
{
    double area = 0;
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++)
        area += tl_space_wall_layout_at(space, i, NULL, NULL)->floor_area;
    return area;
}

void spec_panno(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Панно: картинка из нескольких плиток на стене и на полу")
    {
        IT("панно 2×2 плитки 300×600 — 602×1202 мм, кладётся целиком: четыре целые плитки")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            TlPanno panno = tl_panno_default(tl_space_wall_at(space, 1)->edge);
            EXPECT(fabs(tl_panno_width(&panno) - 602) < 1e-9 && fabs(tl_panno_height(&panno) - 1202) < 1e-9);
            tl_panno_list_push(&space->pannos, panno);
            TlLayout *layout = tl_space_panno_layout(space, 0);
            EXPECT(layout != NULL);
            EXPECT_INT(layout->whole_count, 4);
            EXPECT_INT(layout->cut_count + layout->narrow_count, 0);
            tl_layout_free(layout);
            char name[160];
            tl_panno_name(&panno, name, sizeof name);
            EXPECT_STR(name, "Панно 602×1202 мм (2×2 плитки 300×600)");
            tl_project_free(project);
        }
        IT("плитка стены обходит панно со своим швом; в расчёте площадь стен — та же")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            double before = walls_area(space);
            tl_panno_list_push(&space->pannos, tl_panno_default(tl_space_wall_at(space, 1)->edge));
            double gap = tl_space_wall_tile(space, TL_WALL_MAIN)->joint_width;
            double cut = (602 + 2 * gap) * (1202 + 2 * gap);
            EXPECT(fabs(before - walls_area(space) - cut) < 1);
            TlSummary summary = tl_report_summarize(project);
            EXPECT(fabs(summary.wall_area - (before - cut + 602 * 1202)) < 1); /* плитка вокруг и панно */
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("панно на полу — в центре комнаты со сдвигом; основной пол его обходит")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            double before = tl_space_layout(space)->floor_area;
            TlPanno panno = tl_panno_default(TL_PANNO_FLOOR);
            panno.at = tl_vec2(300, 0);
            tl_panno_list_push(&space->pannos, panno);
            TlPolygon outline = tl_space_panno_outline(space, 0);
            TlRect bounds = tl_polygon_bounds(&outline);
            EXPECT(fabs((bounds.left + bounds.right) / 2 - 1800) < 1e-6);
            EXPECT(fabs((bounds.top + bounds.bottom) / 2 - 1000) < 1e-6);
            tl_polygon_free(&outline);
            double gap = tl_space_effective_tile(space)->joint_width;
            EXPECT(fabs(before - tl_space_layout(space)->floor_area - (602 + 2 * gap) * (1202 + 2 * gap)) < 1);
            tl_project_free(project);
        }
        IT("в смете — панно комплектом по своей цене, одинаковые — одной строкой")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            TlPanno panno = tl_panno_default(tl_space_wall_at(space, 1)->edge);
            panno.price = 12000;
            tl_panno_list_push(&space->pannos, panno);
            panno.wall = tl_space_wall_at(space, 3)->edge;
            tl_panno_list_push(&space->pannos, panno);
            TlSummary summary = tl_report_summarize(project);
            TlEstimate estimate = tl_estimate_from_summary(&summary);
            int lines = 0;
            for (size_t i = 0; i < estimate.lines.count; i++)
                if (strncmp(estimate.lines.items[i].name, "Панно", strlen("Панно")) == 0) {
                    lines++;
                    EXPECT(fabs(estimate.lines.items[i].quantity - 2) < 1e-9);
                    EXPECT(fabs(estimate.lines.items[i].cost - 24000) < 1e-9);
                }
            EXPECT_INT(lines, 1);
            tl_estimate_free(&estimate);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("панно сохраняются в файле проекта")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            TlPanno panno = tl_panno_default(2);
            panno.columns = 3;
            panno.price = 9900;
            snprintf(panno.texture, sizeof panno.texture, "%s", "Мой каталог/Панно/Море");
            tl_panno_list_push(&space->pannos, panno);
            tl_panno_list_push(&space->pannos, tl_panno_default(TL_PANNO_FLOOR));
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            TlSpace *copy = only_space(loaded);
            EXPECT_INT(copy->pannos.count, 2);
            EXPECT_INT(copy->pannos.items[0].columns, 3);
            EXPECT_INT((long)copy->pannos.items[0].wall, 2);
            EXPECT_STR(copy->pannos.items[0].texture, "Мой каталог/Панно/Море");
            EXPECT(copy->pannos.items[1].wall == TL_PANNO_FLOOR);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

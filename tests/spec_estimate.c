#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_estimate.h"
#include "tl_layout.h"
#include "tl_project.h"
#include "tl_project_io.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

static TlSpace *only_space(TlProject *project) { return project->spaces.items[0]; }

static const TlEstimateLine *line_named(const TlEstimate *estimate, const char *name)
{
    for (size_t i = 0; i < estimate->lines.count; i++)
        if (strcmp(estimate->lines.items[i].name, name) == 0)
            return &estimate->lines.items[i];
    return NULL;
}

void spec_estimate(void)
{
    tl_tile_settings_reset_defaults(); /* другие спецификации запоминают свои настройки для новых комнат */
    DESCRIBE("Затирка, грунт и смета")
    {
        IT("затирка: 4 плитки 600×300, шов 2, толщина 9, плотность 1,6 — 3600 мм швов, 0,104 кг")
        {
            TlProject *project = room_project(1202, 602);
            TlSpace *space = only_space(project);
            TlLayout *layout = tl_space_layout(space);
            EXPECT_NEAR(tl_layout_joint_length(layout), 3600, 0.01);
            EXPECT_NEAR(tl_layout_grout_kg(space, layout), 3600 * 2 * 9 * 1.6e-6, 1e-9);
            tl_project_free(project);
        }
        IT("затирка на м² совпадает с формулой производителей (A + B) / (A·B) · шов · глубина · плотность")
        {
            TlProject *project = room_project(3002, 3002); /* 10 × 10 плиток 300×300 со швом 2 */
            TlSpace *space = only_space(project);
            space->tile.tile_width = space->tile.tile_height = 300;
            TlLayout *layout = tl_space_layout(space);
            double per_m2 = tl_layout_grout_kg(space, layout) / (layout->floor_area / 1e6);
            double formula = (300.0 + 300) / (300.0 * 300) * 2 * 9 * 1.6;
            EXPECT_NEAR(per_m2, formula, formula * 0.01);
            tl_project_free(project);
        }
        IT("грунт: площадь под плитку × расход; в сводке — канистры")
        {
            TlProject *project = room_project(5000, 4000);
            TlSpace *space = only_space(project);
            EXPECT_NEAR(tl_layout_primer_l(space, tl_space_layout(space)), 20 * 0.2, 1e-9);
            TlSummary summary = tl_report_summarize(project);
            EXPECT_NEAR(summary.primer_total_l, 4, 1e-9);
            EXPECT_INT(summary.primer.count, 1);
            if (summary.primer.count == 1)
                EXPECT_INT(summary.primer.items[0].units, 1); /* 4 л из канистры 10 л */
            EXPECT_INT(summary.grout.count, 1);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("смета: плитка за м² к покупке с запасом, клей за мешок, укладка за м²; без цены — не в смете")
        {
            TlProject *project = room_project(1202, 602);
            TlTileSettings *tile = &only_space(project)->tile;
            tile->price_tile_m2 = 1000;
            tile->price_glue_bag = 500;
            tile->price_work_m2 = 800;
            TlSummary summary = tl_report_summarize(project);
            TlEstimate estimate = tl_estimate_from_summary(&summary);
            EXPECT_INT(estimate.lines.count, 3);
            const TlEstimateLine *tiles = line_named(&estimate, "Плитка 600×300 мм");
            EXPECT(tiles != NULL);
            if (tiles)
                EXPECT_NEAR(tiles->cost, 5 * 0.18 * 1000, 1e-6); /* 4 + 10 % → 5 плиток */
            const TlEstimateLine *glue = line_named(&estimate, "Клей, мешок 25 кг");
            EXPECT(glue != NULL);
            if (glue)
                EXPECT_NEAR(glue->cost, 500, 1e-9);
            EXPECT_NEAR(estimate.work, 1.202 * 0.602 * 800, 1e-6);
            EXPECT_NEAR(estimate.total, 900 + 500 + 1.202 * 0.602 * 800, 1e-6);
            tl_estimate_free(&estimate);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("рубли — с пробелами между разрядами, копейки — только если есть")
        {
            char text[64];
            tl_format_rubles(12345, text, sizeof text);
            EXPECT_STR(text, "12\xe2\x80\xaf" "345 ₽");
            tl_format_rubles(578.88, text, sizeof text);
            EXPECT_STR(text, "578,88 ₽");
            tl_format_rubles(1234567.5, text, sizeof text);
            EXPECT_STR(text, "1\xe2\x80\xaf" "234\xe2\x80\xaf" "567,50 ₽");
        }
        IT("цены и параметры затирки сохраняются в файле проекта")
        {
            TlProject *project = room_project(1202, 602);
            only_space(project)->tile.price_work_m2 = 950;
            only_space(project)->tile.tile_thickness = 11;
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_NEAR(only_space(loaded)->tile.price_work_m2, 950, 0);
            EXPECT_NEAR(only_space(loaded)->tile.tile_thickness, 11, 0);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

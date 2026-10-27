#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

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

static void set_pattern(TlTileSettings *tile, const char *id) { snprintf(tile->pattern, sizeof tile->pattern, "%s", id); }

/* Плитки рисунка с полушвом вокруг должны покрыть плоскость ровно один раз. */
typedef struct Coverage {
    TlVec2 point;
    double half_gap;
    int hits;
} Coverage;

static void count_hit(const TlPlacedTile *tile, void *context)
{
    Coverage *coverage = context;
    if (tile->shape_count) { /* многоугольник — проверяется без швов (half_gap = 0) */
        TlPolygon shape = tl_polygon_from_points(tile->shape, tile->shape_count);
        coverage->hits += tl_polygon_contains(&shape, coverage->point);
        tl_polygon_free(&shape);
        return;
    }
    TlRect r = tile->rect;
    double g = coverage->half_gap;
    if (coverage->point.x >= r.left - g && coverage->point.x < r.right + g && coverage->point.y >= r.top - g &&
        coverage->point.y < r.bottom + g)
        coverage->hits++;
}

static bool covers_once(const TlTileSettings *tile)
{
    TlPatternUnit unit;
    if (!tl_pattern_unit(tile, &unit))
        return false;
    for (int i = 0; i < 23; i++)
        for (int j = 0; j < 19; j++) {
            /* точки не на швах: шаги несоизмеримы с размерами плитки */
            Coverage coverage = {{100.37 + i * 97.13, 50.11 + j * 103.71}, tile->joint_width / 2, 0};
            TlRect probe = {coverage.point.x - 1, coverage.point.y - 1, coverage.point.x + 1, coverage.point.y + 1};
            tl_pattern_cover(&unit, tl_vec2(13, 7), probe, count_hit, &coverage);
            if (coverage.hits != 1)
                return false;
        }
    return true;
}

static const TlTileTypeSummary *type_named(const TlSummary *summary, const char *name)
{
    for (size_t i = 0; i < summary->types.count; i++) {
        char text[64];
        tl_tile_spec_name(&summary->types.items[i].spec, text, sizeof text);
        if (strcmp(text, name) == 0)
            return &summary->types.items[i];
    }
    return NULL;
}

void spec_pattern(void)
{
    tl_tile_settings_reset_defaults(); /* другие спецификации запоминают свои настройки для новых комнат */
    DESCRIBE("Рисунки укладки")
    {
        IT("каждый рисунок покрывает пол без щелей и наложений — у каждого шва ровно шов")
        {
            size_t count;
            const TlPatternInfo *patterns = tl_patterns(&count);
            EXPECT(count >= 6);
            for (size_t i = 0; i < count; i++) {
                TlTileSettings tile = tl_tile_settings_factory();
                set_pattern(&tile, patterns[i].id);
                tile.joint_width = 3;
                tile.row_shift = 1.0 / 3;
                bool polygons = strcmp(patterns[i].id, "chevron") == 0 || strcmp(patterns[i].id, "insert") == 0;
                if (polygons) /* у многоугольников покрытие проверяется без швов */
                    tile.joint_width = 0;
                if (strcmp(patterns[i].id, "insert") == 0)
                    tile.tile_width = tile.tile_height = 330;
                if (strcmp(patterns[i].id, "basket") == 0)
                    tile.tile_height = 200; /* 600×200 — три в квадрате */
                if (patterns[i].modular)
                    tile.tile_width = tile.tile_height = 200;
                bool ok = covers_once(&tile);
                if (!ok)
                    printf("      рисунок «%s» покрывает не ровно один раз\n", patterns[i].name);
                EXPECT(ok);
            }
        }
        IT("ёлочка верна и для плитки поперёк (300×600) и квадратной")
        {
            TlTileSettings tile = tl_tile_settings_factory();
            set_pattern(&tile, "herringbone");
            tile.tile_width = 300;
            tile.tile_height = 600;
            EXPECT(covers_once(&tile));
            tile.tile_width = tile.tile_height = 400;
            EXPECT(covers_once(&tile));
        }
        IT("шеврон, вставка и корзинка — швы между плитками тоже ровно шов (точки посреди швов не заняты)")
        {
            const char *ids[] = {"chevron", "insert", "basket"};
            for (size_t i = 0; i < 3; i++) {
                TlTileSettings tile = tl_tile_settings_factory();
                set_pattern(&tile, ids[i]);
                tile.joint_width = 3;
                if (i == 1)
                    tile.tile_width = tile.tile_height = 330;
                if (i == 2)
                    tile.tile_height = 200;
                TlPatternUnit unit;
                EXPECT(tl_pattern_unit(&tile, &unit));
                /* площадь плиток модуля меньше площади ячейки решётки ровно на швы — плитки не
                   налезают друг на друга: сумма площадей < |a × b| */
                double area = 0;
                for (size_t k = 0; k < unit.tile_count; k++) {
                    const TlPatternTile *t = &unit.tiles[k];
                    TlPolygon p = t->shape_count ? tl_polygon_from_points(t->shape, t->shape_count)
                                                 : tl_polygon_from_rect(t->rect);
                    area += tl_polygon_area(&p);
                    tl_polygon_free(&p);
                }
                double cell = fabs(unit.a.x * unit.b.y - unit.a.y * unit.b.x);
                EXPECT(area < cell && area > cell * 0.9);
            }
        }
        IT("шеврон: к покупке — левые и правые плитки отдельно; остатки подрезок не парятся (плитка не прямоугольная)")
        {
            TlProject *project = room_project(3000, 2000);
            TlTileSettings *tile = &only_space(project)->tile;
            set_pattern(tile, "chevron");
            tile->tile_width = 600;
            tile->tile_height = 100;
            TlLayout *layout = tl_space_layout(only_space(project));
            EXPECT(layout->tiles.count > 50);
            EXPECT(layout->whole_count > 0 && layout->cut_count > 0);
            EXPECT(layout->tiles.items[0].outline.count == 4);
            EXPECT(layout->clip_points.count > 0);
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 2); /* левые и правые — разные позиции */
            EXPECT(type_named(&summary, "600×100 мм, левая") != NULL);
            EXPECT(type_named(&summary, "600×100 мм, правая") != NULL);
            for (size_t i = 0; i < summary.types.count; i++) {
                EXPECT_INT(summary.types.items[i].offcut_saving, 0);
                EXPECT_NEAR(tl_tile_spec_area_m2(&summary.types.items[i].spec), 0.06, 1e-9);
            }
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("со вставкой: плитка 330×330 и кабошон 70×70 — два вида к покупке, кабошонов столько же, сколько плиток")
        {
            TlProject *project = room_project(3302, 3302); /* 10 × 10 плиток со швом 0 */
            TlTileSettings *tile = &only_space(project)->tile;
            set_pattern(tile, "insert");
            tile->tile_width = tile->tile_height = 330;
            tile->joint_width = 0;
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 2);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("неизвестный рисунок — прямая; пустой — тоже")
        {
            TlTileSettings tile = tl_tile_settings_factory();
            set_pattern(&tile, "spiral");
            EXPECT_STR(tl_pattern_of(&tile)->id, "straight");
            set_pattern(&tile, "");
            EXPECT_STR(tl_pattern_of(&tile)->id, "straight");
        }
        IT("шахматка: 1202×602, плитка 600×300 — по две плитки каждого цвета, к покупке — два вида")
        {
            TlProject *project = room_project(1202, 602);
            set_pattern(&only_space(project)->tile, "checker");
            TlLayout *layout = tl_space_layout(only_space(project));
            int second = 0;
            for (size_t i = 0; i < layout->tiles.count; i++)
                second += layout->tiles.items[i].spec == 1;
            EXPECT_INT(layout->whole_count, 4);
            EXPECT_INT(second, 2);
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 2);
            if (summary.types.count == 2) {
                EXPECT_INT(summary.types.items[0].count, 2);
                EXPECT_INT(summary.types.items[1].count, 2);
                EXPECT(strcmp(summary.types.items[0].spec.color, summary.types.items[1].spec.color) != 0);
            }
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("ёлочка: половина плиток лежит поперёк; к покупке — один вид")
        {
            TlProject *project = room_project(3000, 3000);
            set_pattern(&only_space(project)->tile, "herringbone");
            TlLayout *layout = tl_space_layout(only_space(project));
            int turned = 0;
            for (size_t i = 0; i < layout->tiles.count; i++)
                turned += layout->tiles.items[i].turned;
            EXPECT(layout->tiles.count > 40);
            EXPECT(abs(2 * turned - (int)layout->tiles.count) <= (int)layout->tiles.count / 10);
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 1);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("модульная из трёх форматов на клетке 300×300: к покупке 600×600, 600×300 и 300×300")
        {
            TlProject *project = room_project(3000, 2400);
            TlTileSettings *tile = &only_space(project)->tile;
            set_pattern(tile, "modular3");
            tile->tile_width = tile->tile_height = 300;
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 3);
            EXPECT(type_named(&summary, "600×600 мм") != NULL);
            EXPECT(type_named(&summary, "600×300 мм") != NULL);
            EXPECT(type_named(&summary, "300×300 мм") != NULL);
            /* площадь пола делится между видами — вместе вся */
            double area = 0;
            for (size_t i = 0; i < summary.types.count; i++)
                area += summary.types.items[i].area;
            EXPECT_NEAR(area, summary.tiled_area, 1);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("версаль на клетке 200×200: четыре формата — 600×400, 400×400, 400×200, 200×200")
        {
            TlProject *project = room_project(4000, 3000);
            TlTileSettings *tile = &only_space(project)->tile;
            set_pattern(tile, "versailles");
            tile->tile_width = tile->tile_height = 200;
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 4);
            EXPECT(type_named(&summary, "600×400 мм") != NULL);
            EXPECT(type_named(&summary, "400×400 мм") != NULL);
            EXPECT(type_named(&summary, "400×200 мм") != NULL);
            EXPECT(type_named(&summary, "200×200 мм") != NULL);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("смещение приводится к одному модулю — раскладка та же")
        {
            TlProject *project = room_project(2500, 1800);
            TlTileSettings *tile = &only_space(project)->tile;
            set_pattern(tile, "herringbone");
            tile->offset_x = 5123;
            tile->offset_y = -2777;
            int before = tl_layout_total(tl_space_layout(only_space(project)));
            tl_layout_normalize_offsets(tile, &tile->offset_x, &tile->offset_y);
            EXPECT(fabs(tile->offset_x) < 2000 && fabs(tile->offset_y) < 2000);
            EXPECT_INT(tl_layout_total(tl_space_layout(only_space(project))), before);
            tl_project_free(project);
        }
        IT("«Минимум плиток» перебирает смещения и для ёлочки")
        {
            TlProject *project = room_project(2100, 1700);
            set_pattern(&only_space(project)->tile, "herringbone");
            TlTileCountResult result;
            EXPECT(tl_report_optimize_count(project, only_space(project), &result));
            EXPECT(result.after.tiles <= result.before.tiles);
            tl_project_free(project);
        }
        IT("рисунок и цвет второй плитки сохраняются в файле проекта")
        {
            TlProject *project = room_project(1202, 602);
            set_pattern(&only_space(project)->tile, "checker");
            snprintf(only_space(project)->tile.color2, TL_COLOR_LENGTH, "%s", "#112233");
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_STR(only_space(loaded)->tile.pattern, "checker");
            EXPECT_STR(only_space(loaded)->tile.color2, "#112233");
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }
}

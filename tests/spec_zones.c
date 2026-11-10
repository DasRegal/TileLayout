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

static double layouts_area(TlSpace *space)
{
    double area = 0;
    for (size_t i = 0; i < tl_space_layout_count(space); i++)
        area += tl_space_layout_at(space, i)->floor_area;
    return area;
}

void spec_zones(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Зоны пола: бордюр и ковёр")
    {
        IT("бордюр — по раскладке на каждую стену, первый ряд — от стены; пол делится без потерь")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            double room = tl_space_area(space);
            space->zones[TL_ZONE_BORDER].enabled = true;
            EXPECT_INT(tl_space_layout_count(space), 5); /* основной пол и 4 стены */
            EXPECT_INT(tl_space_layout_zone(space, 1), TL_ZONE_BORDER);
            /* пол комнаты — основной + бордюр, без полоски шва между ними (≈ периметр × 1 мм) */
            EXPECT(fabs(layouts_area(space) - room) < 2 * 10000 * 1.0 + 1);
            for (size_t i = 1; i < 5; i++) {
                TlLayout *strip = tl_space_layout_at(space, i);
                EXPECT(strip->whole_count > 0);
                bool from_wall = false; /* у стены — ряд, начинающийся от неё (y = 0 в осях полосы) */
                for (size_t t = 0; t < strip->tiles.count; t++)
                    from_wall |= fabs(strip->tiles.items[t].rect.top) < 0.01;
                EXPECT(from_wall);
            }
            /* основной пол — без бордюра: плиток меньше, чем во всей комнате */
            int with_border = tl_layout_total(tl_space_layout(space));
            space->zones[TL_ZONE_BORDER].enabled = false;
            EXPECT(with_border < tl_layout_total(tl_space_layout(space)));
            EXPECT_INT(tl_space_layout_count(space), 1);
            tl_project_free(project);
        }
        IT("бордюр — своя позиция к покупке: свой цвет, клей и затирка — как у комнаты")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->zones[TL_ZONE_BORDER].enabled = true;
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 2);
            bool border_color = false;
            for (size_t i = 0; i < summary.types.count; i++)
                border_color |= strcmp(summary.types.items[i].spec.color, "#6b5a4a") == 0;
            EXPECT(border_color);
            EXPECT_NEAR(summary.tiled_area, layouts_area(space), 1);
            EXPECT(summary.grout_total_kg > 0);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("ковёр в центре — своя раскладка и рисунок; основной пол его обходит")
        {
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = only_space(project);
            space->zones[TL_ZONE_CARPET].enabled = true;
            EXPECT_INT(tl_space_layout_count(space), 2);
            TlLayout *carpet = tl_space_layout_at(space, 1);
            EXPECT(tl_layout_total(carpet) >= 16);
            EXPECT_INT(carpet->pattern.spec_count, 2); /* шахматка */
            TlLayout *main = tl_space_layout(space);
            TlAffine to_local = tl_layout_world_to_local(space);
            TlVec2 center = tl_affine_apply(&to_local, tl_vec2(2000, 1500));
            bool covered = false;
            for (size_t t = 0; t < main->tiles.count; t++)
                covered |= tl_rect_contains_point(main->tiles.items[t].rect, center) &&
                           main->tiles.items[t].kind == TL_TILE_WHOLE;
            EXPECT(!covered);
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 3); /* пол и две плитки шахматки ковра */
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("бордюр со шахматкой: цены и запас — свои у зоны, остальное — от комнаты")
        {
            TlZone zone = tl_zone_factory(TL_ZONE_BORDER);
            zone.tile.price_tile_m2 = 3000;
            zone.tile.glue_bag_kg = 5;
            TlTileSettings room = tl_tile_settings_factory();
            room.price_tile_m2 = 1000;
            TlTileSettings tile = tl_zone_tile(&zone, &room);
            EXPECT_NEAR(tile.price_tile_m2, 3000, 0);
            EXPECT_NEAR(tile.glue_bag_kg, room.glue_bag_kg, 0);
            EXPECT_NEAR(tile.tile_width, 300, 0);
        }
        IT("зоны сохраняются в файле проекта")
        {
            TlProject *project = room_project(3000, 2000);
            TlZone *carpet = &only_space(project)->zones[TL_ZONE_CARPET];
            carpet->enabled = true;
            carpet->width = 900;
            carpet->dx = 150;
            snprintf(carpet->tile.color, TL_COLOR_LENGTH, "%s", "#123456");
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[64];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            const TlZone *read = &only_space(loaded)->zones[TL_ZONE_CARPET];
            EXPECT(read->enabled);
            EXPECT_NEAR(read->width, 900, 0);
            EXPECT_NEAR(read->dx, 150, 0);
            EXPECT_STR(read->tile.color, "#123456");
            EXPECT(!only_space(loaded)->zones[TL_ZONE_BORDER].enabled);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("чертежи подрезок зон подписаны: «Комната · бордюр»")
        {
            TlProject *project = room_project(3010, 2010);
            TlSpace *space = only_space(project);
            tl_space_set_name(space, "Холл");
            space->zones[TL_ZONE_BORDER].enabled = true;
            TlCutCardList cards = tl_report_cut_cards(project);
            bool named = false;
            for (size_t i = 0; i < cards.count; i++)
                named |= strstr(cards.items[i].title, "Холл · бордюр") != NULL;
            EXPECT(named);
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
        IT("по целой плитке: ковёр 1802 при плитке 300 и шве 2 — 1810 (6 плиток), бордюр — целыми рядами")
        {
            EXPECT_NEAR(tl_zone_fit_size(1802, 300, 2), 1810, 1e-9);
            EXPECT_NEAR(tl_zone_fit_size(100, 300, 2), 300, 1e-9); /* не меньше одной плитки */
            TlZone carpet = tl_zone_factory(TL_ZONE_CARPET);
            carpet.width = 1802;
            carpet.height = 1190;
            TlZone laid = tl_zone_effective(&carpet, TL_ZONE_CARPET);
            EXPECT_NEAR(laid.width, 1810, 1e-9);
            EXPECT_NEAR(laid.height, 1206, 1e-9);
            carpet.fit = false;
            EXPECT_NEAR(tl_zone_effective(&carpet, TL_ZONE_CARPET).width, 1802, 0);
            TlZone border = tl_zone_factory(TL_ZONE_BORDER);
            border.tile.tile_height = 100;
            border.tile.wall_gap = 5;
            border.width = 230;
            EXPECT_NEAR(tl_zone_effective(&border, TL_ZONE_BORDER).width, 202 + 5, 1e-9); /* 2 ряда и отступ */
            /* ковёр по целой плитке — без подрезок */
            TlProject *project = room_project(4000, 3000);
            TlSpace *space = only_space(project);
            space->zones[TL_ZONE_CARPET].enabled = true;
            space->zones[TL_ZONE_CARPET].width = 1802;
            TlLayout *rug = tl_space_layout_at(space, 1);
            EXPECT_INT(rug->cut_count + rug->narrow_count, 0);
            EXPECT_INT(rug->whole_count, 24);
            tl_project_free(project);
        }
        IT("у двери бордюр прерывается: через проход кладётся основная плитка")
        {
            TlProject *project = tl_project_demo();
            TlSpace *bath = project->spaces.items[1]; /* дверь — в перегородке слева */
            bath->zones[TL_ZONE_BORDER].enabled = true;
            size_t strips = 0;
            for (size_t l = 1; l < tl_space_layout_count(bath); l++)
                strips += tl_space_layout_zone(bath, l) == TL_ZONE_BORDER;
            EXPECT_INT(strips, 5); /* 4 стены, левая — двумя кусками по сторонам двери */
            /* середина двери на краю комнаты — пол основной плиткой, не бордюр */
            TlVec2 from, to;
            EXPECT(tl_project_space_door_edge(project, bath, project->openings.items[0], &from, &to));
            TlVec2 probe = tl_vec2_add(tl_vec2_midpoint(from, to), tl_vec2(150, 0));
            bool main_there = false, border_there = false;
            for (size_t l = 0; l < tl_space_layout_count(bath); l++) {
                TlLayout *layout = tl_space_layout_at(bath, l);
                TlAffine to_local = tl_affine_inverted(&layout->local_to_world);
                TlVec2 local = tl_affine_apply(&to_local, probe);
                for (size_t t = 0; t < layout->tiles.count; t++)
                    if (tl_rect_contains_point(layout->tiles.items[t].rect, local)) {
                        if (l == 0)
                            main_there = true;
                        else
                            border_there = true;
                    }
            }
            EXPECT(main_there);
            EXPECT(!border_there);
            tl_project_free(project);
        }
    }
}

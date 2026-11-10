#include <math.h>
#include <stdint.h>
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

/* Самая нижняя плитка раскладки стены касается низа участка целым рядом? */
static bool whole_row_at(const TlLayout *layout, double bottom)
{
    for (size_t t = 0; t < layout->tiles.count; t++) {
        const TlTile *tile = &layout->tiles.items[t];
        TlVec2 world = tl_affine_apply(&layout->local_to_world, tl_vec2(tile->rect.left, tile->rect.bottom));
        if (tile->kind == TL_TILE_WHOLE && fabs(world.y - bottom) < 0.01)
            return true;
    }
    return false;
}

void spec_walls(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Стены: развёртка и облицовка")
    {
        IT("развёртка — стены комнаты в ряд с зазором, высота помещения по умолчанию 2700")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            EXPECT(fabs(space->height - TL_ROOM_HEIGHT) < 1e-9);
            for (int band = 0; band < TL_WALL_BAND_COUNT; band++) /* прямая — без сдвига рядов */
                EXPECT(fabs(space->walls.bands[band].tile.row_shift) < 1e-9);
            EXPECT_INT(tl_space_wall_count(space), 4);
            double total = 0;
            for (size_t i = 0; i < 4; i++) {
                const TlRoomWall *wall = tl_space_wall_at(space, i);
                EXPECT(fabs(wall->x - total) < 1e-6);
                total += wall->length + TL_WALL_GAP;
            }
            EXPECT(fabs(tl_space_walls_bounds(space).right - (10000 + 3 * TL_WALL_GAP)) < 1e-6);
            /* стены идут по кругу: правый конец одной — левый конец следующей */
            for (size_t i = 0; i + 1 < 4; i++)
                EXPECT(tl_distance(tl_space_wall_at(space, i)->end, tl_space_wall_at(space, i + 1)->start) < 1e-6);
            /* без облицовки раскладок стен нет */
            EXPECT_INT(tl_space_wall_layout_count(space), 0);
            EXPECT_INT(tl_space_layout_count(space), tl_space_floor_layout_count(space));
            tl_project_free(project);
        }
        IT("проём на стене — дыра в развёртке на своей высоте; окно без пола")
        {
            TlProject *project = room_project(3000, 2000);
            TlOpening *window = tl_opening_new(tl_vec2(1000, -100), 1200);
            tl_opening_set_kind(window, TL_OPENING_WINDOW);
            tl_project_add_opening(project, window);
            tl_project_update_spaces(project);
            TlSpace *space = only_space(project);
            EXPECT_INT(space->wall_openings.count, 1);
            EXPECT(fabs(space->wall_openings.items[0].to - space->wall_openings.items[0].from - 1200) < 1);
            EXPECT(fabs(space->wall_openings.items[0].bottom - TL_WINDOW_SILL) < 1e-9);
            EXPECT_INT(tl_space_wall_hole_count(space), 1);
            const TlWallHole *hole = tl_space_wall_hole_at(space, 0);
            EXPECT(fabs(hole->rect.bottom - (TL_ROOM_HEIGHT - TL_WINDOW_SILL)) < 1e-6);
            EXPECT(fabs(hole->rect.top - (TL_ROOM_HEIGHT - TL_WINDOW_SILL - TL_WINDOW_HEIGHT)) < 1e-6);
            const TlRoomWall *wall = tl_space_wall_at(space, hole->wall);
            EXPECT(fabs(wall->length - 3000) < 1e-6);
            /* окно в 1000 от угла (0, 0): посередине между 400 и 1600 от одного из концов */
            double middle = (hole->rect.left + hole->rect.right) / 2 - wall->x;
            EXPECT(fabs(middle - 1000) < 1 || fabs(middle - 2000) < 1);
            /* облицовка обходит окно: площадь — стены минус проём */
            space->walls.enabled = true;
            double expected = (10000 * TL_ROOM_HEIGHT - 1200 * TL_WINDOW_HEIGHT) / 1e6;
            EXPECT(fabs(tl_space_walls_area(space) / 1e6 - expected) < 0.05);
            tl_project_free(project);
        }
        IT("полосы: низ, фриз над ним, основная плитка до верха облицовки; ряды — от низа полосы")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            space->walls.height = 2400;
            space->walls.bands[TL_WALL_LOWER].enabled = true;
            space->walls.bands[TL_WALL_FRIEZE].enabled = true;
            double spans[2][2];
            EXPECT_INT(tl_wall_band_spans(&space->walls, TL_WALL_LOWER, space->height, spans), 1);
            EXPECT(fabs(spans[0][1] - 4 * 302) < 1e-9); /* 1200 — четыре ряда 300 со швом 2 */
            EXPECT_INT(tl_wall_band_spans(&space->walls, TL_WALL_FRIEZE, space->height, spans), 1);
            EXPECT(fabs(spans[0][0] - 1208) < 1e-9 && fabs(spans[0][1] - 1310) < 1e-9);
            EXPECT_INT(tl_wall_band_spans(&space->walls, TL_WALL_MAIN, space->height, spans), 1);
            EXPECT(fabs(spans[0][1] - 2400) < 1e-9);
            EXPECT_INT(tl_space_wall_layout_count(space), 12); /* три полосы на четыре стены */
            for (size_t i = 0; i < 12; i++) {
                TlWallBand band;
                size_t wall;
                TlLayout *layout = tl_space_wall_layout_at(space, i, &wall, &band);
                double bottom = band == TL_WALL_LOWER ? 0 : band == TL_WALL_FRIEZE ? 1208 : 1310;
                EXPECT(whole_row_at(layout, space->height - bottom));
                EXPECT(tl_space_layout_wall(space, tl_space_floor_layout_count(space) + i, NULL, NULL));
            }
            EXPECT(!tl_space_layout_wall(space, 0, NULL, NULL));
            /* фриз без низа — на своей высоте, основная плитка — под ним и над ним */
            space->walls.bands[TL_WALL_LOWER].enabled = false;
            EXPECT_INT(tl_wall_band_spans(&space->walls, TL_WALL_MAIN, space->height, spans), 2);
            EXPECT(fabs(spans[0][0]) < 1e-9 && fabs(spans[0][1] - 1200) < 1e-9); /* фриз — на dy 1200 */
            tl_project_free(project);
        }
        IT("фриз — один ряд: высота фриза — высота его плитки; полоса без «целыми рядами» — без подрезки по высоте")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            space->walls.bands[TL_WALL_FRIEZE].enabled = true;
            double spans[2][2];
            space->walls.bands[TL_WALL_FRIEZE].tile.tile_height = 50;
            EXPECT_INT(tl_wall_band_spans(&space->walls, TL_WALL_FRIEZE, space->height, spans), 1);
            EXPECT(fabs(spans[0][1] - spans[0][0] - 52) < 1e-9); /* плитка 50 и шов над ней */
            space->walls.bands[TL_WALL_FRIEZE].tile.tile_height = 150;
            tl_wall_band_spans(&space->walls, TL_WALL_FRIEZE, space->height, spans);
            EXPECT(fabs(spans[0][1] - spans[0][0] - 152) < 1e-9);
            /* высота полосы (width) у фриза не действует */
            space->walls.bands[TL_WALL_FRIEZE].width = 900;
            tl_wall_band_spans(&space->walls, TL_WALL_FRIEZE, space->height, spans);
            EXPECT(fabs(spans[0][1] - spans[0][0] - 152) < 1e-9);
            /* низ 1200 без подгонки: 1200 плитки и шов; плитка фриза по высоте — целая */
            space->walls.bands[TL_WALL_LOWER].enabled = true;
            space->walls.bands[TL_WALL_LOWER].fit = false;
            EXPECT_INT(tl_wall_band_spans(&space->walls, TL_WALL_LOWER, space->height, spans), 1);
            EXPECT(fabs(spans[0][1] - 1202) < 1e-9);
            for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
                TlWallBand band;
                size_t wall;
                const TlLayout *layout = tl_space_wall_layout_at(space, i, &wall, &band);
                if (band == TL_WALL_FRIEZE && wall == 0)
                    EXPECT(layout->whole_count > 0);
            }
            tl_project_free(project);
        }
        IT("смета: облицовка стен — отдельной строкой работ, площадь стен — отдельно от пола")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->tile.price_work_m2 = 1000;
            space->walls.enabled = true;
            space->walls.bands[TL_WALL_MAIN].tile.price_work_m2 = 1500;
            TlSummary summary = tl_report_summarize(project);
            EXPECT(fabs(summary.wall_area / 1e6 - 10 * 2.7) < 0.05);
            EXPECT(fabs(summary.tiled_area / 1e6 - 6) < 0.1);
            TlEstimate estimate = tl_estimate_from_summary(&summary);
            bool walls = false;
            for (size_t i = 0; i < estimate.lines.count; i++)
                walls |= strcmp(estimate.lines.items[i].name, "Облицовка стен") == 0 &&
                         fabs(estimate.lines.items[i].price - 1500) < 1e-9;
            EXPECT(walls);
            tl_estimate_free(&estimate);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("высота помещения, облицовка и вид проёма сохраняются в файле проекта")
        {
            TlProject *project = room_project(3000, 2000);
            TlOpening *window = tl_opening_new(tl_vec2(1000, -100), 1200);
            tl_opening_set_kind(window, TL_OPENING_WINDOW);
            window->sill = 850;
            tl_project_add_opening(project, window);
            tl_project_update_spaces(project);
            TlSpace *space = only_space(project);
            space->height = 2500;
            space->walls.enabled = true;
            space->walls.height = 2000;
            space->walls.bands[TL_WALL_FRIEZE].enabled = true;
            space->walls.bands[TL_WALL_FRIEZE].dy = 900;
            snprintf(space->walls.bands[TL_WALL_MAIN].tile.color, TL_COLOR_LENGTH, "%s", "#123456");
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            TlSpace *copy = only_space(loaded);
            EXPECT(fabs(copy->height - 2500) < 1e-9);
            EXPECT(tl_wall_finishes_equal(&copy->walls, &space->walls));
            EXPECT_INT(loaded->openings.items[0]->kind, TL_OPENING_WINDOW);
            EXPECT(fabs(loaded->openings.items[0]->sill - 850) < 1e-9);
            EXPECT_INT(copy->wall_openings.count, 1);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("плитка стены сдвигается своим смещением; труба на стене сверлит плитку")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            TlLayout *layout = tl_space_wall_layout_at(space, 0, NULL, NULL);
            double left = layout->tiles.items[0].rect.left;
            tl_space_set_wall_offset(space, 0, tl_vec2(100, 0));
            layout = tl_space_wall_layout_at(space, 0, NULL, NULL);
            bool moved = false;
            for (size_t t = 0; t < layout->tiles.count; t++)
                moved |= fabs(fmod(layout->tiles.items[t].rect.left - left - 100 + 6020, 602)) < 0.01;
            EXPECT(moved);
            EXPECT(fabs(tl_space_wall_offset(space, 0).x - 100) < 1e-9);
            EXPECT(fabs(tl_space_wall_offset(space, 1).x) < 1e-9); /* у других стен — своё */
            const TlRoomWall *wall = tl_space_wall_at(space, 1);
            tl_wall_pipe_list_push(&space->wall_pipes, (TlWallPipe){wall->edge, {500, 1000}, 40});
            int drills = 0;
            for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
                size_t on;
                TlLayout *l = tl_space_wall_layout_at(space, i, &on, NULL);
                for (size_t t = 0; t < l->tiles.count; t++)
                    drills += on == 1 ? (int)l->tiles.items[t].drills.count : 0;
            }
            EXPECT(drills >= 1);
            /* точка на стене ↔ развёртка */
            TlVec2 point = tl_space_wall_to_development(space, 1, tl_vec2(500, 1000));
            size_t found;
            TlVec2 at;
            EXPECT(tl_space_wall_from_development(space, point, &found, &at));
            EXPECT_INT(found, 1);
            EXPECT(fabs(at.x - 500) < 1e-9 && fabs(at.y - 1000) < 1e-9);
            tl_project_free(project);
        }
        IT("пол под стеной: откинут вниз; «Швы как у пола» — шов стены продолжает шов пола")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            space->tile.offset_x = 137; /* сетка пола не от угла */
            for (size_t w = 0; w < tl_space_wall_count(space); w++) {
                const TlRoomWall *wall = tl_space_wall_at(space, w);
                TlAffine m = tl_space_wall_floor_affine(space, w);
                TlVec2 start = tl_affine_apply(&m, wall->start), end = tl_affine_apply(&m, wall->end);
                EXPECT(fabs(start.x - wall->x) < 1e-6 && fabs(start.y - space->height) < 1e-6);
                EXPECT(fabs(end.x - wall->x - wall->length) < 1e-6);
                /* центр комнаты — ниже пола в развёртке */
                EXPECT(tl_affine_apply(&m, tl_vec2(1500, 1000)).y > space->height);
            }
            EXPECT(tl_space_wall_align_floor(space, 0));
            EXPECT(tl_space_wall_align_floor(space, 0)); /* второй раз — уже совпадают: сдвиг ~0 */
            TlVec2 first = tl_space_wall_offset(space, 0);
            EXPECT(tl_space_wall_align_floor(space, 0));
            EXPECT(fabs(tl_space_wall_offset(space, 0).x - first.x) < 0.01);
            EXPECT(fabs(first.x) > 0.01);
            for (size_t w = 0; w < tl_space_wall_count(space); w++) /* у каждой стены — швы пола вдоль неё */
                EXPECT(tl_space_wall_align_floor(space, w));
            tl_project_free(project);
        }
        IT("привязка на развёртке: к углу стены, полу и шву плитки")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            const TlRoomWall *wall = tl_space_wall_at(space, 1);
            TlVec2 snapped = tl_space_walls_snap(space, tl_vec2(wall->x + 3, space->height - 4), 10);
            EXPECT(fabs(snapped.x - wall->x) < 1e-9 && fabs(snapped.y - space->height) < 1e-9);
            snapped = tl_space_walls_snap(space, tl_vec2(wall->x + 603, space->height - 1500), 10);
            EXPECT(fabs(snapped.x - (wall->x + 601)) < 1e-6); /* середина шва 600 + 1 */
            tl_project_free(project);
        }
        IT("трубы, линейки и смещения на стенах сохраняются в файле проекта")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            tl_wall_pipe_list_push(&space->wall_pipes, (TlWallPipe){2, {700, 1100}, 20});
            tl_wall_ruler_list_push(&space->wall_rulers, (TlWallRuler){1, {0, 900}, {1200, 900}});
            tl_space_set_wall_offset(space, 0, tl_vec2(150, -30));
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[256];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            TlSpace *copy = only_space(loaded);
            EXPECT_INT(copy->wall_pipes.count, 1);
            EXPECT(fabs(copy->wall_pipes.items[0].diameter - 20) < 1e-9);
            EXPECT_INT(copy->wall_rulers.count, 1);
            EXPECT(fabs(copy->wall_rulers.items[0].end.x - 1200) < 1e-9);
            EXPECT(fabs(tl_space_wall_offset(copy, 0).y + 30) < 1e-9);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
        IT("откосы окна: слева, справа и сверху; на стыке со стеной — запил 45° у кусков стены и откоса")
        {
            TlProject *project = room_project(3000, 2000);
            TlOpening *window = tl_opening_new(tl_vec2(1500, -100), 1200);
            tl_opening_set_kind(window, TL_OPENING_WINDOW);
            window->reveal = 200;
            tl_project_add_opening(project, window);
            tl_project_update_spaces(project);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            int reveals = 0, reveal_miters = 0, wall_miters = 0;
            for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
                TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, NULL);
                bool reveal = tl_space_wall_layout_reveal(space, i) != SIZE_MAX;
                reveals += reveal;
                for (size_t t = 0; t < layout->tiles.count; t++)
                    *(reveal ? &reveal_miters : &wall_miters) += (int)layout->tiles.items[t].miters.count;
            }
            EXPECT_INT(reveals, 3);
            EXPECT(reveal_miters > 0 && wall_miters > 0);
            /* в чертежах подрезок — стороны с запилом */
            TlCutCardList cards = tl_report_cut_cards(project);
            int marked = 0;
            for (size_t c = 0; c < cards.count; c++)
                for (size_t p = 0; p < cards.items[c].parts.count; p++)
                    marked += cards.items[c].parts.items[p].miters.count > 0;
            EXPECT(marked > 0);
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
        IT("наружный угол стен (выступ в комнату) — запил у кусков на углу; внутренние углы — без запила")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {3000, 0}, {3000, 2000}, {1500, 2000}, {1500, 1000}, {0, 1000}};
            tl_project_add_outline(project, tl_outline_new(corners, 6, 200));
            tl_project_update_spaces(project);
            TlSpace *space = only_space(project);
            space->walls.enabled = true;
            int marked = 0;
            for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
                TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, NULL);
                for (size_t t = 0; t < layout->tiles.count; t++)
                    marked += layout->tiles.items[t].miters.count > 0;
            }
            EXPECT(marked > 0); /* угол (1500, 1000) — наружный: обе стены у него */
            TlProject *box = room_project(3000, 2000); /* прямоугольная — все углы внутренние */
            only_space(box)->walls.enabled = true;
            int none = 0;
            for (size_t i = 0; i < tl_space_wall_layout_count(only_space(box)); i++) {
                TlLayout *layout = tl_space_wall_layout_at(only_space(box), i, NULL, NULL);
                for (size_t t = 0; t < layout->tiles.count; t++)
                    none += layout->tiles.items[t].miters.count > 0;
            }
            EXPECT_INT(none, 0);
            tl_project_free(box);
            tl_project_free(project);
        }
    }
}

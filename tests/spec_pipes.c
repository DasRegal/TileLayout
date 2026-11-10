#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_drag.h"
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

static int drilled_tiles(TlLayout *layout)
{
    int count = 0;
    for (size_t i = 0; i < layout->tiles.count; i++)
        count += layout->tiles.items[i].drills.count > 0;
    return count;
}

/* Есть ли у плитки раскладки край (в осях раскладки) на координате value по оси x или y. */
static bool has_edge_at(TlLayout *layout, bool along_x, double value)
{
    for (size_t i = 0; i < layout->tiles.count; i++) {
        TlRect r = layout->tiles.items[i].rect;
        double low = along_x ? r.left : r.top, high = along_x ? r.right : r.bottom;
        if (fabs(low - value) < 0.05 || fabs(high - value) < 0.05)
            return true;
    }
    return false;
}

void spec_pipes(void)
{
    tl_tile_settings_reset_defaults(); /* другие спецификации запоминают свои настройки для новых комнат */
    DESCRIBE("Отверстия под трубы")
    {
        IT("труба внутри плитки — одно отверстие, центр от угла плитки; плитка остаётся целой")
        {
            TlProject *project = room_project(1202, 602);
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(150, 120), 50));
            tl_project_update_spaces(project);
            TlLayout *layout = tl_space_layout(only_space(project));
            EXPECT_INT(drilled_tiles(layout), 1);
            EXPECT_INT(layout->whole_count, 4);
            for (size_t i = 0; i < layout->tiles.count; i++)
                if (layout->tiles.items[i].drills.count == 1) {
                    EXPECT_NEAR(layout->tiles.items[i].drills.items[0].center.x, 150, 1e-6);
                    EXPECT_NEAR(layout->tiles.items[i].drills.items[0].center.y, 120, 1e-6);
                }
            tl_project_free(project);
        }
        IT("труба на пересечении швов сверлит четыре плитки — у каждой своя четверть")
        {
            TlProject *project = room_project(1202, 602);
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(601, 301), 50));
            tl_project_update_spaces(project);
            EXPECT_INT(drilled_tiles(tl_space_layout(only_space(project))), 4);
            tl_project_free(project);
        }
        IT("целая плитка с отверстием — карточка чертежа: где сверлить и какой коронкой")
        {
            TlProject *project = room_project(1202, 602);
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(150, 120), 110));
            tl_project_update_spaces(project);
            TlCutCardList cards = tl_report_cut_cards(project);
            EXPECT_INT(cards.count, 1);
            if (cards.count == 1) {
                EXPECT_INT(cards.items[0].kind, TL_CARD_DRILLED);
                EXPECT(strstr(cards.items[0].subtitle, "Ø110") != NULL);
                EXPECT_INT(cards.items[0].parts.items[0].drills.count, 1);
                EXPECT_NEAR(cards.items[0].width, 600, 1e-9);
            }
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
        IT("кусок с отверстием не идёт в пару обрезков")
        {
            TlProject *project = room_project(900, 302); /* 600 + 298: справа подрезка */
            TlSummary before = tl_report_summarize(project);
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(750, 150), 50));
            tl_project_update_spaces(project);
            TlSummary after = tl_report_summarize(project);
            EXPECT(after.offcut_pairs.count <= before.offcut_pairs.count);
            tl_summary_free(&before);
            tl_summary_free(&after);
            tl_project_free(project);
        }
        IT("трубы сохраняются в файле проекта и едут вместе с домом")
        {
            TlProject *project = room_project(1202, 602);
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(300, 200), 32));
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            char error[128];
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->pipes.count, 1);
            if (loaded->pipes.count == 1) {
                EXPECT_NEAR(loaded->pipes.items[0]->diameter, 32, 0);
                EXPECT_NEAR(loaded->pipes.items[0]->center.x, 300, 0);
            }
            TlEditToken *token = tl_project_begin_edit(loaded, loaded->outlines.items[0]);
            tl_outline_translate(loaded->outlines.items[0], tl_vec2(1000, 0));
            TlEditOptions options = {.translate = tl_vec2(1000, 0), .has_translate = true};
            tl_project_end_edit(loaded, token, &options);
            EXPECT_NEAR(loaded->pipes.items[0]->center.x, 1300, 1e-9);
            free(text);
            tl_project_free(loaded);
            tl_project_free(project);
        }
    }

    DESCRIBE("Раскладка от линии")
    {
        IT("шов по линейке: вертикальная линия на x = 1000 — между плитками шов посередине")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            double x, y;
            EXPECT(tl_layout_line_offsets(space, tl_vec2(1000, 100), tl_vec2(1000, 1900), TL_LINE_JOINT,
                                          tl_vec2(1100, 500), &x, &y));
            space->tile.offset_x = x;
            space->tile.offset_y = y;
            EXPECT(has_edge_at(tl_space_layout(space), true, 1000 - 1));
            EXPECT(has_edge_at(tl_space_layout(space), true, 1000 + 1));
            tl_project_free(project);
        }
        IT("середина плитки по оси и целая плитка от линии в нужную сторону")
        {
            TlProject *project = room_project(3000, 2000);
            TlSpace *space = only_space(project);
            double x, y;
            EXPECT(tl_layout_line_offsets(space, tl_vec2(100, 700), tl_vec2(2900, 700), TL_LINE_TILE_EDGE,
                                          tl_vec2(500, 300), &x, &y));
            space->tile.offset_x = x;
            space->tile.offset_y = y;
            EXPECT(has_edge_at(tl_space_layout(space), false, 700)); /* низ плитки — на линии */
            EXPECT(tl_layout_line_offsets(space, tl_vec2(1500, 100), tl_vec2(1500, 1900), TL_LINE_TILE_CENTER,
                                          tl_vec2(0, 0), &x, &y));
            space->tile.offset_x = x;
            EXPECT(has_edge_at(tl_space_layout(space), true, 1500 - 300));
            tl_project_free(project);
        }
        IT("наклонная линия — нельзя")
        {
            TlProject *project = room_project(3000, 2000);
            double x, y;
            EXPECT(!tl_layout_line_offsets(only_space(project), tl_vec2(0, 0), tl_vec2(1000, 1000), TL_LINE_JOINT,
                                           tl_vec2(0, 0), &x, &y));
            tl_project_free(project);
        }
        IT("от двери: от стыка в проёме в комнату — целый ряд, проём — посередине плитки или шва")
        {
            TlProject *project = tl_project_demo();
            TlSpace *living = project->spaces.items[0];
            TlVec2 from, to;
            EXPECT(tl_project_space_door_edge(project, living, project->openings.items[0], &from, &to));
            double x, y;
            EXPECT(tl_layout_door_offsets(living, from, to, &x, &y));
            living->tile.offset_x = x;
            living->tile.offset_y = y;
            TlLayout *layout = tl_space_layout(living);
            TlAffine to_local = tl_layout_world_to_local(living);
            TlVec2 a = tl_affine_apply(&to_local, from), b = tl_affine_apply(&to_local, to);
            EXPECT(has_edge_at(layout, true, a.x)); /* порог вертикален: край плитки на нём */
            /* куски у косяков одинаковы: проём симметричен относительно плиток */
            double middle = (a.y + b.y) / 2;
            bool centered = false;
            for (size_t i = 0; i < layout->tiles.count; i++) {
                TlRect r = layout->tiles.items[i].rect;
                double tile_middle = (r.top + r.bottom) / 2;
                centered |= fabs(tile_middle - middle) < 0.05 || fabs(r.bottom + 1 - middle) < 0.05;
            }
            EXPECT(centered);
            TlOpeningList doors = tl_project_space_doors(project, living);
            EXPECT_INT(doors.count, 1);
            tl_opening_list_free(&doors);
            tl_project_free(project);
        }
        IT("дверь со швом под ней — дверь обеих комнат; целый ряд — от края пола, а не от стыка")
        {
            TlProject *project = tl_project_demo();
            TlOpening *door = project->openings.items[0];
            door->threshold_gap = 2; /* край пола каждой комнаты — в 1 мм от стыка */
            tl_project_update_spaces(project);
            for (size_t s = 0; s < project->spaces.count; s++) {
                TlOpeningList doors = tl_project_space_doors(project, project->spaces.items[s]);
                EXPECT_INT(doors.count, 1);
                tl_opening_list_free(&doors);
            }
            TlSpace *living = project->spaces.items[0];
            TlVec2 from, to, a, b;
            EXPECT(tl_project_space_door_edge(project, living, door, &from, &to));
            EXPECT(tl_project_opening_threshold(project, door, &a, &b));
            EXPECT_NEAR(tl_distance(tl_vec2_midpoint(from, to), tl_vec2_midpoint(a, b)), 1, 1e-6);
            double x, y;
            EXPECT(tl_layout_door_offsets(living, from, to, &x, &y));
            living->tile.offset_x = x;
            living->tile.offset_y = y;
            TlLayout *layout = tl_space_layout(living);
            TlAffine to_local = tl_layout_world_to_local(living);
            double edge = tl_affine_apply(&to_local, from).x;
            bool whole_at_door = false; /* у порога — целые плитки, без полоски в 1 мм */
            for (size_t i = 0; i < layout->tiles.count; i++) {
                const TlTile *tile = &layout->tiles.items[i];
                if (fabs(tile->rect.right - edge) < 0.05 || fabs(tile->rect.left - edge) < 0.05)
                    whole_at_door = true;
            }
            EXPECT(whole_at_door);
            tl_project_free(project);
        }
        IT("линейки комнаты — те, чья середина в ней")
        {
            TlProject *project = tl_project_demo();
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(1000, 500), tl_vec2(1000, 1500)));
            tl_project_add_ruler(project, tl_ruler_new(tl_vec2(5000, 500), tl_vec2(5000, 1500)));
            TlRulerList rulers = tl_project_space_rulers(project, project->spaces.items[0]);
            EXPECT_INT(rulers.count, 1);
            tl_ruler_list_free(&rulers);
            tl_project_free(project);
        }
        IT("трубу тянут целиком — по сетке")
        {
            TlProject *project = room_project(1202, 602);
            TlPipe *pipe = tl_pipe_new(tl_vec2(100, 100), 50);
            tl_project_add_pipe(project, pipe);
            TlDrag drag = {0};
            TlDragTarget target = {.kind = TL_DRAG_PIPE, .pipe = pipe};
            EXPECT(tl_drag_begin(&drag, project, target, tl_vec2(100, 100)));
            TlSnapContext snap = {.project = project, .tolerance = 5, .grid_step = 50};
            tl_drag_move(&drag, project, tl_vec2(237, 161), &snap, false, NULL);
            EXPECT_NEAR(pipe->center.x, 250, 1e-9);
            EXPECT_NEAR(pipe->center.y, 150, 1e-9);
            tl_drag_end(&drag, project);
            EXPECT_INT(drilled_tiles(tl_space_layout(only_space(project))), 1);
            tl_project_free(project);
        }
    }
}

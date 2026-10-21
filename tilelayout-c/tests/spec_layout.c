#include "spec.h"
#include "specs.h"

#include "tl_layout.h"
#include "tl_project.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    return project;
}

static TlSpace *only_space(TlProject *project) { return project->spaces.items[0]; }

/* Вид плитки, накрывающей точку чуть внутри от угла комнаты (в осях раскладки). */
static int kind_near_local_point(TlLayout *layout, TlVec2 point)
{
    for (size_t i = 0; i < layout->tiles.count; i++)
        if (tl_rect_contains_point(layout->tiles.items[i].rect, point))
            return (int)layout->tiles.items[i].kind;
    return -1;
}

void spec_layout(void)
{
    DESCRIBE("Раскладка плитки")
    {
        IT("комната 1202×602, плитка 600×300 со швом 2 мм — ровно 4 целые плитки")
        {
            TlProject *project = room_project(1202, 602);
            TlLayout *layout = tl_space_layout(only_space(project));
            EXPECT_INT(layout->whole_count, 4);
            EXPECT_INT(tl_layout_total(layout), 4);
            tl_project_free(project);
        }
        IT("СВП только между плитками: 2 на длинную и 1 на короткую сторону дают 6")
        {
            TlProject *project = room_project(1202, 602);
            TlLayout *layout = tl_space_layout(only_space(project));
            EXPECT_INT(layout->clip_points.count, 6);
            only_space(project)->tile.clips_per_long_side = 3;
            only_space(project)->tile.clips_per_short_side = 2;
            EXPECT_INT(tl_space_layout(only_space(project))->clip_points.count, 10);
            tl_project_free(project);
        }
        IT("подрезка уже минимальной ширины считается узкой")
        {
            TlProject *project = room_project(1230, 602); /* справа полоски по 26 мм */
            TlLayout *layout = tl_space_layout(only_space(project));
            EXPECT_INT(layout->narrow_count, 2);
            EXPECT_INT(layout->whole_count, 4);
            tl_project_free(project);
        }
        IT("отступ от стены уменьшает плитки у стен до подрезок")
        {
            TlProject *project = room_project(1202, 602);
            only_space(project)->tile.wall_gap = 5;
            TlLayout *layout = tl_space_layout(only_space(project));
            EXPECT_INT(layout->whole_count, 0);
            EXPECT_INT(tl_layout_total(layout), 4);
            tl_project_free(project);
        }
        IT("кусок, вырезанный П-образно, помечается фигурным, а его контур сохраняется")
        {
            TlProject *project = room_project(1202, 602);
            tl_project_add_partition(project, tl_partition_new(tl_vec2(300, 0), tl_vec2(300, 150), 100));
            tl_project_update_spaces(project);
            TlLayout *layout = tl_space_layout(only_space(project));
            int figured = 0;
            for (size_t i = 0; i < layout->tiles.count; i++)
                if (layout->tiles.items[i].figured) {
                    figured++;
                    EXPECT_INT(layout->tiles.items[i].shapes.items[0].count, 8);
                }
            EXPECT_INT(figured, 1);
            tl_project_free(project);
        }
        IT("клей: площадь × расход × толщина слоя, мешки — с округлением вверх")
        {
            TlProject *project = room_project(2000, 2000);
            TlSpace *space = only_space(project);
            space->tile.glue_layer_mm = 4;
            TlLayout *layout = tl_space_layout(space);
            EXPECT_NEAR(tl_layout_glue_kg(space, layout), 4.0 * 1.5 * 4, 1e-9);
            EXPECT_INT(tl_layout_glue_bags(space, layout), 1);
            tl_project_free(project);
        }
    }

    DESCRIBE("Смещение сетки")
    {
        IT("от выбранного угла в углу стоит целая плитка — при любом сдвиге рядов")
        {
            TlProject *project = tl_project_demo();
            const TlLayoutCorner corners[4] = {TL_CORNER_TOP_LEFT, TL_CORNER_TOP_RIGHT,
                                               TL_CORNER_BOTTOM_LEFT, TL_CORNER_BOTTOM_RIGHT};
            const double shifts[3] = {0.0, 0.5, 1.0 / 3.0};
            int whole_corners = 0;
            for (size_t s = 0; s < project->spaces.count; s++) {
                TlSpace *space = project->spaces.items[s];
                for (int k = 0; k < 3; k++)
                    for (int c = 0; c < 4; c++) {
                        space->tile.row_shift = shifts[k];
                        tl_layout_corner_offsets(space, corners[c], &space->tile.offset_x, &space->tile.offset_y);
                        tl_layout_normalize_offsets(&space->tile, &space->tile.offset_x, &space->tile.offset_y);
                        TlLayout *layout = tl_space_layout(space);
                        TlVec2 corner = tl_layout_local_corner(space, corners[c]);
                        double sx = corners[c] == TL_CORNER_TOP_LEFT || corners[c] == TL_CORNER_BOTTOM_LEFT ? 5 : -5;
                        double sy = corners[c] == TL_CORNER_TOP_LEFT || corners[c] == TL_CORNER_TOP_RIGHT ? 5 : -5;
                        if (kind_near_local_point(layout, tl_vec2(corner.x + sx, corner.y + sy)) == TL_TILE_WHOLE)
                            whole_corners++;
                    }
            }
            EXPECT_INT(whole_corners, 2 * 3 * 4);
            tl_project_free(project);
        }
        IT("«центр — шов»: середина комнаты попадает на пересечение швов")
        {
            TlProject *project = room_project(1802, 902);
            TlSpace *space = only_space(project);
            tl_layout_center_offsets(space, true, &space->tile.offset_x, &space->tile.offset_y);
            TlLayout *layout = tl_space_layout(space);
            EXPECT_INT(layout->whole_count, 4);
            EXPECT_INT(tl_layout_total(layout), 16); /* 4 колонки × 4 ряда, целые — 2×2 */
            tl_project_free(project);
        }
        IT("подбор без узких подрезок убирает узкие полоски")
        {
            TlProject *project = room_project(1230, 602);
            TlSpace *space = only_space(project);
            EXPECT(tl_layout_optimize_narrow(space, &space->tile.offset_x, &space->tile.offset_y));
            EXPECT_INT(tl_space_layout(space)->narrow_count, 0);
            tl_project_free(project);
        }
        IT("приведение смещения к шагу сетки не меняет рисунок вразбежку")
        {
            TlTileSettings tile = tl_tile_settings_factory();
            tile.row_shift = 0.5;
            double x = 100, y = 302 * 3 + 50;  /* на три ряда ниже */
            tl_layout_normalize_offsets(&tile, &x, &y);
            EXPECT_NEAR(y, 50, 1e-9);
            EXPECT_NEAR(x, 100 + 301, 1e-9); /* нечётное число рядов — сдвиг на полплитки */
        }
    }
}

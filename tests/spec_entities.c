#include "spec.h"
#include "specs.h"

#include "tl_outline.h"
#include "tl_partition.h"
#include "tl_tile_settings.h"

static TlOutline *rectangle_house(double width, double height, double thickness)
{
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    return tl_outline_new(corners, 4, thickness);
}

static void shift_like_outline(TlOutline *outline, size_t wall, TlVec2 offset, void *calls)
{
    (*(int *)calls)++;
    tl_outline_shift_wall(outline, wall, offset);
}

void spec_entities(void)
{
    DESCRIBE("Контур дома (наружные стены)")
    {
        IT("считает длину стены по внутренней, наружной стороне и по оси")
        {
            TlOutline *house = rectangle_house(6400, 3600, 250);
            EXPECT_NEAR(tl_outline_inner_length(house, 0), 6400, 1e-9);
            EXPECT_NEAR(tl_outline_outer_length(house, 0), 6900, 1e-9);
            EXPECT_NEAR(tl_outline_axis_length(house, 0), 6650, 1e-9);
            tl_outline_free(house);
        }
        IT("сдвигает стену параллельно, соседние стены сохраняют направление")
        {
            TlOutline *house = rectangle_house(4000, 3000, 200);
            tl_outline_shift_wall(house, 1, tl_vec2(500, 0)); /* правая стена */
            EXPECT_NEAR(tl_outline_inner_length(house, 0), 4500, 1e-9);
            EXPECT_NEAR(tl_outline_inner_length(house, 1), 3000, 1e-9);
            tl_outline_free(house);
        }
        IT("меняет длину стены с сохранением углов через сдвиг следующей стены")
        {
            TlOutline *house = rectangle_house(4000, 3000, 200);
            int shifter_calls = 0;
            tl_outline_set_inner_length(house, 0, 4200, true, shift_like_outline, &shifter_calls);
            EXPECT_INT(shifter_calls, 1);
            EXPECT_NEAR(tl_outline_inner_length(house, 0), 4200, 1e-9);
            EXPECT_NEAR(tl_outline_inner_length(house, 2), 4200, 1e-9);
            tl_outline_free(house);
        }
        IT("добавляет и удаляет угол, сохраняя толщины и блокировки стен")
        {
            TlOutline *house = rectangle_house(4000, 3000, 200);
            house->thickness.items[0] = 300;
            house->locked.items[0] = true;
            tl_outline_insert_vertex(house, 0, tl_vec2(2000, 0));
            EXPECT_INT(tl_outline_wall_count(house), 5);
            EXPECT_NEAR(house->thickness.items[1], 300, 1e-9);
            EXPECT(house->locked.items[1]);
            EXPECT(tl_outline_vertex_locked(house, 1));
            EXPECT(tl_outline_remove_vertex(house, 1));
            EXPECT_INT(tl_outline_wall_count(house), 4);
            EXPECT_INT(house->thickness.count, 4);
            EXPECT_INT(house->locked.count, 4);
            tl_outline_free(house);
        }
        IT("не даёт удалить угол у треугольника")
        {
            const TlVec2 corners[] = {{0, 0}, {1000, 0}, {0, 1000}};
            TlOutline *triangle = tl_outline_new(corners, 3, 200);
            EXPECT(!tl_outline_remove_vertex(triangle, 0));
            tl_outline_free(triangle);
        }
    }

    DESCRIBE("Перегородка")
    {
        IT("знает длину и меняет её, двигая конец")
        {
            TlPartition *partition = tl_partition_new(tl_vec2(0, 0), tl_vec2(0, 1000), 100);
            EXPECT_NEAR(tl_partition_length(partition), 1000, 1e-9);
            tl_partition_set_length(partition, 1500);
            EXPECT_NEAR(partition->end.y, 1500, 1e-9);
            tl_partition_free(partition);
        }
        IT("тело — прямоугольник толщиной в обе стороны от оси")
        {
            TlPartition *partition = tl_partition_new(tl_vec2(0, 0), tl_vec2(1000, 0), 100);
            TlPolygon body = tl_partition_body(partition, 0);
            TlRect bounds = tl_polygon_bounds(&body);
            EXPECT_NEAR(bounds.top, -50, 1e-9);
            EXPECT_NEAR(bounds.bottom, 50, 1e-9);
            EXPECT_NEAR(tl_polygon_area(&body), 100000, 1e-6);
            tl_polygon_free(&body);
            tl_partition_free(partition);
        }
    }

    DESCRIBE("Настройки плитки")
    {
        IT("по умолчанию — плитка 600×300, шов 2 мм, клей 1,5 кг/м² на 1 мм")
        {
            TlTileSettings tile = tl_tile_settings_factory();
            EXPECT_NEAR(tile.tile_width, 600, 1e-9);
            EXPECT_NEAR(tile.tile_height, 300, 1e-9);
            EXPECT_NEAR(tile.joint_width, 2, 1e-9);
            EXPECT_NEAR(tile.glue_kg_per_m2_mm, 1.5, 1e-9);
            EXPECT(tile.enabled);
        }
        IT("запоминает последние настройки для новых комнат, кроме смещения сетки")
        {
            TlTileSettings edited = tl_tile_settings_factory();
            edited.tile_width = 450;
            edited.offset_x = 123;
            tl_tile_settings_remember_as_default(&edited);
            TlTileSettings fresh = tl_tile_settings_for_new_room();
            EXPECT_NEAR(fresh.tile_width, 450, 1e-9);
            EXPECT_NEAR(fresh.offset_x, 0, 1e-9);
            tl_tile_settings_reset_defaults();
        }
    }
}

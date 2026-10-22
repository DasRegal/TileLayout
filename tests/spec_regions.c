#include "spec.h"
#include "specs.h"

#include "tl_regions.h"

static TlPolygon rect_polygon(double left, double top, double right, double bottom)
{
    return tl_polygon_from_rect((TlRect){left, top, right, bottom});
}

static double total_area(const TlRegionList *regions)
{
    double sum = 0.0;
    for (size_t i = 0; i < regions->count; i++)
        sum += tl_region_area(&regions->items[i]);
    return sum;
}

void spec_regions(void)
{
    DESCRIBE("Области: пол комнаты минус перегородки")
    {
        IT("перегородка от стены до стены делит комнату на две области")
        {
            TlPolygon room = rect_polygon(0, 0, 1000, 600);
            TlPolygon wall = rect_polygon(400, 0, 500, 600);
            TlRegionList regions = tl_regions_difference(&room, &wall, 1);
            EXPECT_INT(regions.count, 2);
            EXPECT_NEAR(total_area(&regions), 1000 * 600 - 100 * 600, 1e-6);
            tl_region_list_free_all(&regions);
            tl_polygon_free(&room);
            tl_polygon_free(&wall);
        }
        IT("тупиковая перегородка оставляет одну область с вырезом")
        {
            TlPolygon room = rect_polygon(0, 0, 1000, 600);
            TlPolygon wall = rect_polygon(400, 0, 500, 300);
            TlRegionList regions = tl_regions_difference(&room, &wall, 1);
            EXPECT_INT(regions.count, 1);
            EXPECT_NEAR(total_area(&regions), 600000 - 30000, 1e-6);
            tl_region_list_free_all(&regions);
            tl_polygon_free(&room);
            tl_polygon_free(&wall);
        }
        IT("отдельно стоящая перегородка становится дырой в области")
        {
            TlPolygon room = rect_polygon(0, 0, 1000, 600);
            TlPolygon column = rect_polygon(400, 200, 500, 300);
            TlRegionList regions = tl_regions_difference(&room, &column, 1);
            EXPECT_INT(regions.count, 1);
            EXPECT_INT(regions.items[0].holes.count, 1);
            EXPECT_NEAR(total_area(&regions), 600000 - 10000, 1e-6);
            EXPECT(!tl_region_contains(&regions.items[0], tl_vec2(450, 250)));
            EXPECT(tl_region_contains(&regions.items[0], tl_vec2(100, 100)));
            tl_region_list_free_all(&regions);
            tl_polygon_free(&room);
            tl_polygon_free(&column);
        }
        IT("Т-образное примыкание двух перегородок замыкает угловую комнату")
        {
            TlPolygon room = rect_polygon(0, 0, 1000, 600);
            TlPolygon cutters[2] = {rect_polygon(400, 0, 500, 400),     /* от верхней стены */
                                    rect_polygon(0, 300, 400, 400)};    /* от левой стены к первой */
            TlRegionList regions = tl_regions_difference(&room, cutters, 2);
            EXPECT_INT(regions.count, 2);
            EXPECT_NEAR(total_area(&regions), 600000 - 40000 - 40000, 1e-6);
            tl_region_list_free_all(&regions);
            tl_polygon_free(&room);
            tl_polygon_free(&cutters[0]);
            tl_polygon_free(&cutters[1]);
        }
        IT("перегородка вдоль стены (совпадающие стороны) не ломает разбиение")
        {
            TlPolygon room = rect_polygon(0, 0, 1000, 600);
            TlPolygon wall = rect_polygon(0, 0, 1000, 100);
            TlRegionList regions = tl_regions_difference(&room, &wall, 1);
            EXPECT_INT(regions.count, 1);
            EXPECT_NEAR(total_area(&regions), 500000, 1e-6);
            tl_region_list_free_all(&regions);
            tl_polygon_free(&room);
            tl_polygon_free(&wall);
        }
    }

    DESCRIBE("Площадь перекрытия многоугольников")
    {
        IT("считает перекрытие сдвинутых квадратов и ноль для непересекающихся")
        {
            TlPolygon a = rect_polygon(0, 0, 100, 100);
            TlPolygon b = rect_polygon(50, 30, 150, 130);
            TlPolygon far = rect_polygon(500, 500, 600, 600);
            EXPECT_NEAR(tl_polygons_overlap_area(&a, &b), 50 * 70, 1e-6);
            EXPECT_NEAR(tl_polygons_overlap_area(&a, &far), 0, 1e-9);
            tl_polygon_free(&a);
            tl_polygon_free(&b);
            tl_polygon_free(&far);
        }
    }
}

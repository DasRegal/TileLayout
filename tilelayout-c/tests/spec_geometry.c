#include "spec.h"
#include "specs.h"

#include "tl_geometry.h"

static TlPolygon make_polygon(const double *xy, size_t count)
{
    TlPolygon polygon = {0};
    for (size_t i = 0; i < count; i++)
        tl_polygon_push(&polygon, tl_vec2(xy[2 * i], xy[2 * i + 1]));
    return polygon;
}

static TlPolygon square(double size)
{
    const double xy[] = {0, 0, size, 0, size, size, 0, size};
    return make_polygon(xy, 4);
}

static TlPolygon l_shape(void)
{
    /* Г-образная комната 200×200 с вырезанным квадратом 100×100 справа снизу. */
    const double xy[] = {0, 0, 200, 0, 200, 100, 100, 100, 100, 200, 0, 200};
    return make_polygon(xy, 6);
}

void spec_geometry(void)
{
    DESCRIBE("Векторы")
    {
        IT("нормализуют вектор до единичной длины, нулевой оставляют нулевым")
        {
            TlVec2 unit = tl_vec2_normalized(tl_vec2(3, 4));
            EXPECT_NEAR(unit.x, 0.6, 1e-12);
            EXPECT_NEAR(unit.y, 0.8, 1e-12);
            TlVec2 zero = tl_vec2_normalized(tl_vec2(0, 0));
            EXPECT(zero.x == 0 && zero.y == 0);
        }
        IT("находят пересечение прямых и сообщают о параллельных")
        {
            TlVec2 hit;
            EXPECT(tl_line_intersection(tl_vec2(0, 0), tl_vec2(1, 0), tl_vec2(5, -5), tl_vec2(0, 1), &hit));
            EXPECT_NEAR(hit.x, 5, 1e-12);
            EXPECT_NEAR(hit.y, 0, 1e-12);
            EXPECT(!tl_line_intersection(tl_vec2(0, 0), tl_vec2(1, 0), tl_vec2(0, 3), tl_vec2(2, 0), &hit));
        }
        IT("меряют расстояние от точки до отрезка и параметр проекции")
        {
            double t;
            EXPECT_NEAR(tl_point_segment_distance(tl_vec2(5, 3), tl_vec2(0, 0), tl_vec2(10, 0), &t), 3, 1e-12);
            EXPECT_NEAR(t, 0.5, 1e-12);
            EXPECT_NEAR(tl_point_segment_distance(tl_vec2(-4, 3), tl_vec2(0, 0), tl_vec2(10, 0), &t), 5, 1e-12);
            EXPECT_NEAR(t, 0, 1e-12);
        }
    }

    DESCRIBE("Многоугольник")
    {
        IT("считает площадь и знак обхода")
        {
            TlPolygon polygon = square(10);
            EXPECT_NEAR(tl_polygon_area(&polygon), 100, 1e-9);
            EXPECT(tl_polygon_signed_area(&polygon) > 0);
            tl_polygon_free(&polygon);
        }
        IT("определяет, лежит ли точка внутри невыпуклого многоугольника")
        {
            TlPolygon room = l_shape();
            EXPECT(tl_polygon_contains(&room, tl_vec2(50, 150)));
            EXPECT(!tl_polygon_contains(&room, tl_vec2(150, 150)));
            EXPECT(tl_polygon_on_boundary(&room, tl_vec2(150, 100), 0.5));
            tl_polygon_free(&room);
        }
        IT("смещает стороны наружу на свою величину (толщина стен)")
        {
            TlPolygon inner = square(100);
            const double thickness[] = {10, 20, 10, 20};
            TlPolygon outer = tl_polygon_offset(&inner, thickness);
            EXPECT_INT(outer.count, 4);
            TlRect bounds = tl_polygon_bounds(&outer);
            EXPECT_NEAR(bounds.right - bounds.left, 140, 1e-9);
            EXPECT_NEAR(bounds.bottom - bounds.top, 120, 1e-9);
            tl_polygon_free(&inner);
            tl_polygon_free(&outer);
        }
        IT("убирает повторяющиеся точки и точки на прямой")
        {
            const double xy[] = {0, 0, 50, 0, 100, 0, 100, 0, 100, 100, 0, 100};
            TlPolygon polygon = make_polygon(xy, 6);
            tl_polygon_clean(&polygon, 0.2);
            EXPECT_INT(polygon.count, 4);
            EXPECT_NEAR(tl_polygon_area(&polygon), 10000, 1e-9);
            tl_polygon_free(&polygon);
        }
        IT("находит точку для подписи внутри Г-образной комнаты")
        {
            TlPolygon room = l_shape();
            EXPECT(tl_polygon_contains(&room, tl_polygon_label_point(&room)));
            tl_polygon_free(&room);
        }
        IT("строит выпуклую оболочку набора точек")
        {
            const TlVec2 points[] = {{0, 0}, {10, 0}, {5, 5}, {10, 10}, {0, 10}, {3, 7}};
            TlPolygon hull = tl_convex_hull(points, 6);
            EXPECT_INT(hull.count, 4);
            EXPECT_NEAR(tl_polygon_area(&hull), 100, 1e-9);
            tl_polygon_free(&hull);
        }
        IT("отсекает прямоугольником: кусок плитки внутри комнаты")
        {
            TlPolygon room = l_shape();
            TlRect tile = {80, 80, 140, 120};          /* плитка 60×40 на внутреннем углу */
            TlPolygon piece = tl_polygon_clip_rect(&room, tile);
            /* Плитка 2400 мм², из неё в вырезе лежит 40×20 = 800. */
            EXPECT_NEAR(tl_polygon_area(&piece), 1600, 1e-6);
            tl_polygon_free(&piece);
            TlPolygon outside = tl_polygon_clip_rect(&room, (TlRect){300, 300, 400, 400});
            EXPECT_NEAR(tl_polygon_area(&outside), 0, 1e-9);
            tl_polygon_free(&outside);
            tl_polygon_free(&room);
        }
    }

    DESCRIBE("Аффинное преобразование")
    {
        IT("поворачивает на 90° вокруг заданного начала и обращается обратно")
        {
            TlAffine local_to_world = tl_affine_rotation_translation(tl_vec2(100, 50), 90);
            TlVec2 world = tl_affine_apply(&local_to_world, tl_vec2(10, 0));
            EXPECT_NEAR(world.x, 100, 1e-9);
            EXPECT_NEAR(world.y, 60, 1e-9);
            TlAffine world_to_local = tl_affine_inverted(&local_to_world);
            TlVec2 back = tl_affine_apply(&world_to_local, world);
            EXPECT_NEAR(back.x, 10, 1e-9);
            EXPECT_NEAR(back.y, 0, 1e-9);
        }
    }
}

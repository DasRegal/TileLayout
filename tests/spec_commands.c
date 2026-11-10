#include "spec.h"
#include "specs.h"

#include <math.h>

#include "tl_project.h"

static bool contains_partition(const TlProject *project, const TlPartition *partition)
{
    for (size_t i = 0; i < project->partitions.count; i++)
        if (project->partitions.items[i] == partition)
            return true;
    return false;
}

void spec_commands(void)
{
    DESCRIBE("Добавление на план")
    {
        IT("контур на пустом месте — дом; внутри дома — комната из перегородок с внутренним размером как нарисовано")
        {
            TlProject *project = tl_project_new();
            const TlVec2 house[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            EXPECT_INT(tl_project_add_contour(project, house, 4, 250, 100), TL_CONTOUR_HOUSE);
            EXPECT_INT((int)project->outlines.count, 1);
            EXPECT_INT((int)project->spaces.count, 1);
            const TlVec2 room[] = {{0, 0}, {2000, 0}, {2000, 1500}, {0, 1500}}; /* в углу дома */
            EXPECT_INT(tl_project_add_contour(project, room, 4, 250, 100), TL_CONTOUR_ROOM);
            EXPECT_INT((int)project->partitions.count, 2); /* две стороны легли на стены */
            EXPECT_INT((int)project->spaces.count, 2);
            TlSpace *small = tl_project_space_at(project, tl_vec2(1000, 700));
            EXPECT(small != NULL);
            if (small)
                EXPECT_NEAR(tl_space_area(small), 2000.0 * 1500.0, 1);
            tl_project_free(project);
        }
        IT("перегородка по прямой продолжает другую — сливается, возвращается живая перегородка")
        {
            TlProject *project = tl_project_new();
            const TlVec2 house[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            tl_project_add_contour(project, house, 4, 250, 100);
            TlPartition *first = tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 2000), 100);
            EXPECT(first && contains_partition(project, first));
            TlPartition *second = tl_project_draw_partition(project, tl_vec2(3000, 2000), tl_vec2(3000, 4000), 100);
            EXPECT_INT((int)project->partitions.count, 1);
            EXPECT(second && contains_partition(project, second));
            if (second)
                EXPECT_NEAR(tl_partition_length(second), 4000, 1e-6);
            EXPECT_INT((int)project->spaces.count, 2);
            EXPECT(!tl_project_draw_partition(project, tl_vec2(10, 10), tl_vec2(10.5, 10), 100)); /* короче 1 мм */
            tl_project_free(project);
        }
        IT("проём на перегородке: стык полов — посередине; на короткой стене не ставится")
        {
            TlProject *project = tl_project_new();
            const TlVec2 house[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            tl_project_add_contour(project, house, 4, 250, 100);
            tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 4000), 100);
            TlBand band;
            EXPECT(tl_project_find_band(project, tl_vec2(3000, 2000), 10, &band));
            EXPECT(band.host.kind == TL_HOST_PARTITION);
            double along;
            tl_band_local(&band, tl_vec2(3000, 2000), &along, NULL);
            TlOpening *door = tl_project_place_opening(project, &band, along, 800);
            EXPECT(door != NULL);
            EXPECT_INT((int)project->openings.count, 1);
            TlBand narrow = band;
            narrow.length = 500;
            EXPECT(!tl_project_place_opening(project, &narrow, 250, 800));
            EXPECT_INT((int)project->openings.count, 1);
            tl_project_free(project);
        }
        IT("по числам: угол дома, точка узла, длина стены, проём от края стены, стык полов")
        {
            TlProject *project = tl_project_new();
            const TlVec2 house[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            tl_project_add_contour(project, house, 4, 200, 100);
            TlOutline *outline = project->outlines.items[0];
            EXPECT(tl_project_set_vertex(project, outline, 2, tl_vec2(6500, 4500)));
            EXPECT_NEAR(outline->inner.items[2].x, 6500, 1e-9);
            EXPECT(tl_project_set_vertex(project, outline, 2, tl_vec2(6000, 4000)));
            EXPECT(tl_project_set_wall_length(project, outline, 0, 7000, true, true)); /* верхняя стена длиннее */
            EXPECT_NEAR(tl_outline_inner_length(outline, 0), 7000, 1e-6);
            EXPECT_NEAR(tl_outline_inner_length(outline, 2), 7000, 1e-6); /* углы сохранились — и нижняя */
            outline->locked.items[0] = true;
            EXPECT(!tl_project_set_wall_length(project, outline, 0, 6000, true, true));
            outline->locked.items[0] = false;

            TlPartition *a = tl_project_draw_partition(project, tl_vec2(3000, 0), tl_vec2(3000, 2000), 100);
            tl_project_draw_partition(project, tl_vec2(3000, 2000), tl_vec2(7000, 2000), 100);
            TlNode node;
            EXPECT(tl_project_node_of(project, a, TL_PARTITION_END, &node));
            EXPECT(tl_project_set_node_point(project, &node, 0, tl_vec2(2500, 2500)));
            EXPECT(tl_project_node_of(project, a, TL_PARTITION_END, &node));
            EXPECT_NEAR(tl_node_center(&node).x, 2500, 0.5); /* середина узла — точно в точке */
            EXPECT_NEAR(tl_node_center(&node).y, 2500, 0.5);

            TlBand band;
            EXPECT(tl_project_find_band(project, tl_vec2(5000, -100), 10, &band)); /* верхняя стена */
            double along;
            tl_band_local(&band, tl_vec2(5000, -100), &along, NULL);
            TlOpening *door = tl_project_place_opening(project, &band, along, 800);
            EXPECT(door != NULL);
            if (!door) {
                tl_project_free(project);
                return;
            }
            EXPECT(tl_project_place_opening_at(project, door, 300, false));
            tl_project_opening_host(project, door, &band);
            tl_band_local(&band, door->center, &along, NULL);
            EXPECT_NEAR(along, 300 + 400, 1e-6);
            EXPECT(tl_project_set_threshold(project, door, 0.9, 0.4)); /* конец ушёл бы за грань — ограничено */
            EXPECT_NEAR(door->threshold_end, 1, 1e-9);
            EXPECT_NEAR(door->threshold, 0.6, 1e-9);
            tl_project_free(project);
        }
        IT("проём у угла двух перегородок: «от начала стены» — до внешнего угла, размеры — до углов на каждом боку")
        {
            TlProject *project = tl_project_new();
            const TlVec2 house[] = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
            tl_project_add_contour(project, house, 4, 200, 123);
            tl_project_draw_partition(project, tl_vec2(2000, 4000), tl_vec2(2000, 1500), 123);
            tl_project_draw_partition(project, tl_vec2(2000, 1500), tl_vec2(6000, 1500), 123);
            TlBand band;
            EXPECT(tl_project_find_band(project, tl_vec2(3000, 1500), 10, &band));
            double along;
            tl_band_local(&band, tl_vec2(3000, 1500), &along, NULL);
            TlOpening *door = tl_project_place_opening(project, &band, along, 800);
            EXPECT(door != NULL);
            if (!door) {
                tl_project_free(project);
                return;
            }
            tl_project_opening_host(project, door, &band);
            tl_band_local(&band, door->center, &along, NULL);
            double start, end;
            EXPECT(tl_project_opening_span(project, door, &start, &end));
            EXPECT_NEAR(along - 400 - start, 661.5, 1e-6); /* до внешнего угла, а не до оси (600) */
            double before[2], after[2];
            const int sides[2] = {+1, -1};
            for (int i = 0; i < 2; i++)
                EXPECT(tl_project_opening_gaps(project, &band, sides[i], door, along - 400, along + 400, &before[i],
                                               &after[i]));
            double outer = fmax(along - 400 - before[0], along - 400 - before[1]);
            double inner = fmin(along - 400 - before[0], along - 400 - before[1]);
            EXPECT_NEAR(outer, 661.5, 1e-6);
            EXPECT_NEAR(inner, 538.5, 1e-6);
            EXPECT(tl_project_place_opening_at(project, door, 300, false));
            tl_band_local(&band, door->center, &along, NULL);
            EXPECT_NEAR(along - 400 - start, 300, 1e-6);
            tl_project_free(project);
        }
    }
}

#include "spec.h"
#include "specs.h"

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
    }
}

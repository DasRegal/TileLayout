#include <math.h>
#include <stdint.h>

#include "spec.h"
#include "specs.h"

#include "tl_project.h"

void spec_dimensions(void)
{
    DESCRIBE("Размеры числом: линейка и стороны комнаты")
    {
        IT("длина линейки: начало на месте, конец — по той же прямой")
        {
            TlRuler *ruler = tl_ruler_new(tl_vec2(100, 100), tl_vec2(400, 500)); /* 500 мм */
            EXPECT(tl_ruler_set_length(ruler, 1000));
            EXPECT(fabs(ruler->start.x - 100) < 1e-9 && fabs(ruler->start.y - 100) < 1e-9);
            EXPECT(fabs(ruler->end.x - 700) < 1e-9 && fabs(ruler->end.y - 900) < 1e-9);
            EXPECT(!tl_ruler_set_length(ruler, 0));
            EXPECT(!tl_ruler_set_length(ruler, NAN));
            tl_ruler_free(ruler);
        }
        IT("сторона комнаты во всю стену дома — её длина как у стены; у перегородки — нет")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3000), 100));
            tl_project_update_spaces(project);
            EXPECT_INT(project->spaces.count, 2);
            int whole = 0, partial = 0;
            for (size_t s = 0; s < project->spaces.count; s++) {
                const TlPolygon *room = tl_space_room_outline(project->spaces.items[s]);
                for (size_t e = 0; e < room->count; e++) {
                    TlOutline *outline = NULL;
                    size_t wall = SIZE_MAX;
                    if (tl_project_room_edge_wall(project, project->spaces.items[s], e, &outline, &wall)) {
                        whole++;
                        EXPECT(outline == project->outlines.items[0] && (wall == 1 || wall == 3));
                    } else {
                        partial++;
                    }
                }
            }
            EXPECT_INT(whole, 2);   /* левая и правая стены дома — целиком в своих комнатах */
            EXPECT_INT(partial, 6); /* верх и низ поделены перегородкой, плюс сами стороны у перегородки */
            tl_project_free(project);
        }
    }
}

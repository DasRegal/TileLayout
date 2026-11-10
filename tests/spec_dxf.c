#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_dxf.h"
#include "tl_layout.h"
#include "tl_project.h"
#include "tl_report.h"

static TlProject *room_project(double width, double height)
{
    TlProject *project = tl_project_new();
    const TlVec2 corners[] = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    TlTileSettings *tile = &project->spaces.items[0]->tile;
    tile->tile_width = tile->tile_height = 300;
    tile->joint_width = 2;
    tile->row_shift = 0;
    tile->offset_x = tile->offset_y = 0;
    tile->wall_gap = 0;
    tile->pattern[0] = '\0';
    tl_project_update_spaces(project);
    return project;
}

static int count(const char *text, const char *needle)
{
    int found = 0;
    for (const char *at = strstr(text, needle); at; at = strstr(at + 1, needle))
        found++;
    return found;
}

/* Пары «код — значение»: строк чётное число, коды — числа; в конце EOF. */
static bool well_formed(const char *text)
{
    int lines = count(text, "\n");
    const char *end = text + strlen(text);
    if (lines % 2 || end - text < 6 || strcmp(end - 6, "0\nEOF\n") != 0)
        return false;
    const char *line = text;
    for (int i = 0; i < lines; i += 2) {
        char *after;
        strtol(line, &after, 10);
        if (after == line || *after != '\n')
            return false;
        line = strchr(after + 1, '\n') + 1;
    }
    return true;
}

void spec_dxf(void)
{
    tl_tile_settings_reset_defaults();
    DESCRIBE("Чертежи DXF")
    {
        IT("план: стены, пол, целые плитки и подрезки по контуру — своими слоями, русский текст кодами")
        {
            TlProject *project = room_project(2000, 1300);
            tl_space_set_name(project->spaces.items[0], "Ванная");
            tl_project_add_pipe(project, tl_pipe_new(tl_vec2(500, 500), 50));
            tl_project_update_spaces(project);
            char *text = tl_dxf_plan(project);
            EXPECT(well_formed(text));
            EXPECT(strstr(text, "AC1009") != NULL);
            EXPECT_INT(count(text, "POLYLINE\n8\nWALLS\n"), 2); /* внутренний и наружный контур */
            EXPECT_INT(count(text, "POLYLINE\n8\nTILES_WHOLE\n"), 6 * 4);
            EXPECT_INT(count(text, "POLYLINE\n8\nTILES_CUT\n"), 6 + 4 + 1);
            EXPECT_INT(count(text, "TEXT\n8\nNUMBERS\n"), 35);
            EXPECT(strstr(text, "\\U+0412\\U+0430\\U+043D") != NULL); /* «Ван…» */
            EXPECT(count(text, "CIRCLE\n8\nPIPES\n") >= 1);
            EXPECT(strstr(text, "\n20\n-1300.000\n") != NULL); /* ось Y — вверх: низ комнаты — ниже нуля */
            free(text);
            tl_project_free(project);
        }
        IT("подрезки: по чертежу на карточку — плитка, куски и подпись")
        {
            TlProject *project = room_project(2000, 1300);
            TlCutCardList cards = tl_report_cut_cards(project);
            size_t parts = 0;
            for (size_t i = 0; i < cards.count; i++)
                for (size_t p = 0; p < cards.items[i].parts.count; p++)
                    parts += cards.items[i].parts.items[p].shapes.count;
            char *text = tl_dxf_cuts(project);
            EXPECT(cards.count > 0);
            EXPECT(well_formed(text));
            EXPECT_INT(count(text, "TEXT\n8\nTITLES\n"), (int)cards.count);
            EXPECT_INT(count(text, "POLYLINE\n8\nTILES_WHOLE\n"), (int)cards.count);
            EXPECT_INT(count(text, "POLYLINE\n8\nTILES_CUT\n"), (int)parts);
            free(text);
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
    }
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"
#include "specs.h"

#include "tl_dxf.h"
#include "tl_history.h"
#include "tl_project.h"
#include "tl_project_io.h"
#include "tl_snap.h"
#include "tl_underlay.h"

/* Комната 4 × 3 м в метрах: замкнутая ломаная, дверь — блок с дугой, вставленный дважды. */
static const char *const PLAN_DXF =
    "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n6\n0\nENDSEC\n"
    "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nDOOR\n10\n0\n20\n0\n"
    "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n0.8\n21\n0\n"
    "0\nARC\n8\n0\n10\n0\n20\n0\n40\n0.8\n50\n0\n51\n90\n0\nENDBLK\n0\nENDSEC\n"
    "0\nSECTION\n2\nENTITIES\n"
    "0\nLWPOLYLINE\n8\nWALLS\n90\n4\n70\n1\n10\n0\n20\n0\n10\n4\n20\n0\n10\n4\n20\n3\n10\n0\n20\n3\n"
    "0\nINSERT\n2\nDOOR\n10\n1\n20\n0\n"
    "0\nINSERT\n2\nDOOR\n10\n3\n20\n3\n50\n180\n"
    "0\nLWPOLYLINE\n90\n2\n70\n0\n10\n1\n20\n1\n42\n1\n10\n3\n20\n1\n"
    "0\nENDSEC\n0\nEOF\n";

static TlRect lines_bounds(const TlUnderlay *underlay)
{
    TlRect bounds = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (size_t i = 0; i < underlay->lines.count; i++) {
        TlRect b = tl_polygon_bounds(&underlay->lines.items[i]);
        bounds = (TlRect){fmin(bounds.left, b.left), fmin(bounds.top, b.top), fmax(bounds.right, b.right),
                          fmax(bounds.bottom, b.bottom)};
    }
    return bounds;
}

void spec_underlay(void)
{
    DESCRIBE("Подложка: фото или DXF-план, калибровка масштаба")
    {
        IT("DXF: ломаные, дуги, блоки; метры — в мм, ось Y — вниз, угол чертежа — в (0, 0)")
        {
            TlUnderlay underlay = tl_underlay_default();
            char error[160] = "";
            EXPECT(tl_underlay_load_dxf(&underlay, PLAN_DXF, error, sizeof error));
            EXPECT_INT(underlay.lines.count, 1 + 2 * 2 + 1);
            const TlPolygon *room = &underlay.lines.items[0];
            EXPECT_INT(room->count, 5); /* замкнутая — с повтором первой точки */
            TlRect bounds = lines_bounds(&underlay);
            EXPECT(fabs(bounds.left) < 1e-6 && fabs(bounds.top) < 1e-6);
            EXPECT(fabs(bounds.right - 4000) < 1e-6 && fabs(bounds.bottom - 3000) < 1e-6);
            /* левый нижний угол комнаты (0, 0 в DXF) — внизу: y = 3000 */
            EXPECT(fabs(room->items[0].x) < 1e-6 && fabs(room->items[0].y - 3000) < 1e-6);
            /* полуокружность из выпуклости 1: её середина — на 1 м ниже хорды (y DXF = 0) */
            const TlPolygon *arc = &underlay.lines.items[1]; /* сначала линии чертежа, потом блоки */
            bool reaches = false;
            for (size_t i = 0; i < arc->count; i++)
                reaches = reaches || (fabs(arc->items[i].x - 2000) < 1 && fabs(arc->items[i].y - 3000) < 1);
            EXPECT(reaches);
            /* вторая дверь повёрнута на 180°: её створка — от (3, 3) влево */
            TlRect door = tl_polygon_bounds(&underlay.lines.items[4]);
            EXPECT(fabs(door.left - 2200) < 1e-6 && fabs(door.right - 3000) < 1e-6);
            tl_underlay_clear(&underlay);
        }
        IT("не DXF и чертёж без линий — понятная ошибка, подложка не меняется")
        {
            TlUnderlay underlay = tl_underlay_default();
            char error[160] = "";
            EXPECT(!tl_underlay_load_dxf(&underlay, "привет", error, sizeof error));
            EXPECT(strstr(error, "DXF") != NULL);
            EXPECT(!tl_underlay_load_dxf(&underlay, "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n", error, sizeof error));
            EXPECT(strstr(error, "нет линий") != NULL);
            EXPECT(!tl_underlay_present(&underlay));
        }
        IT("калибровка по двум точкам: масштаб меняется, первая точка остаётся на месте")
        {
            TlUnderlay underlay = tl_underlay_default();
            EXPECT(tl_underlay_set_image(&underlay, "data:image/png;base64,AAAA", 800, 600));
            underlay.origin = tl_vec2(100, 200);
            TlVec2 a = tl_underlay_to_plan(&underlay, tl_vec2(100, 100));
            TlVec2 b = tl_underlay_to_plan(&underlay, tl_vec2(500, 100)); /* 400 px — это 3 м */
            EXPECT(tl_underlay_calibrate(&underlay, a, b, 3000));
            EXPECT(fabs(underlay.scale - 7.5) < 1e-9);
            TlVec2 a2 = tl_underlay_to_plan(&underlay, tl_vec2(100, 100));
            EXPECT(fabs(a2.x - a.x) < 1e-9 && fabs(a2.y - a.y) < 1e-9);
            EXPECT(fabs(tl_distance(a2, tl_underlay_to_plan(&underlay, tl_vec2(500, 100))) - 3000) < 1e-6);
            EXPECT(!tl_underlay_calibrate(&underlay, a, a, 3000));
            tl_underlay_clear(&underlay);
        }
        IT("подложка сохраняется в файле; в истории правок — только положение, отмена содержимое не теряет")
        {
            TlProject *project = tl_project_new();
            TlHistory *history = tl_history_new();
            char error[160] = "";
            EXPECT(tl_underlay_load_dxf(&project->underlay, PLAN_DXF, error, sizeof error));
            tl_history_checkpoint(history, project, "", 0);
            project->underlay.origin = tl_vec2(500, 700);
            char *snapshot = tl_project_snapshot(project);
            EXPECT(strstr(snapshot, "\"lines\"") == NULL);
            free(snapshot);
            EXPECT(tl_history_undo(history, project));
            EXPECT_INT(project->underlay.lines.count, 6);
            EXPECT(fabs(project->underlay.origin.x) < 1e-9);
            EXPECT(tl_history_redo(history, project));
            EXPECT(fabs(project->underlay.origin.x - 500) < 1e-9);
            char *text = tl_project_to_string(project);
            TlProject *loaded = tl_project_new();
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->underlay.lines.count, 6);
            EXPECT(fabs(loaded->underlay.origin.y - 700) < 1e-9);
            free(text);
            tl_project_free(loaded);
            tl_history_free(history);
            tl_project_free(project);
        }
        IT("привязка: к углам и линиям DXF-подложки, пока она видна")
        {
            TlProject *project = tl_project_new();
            char error[160] = "";
            EXPECT(tl_underlay_load_dxf(&project->underlay, PLAN_DXF, error, sizeof error));
            TlSnapContext context = {.project = project, .tolerance = 50};
            TlSnapResult corner = tl_snap_wall_point(&context, tl_vec2(3980, 2970), NULL, false);
            EXPECT(corner.on_vertex && fabs(corner.point.x - 4000) < 1e-6 && fabs(corner.point.y - 3000) < 1e-6);
            TlSnapResult on_line = tl_snap_wall_point(&context, tl_vec2(2500, 20), NULL, false);
            EXPECT(fabs(on_line.point.y) < 1e-6 && fabs(on_line.point.x - 2500) < 1e-6);
            project->underlay.visible = false;
            TlSnapResult hidden = tl_snap_wall_point(&context, tl_vec2(3980, 2970), NULL, false);
            EXPECT(!hidden.on_vertex);
            tl_project_free(project);
        }
        IT("свой чертёж плана (tl_dxf_plan) читается как подложка — стены на месте")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {3000, 0}, {3000, 2000}, {0, 2000}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_update_spaces(project);
            char *text = tl_dxf_plan(project);
            TlUnderlay underlay = tl_underlay_default();
            char error[160] = "";
            EXPECT(tl_underlay_load_dxf(&underlay, text, error, sizeof error));
            TlRect bounds = lines_bounds(&underlay);
            EXPECT(fabs(tl_rect_width(bounds) - 3400) < 1 && fabs(tl_rect_height(bounds) - 2400) < 1);
            free(text);
            tl_underlay_clear(&underlay);
            tl_project_free(project);
        }
    }
}

/*
 * Инструмент «Предмет»: щелчок — ванна, поддон, инсталляция, тумба или кухня
 * (вид — из настроек правки, поле на панели). У стены предмет встаёт к ней
 * спинкой; Shift — где щёлкнули, без этого. Инструмент остаётся включённым.
 */
#include "app_canvas_private.h"

#include "app_commands.h"
#include "app_render.h"

#define WALL_REACH_MM 400.0 /* ближе к стене — встаёт к ней */

/* Предмет, каким он встанет под курсором. */
static TlFixture placed(AppCanvas *canvas, const AppPointer *pointer)
{
    AppDocument *document = canvas->document;
    double step = document->preferences.snap_step;
    TlVec2 point = pointer->shift || step <= 0 ? pointer->world : tl_snap_grid(step, pointer->world);
    TlFixture fixture = tl_fixture_default((TlFixtureKind)document->preferences.fixture_kind, point);
    TlSpace *space = pointer->shift ? NULL : tl_project_space_at(document->project, point);
    if (space) {
        TlPolygon walls = tl_space_wall_outline(space);
        tl_fixture_snap_to_walls(&fixture, &walls, WALL_REACH_MM);
        tl_polygon_free(&walls);
    }
    return fixture;
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    if (pointer->button != 1)
        return;
    app_command_add_fixture(canvas->document, placed(canvas, pointer));
}

static void motion(AppCanvas *canvas, const AppPointer *pointer)
{
    canvas->cursor = pointer->world;
    canvas->fixture_preview = placed(canvas, pointer);
    app_canvas_redraw(canvas);
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    app_render_fixture(cr, &canvas->view, &canvas->fixture_preview, app_color_hex(APP_BLUE), true);
}

const AppTool APP_FIXTURE_TOOL = {
    .cursor = "crosshair",
    .press = press,
    .motion = motion,
    .draw = draw,
};

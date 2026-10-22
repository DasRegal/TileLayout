/*
 * Инструменты-отрезки «Линейка» и «Деформационный шов»: клик — начало,
 * клик — конец (или протянуть мышью, или набрать длину и Enter).
 * Почти горизонтальный или вертикальный отрезок выпрямляется; концы
 * притягиваются: у линейки — к углам, серединам, граням и осям стен и к
 * швам плитки, у шва — только к поверхностям стен. Shift — без того и
 * другого. Отрезков может быть сколько угодно: инструмент остаётся включённым.
 *
 * Оба инструмента — одна «Стратегия»; различаются вариантом SegmentKind.
 */
#include "app_canvas_private.h"

#include "app_commands.h"
#include "app_draw.h"
#include "app_render.h"

#define DRAG_TO_FINISH_PX 6.0

typedef struct SegmentKind {
    TlSnapTargets targets;
    void (*add)(AppDocument *document, TlVec2 start, TlVec2 end);
    void (*draw_preview)(AppCanvas *canvas, cairo_t *cr, TlVec2 start, TlVec2 end);
} SegmentKind;

static void draw_ruler_preview(AppCanvas *canvas, cairo_t *cr, TlVec2 start, TlVec2 end)
{
    TlRuler preview = {start, end};
    app_render_ruler(cr, &canvas->view, &preview, app_color_hex(APP_BLUE), 1);
}

static void draw_joint_preview(AppCanvas *canvas, cairo_t *cr, TlVec2 start, TlVec2 end)
{
    TlExpansionJoint preview = {start, end, canvas->document->preferences.expansion_joint_gap};
    app_render_expansion_joint(cr, &canvas->view, &preview, app_color_hex(APP_BLUE));
    app_canvas_draw_dimension(canvas, cr, start, end, false, APP_BLUE);
}

static const SegmentKind RULER = {TL_SNAP_TO_ALL, app_command_add_ruler, draw_ruler_preview};
static const SegmentKind EXPANSION_JOINT = {TL_SNAP_TO_WALL_SURFACES, app_command_add_expansion_joint,
                                            draw_joint_preview};

static const SegmentKind *kind_of(const AppCanvas *canvas)
{
    return canvas->tool_id == APP_TOOL_EXPANSION_JOINT ? &EXPANSION_JOINT : &RULER;
}

static void track(AppCanvas *canvas, const AppPointer *pointer)
{
    const TlVec2 *anchor = canvas->has_segment_start ? &canvas->segment_start : NULL;
    canvas->cursor = app_canvas_measure_point(canvas, pointer->world, anchor, kind_of(canvas)->targets, pointer->shift);
    app_canvas_redraw(canvas);
}

static void finish(AppCanvas *canvas)
{
    TlVec2 start = canvas->segment_start;
    TlVec2 end = app_canvas_typed_target(canvas, start, canvas->cursor);
    canvas->has_segment_start = false;
    canvas->typed[0] = '\0';
    app_canvas_clear_guides(canvas);
    kind_of(canvas)->add(canvas->document, start, end);
    app_canvas_redraw(canvas);
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    track(canvas, pointer);
    if (canvas->has_segment_start) {
        finish(canvas);
        return;
    }
    canvas->segment_start = canvas->cursor;
    canvas->segment_press_screen = pointer->screen;
    canvas->has_segment_start = true;
    canvas->typed[0] = '\0';
}

/* Протянули мышью с зажатой кнопкой — отрезок готов при отпускании. */
static void release(AppCanvas *canvas, const AppPointer *pointer)
{
    if (canvas->has_segment_start && tl_distance(pointer->screen, canvas->segment_press_screen) > DRAG_TO_FINISH_PX)
        finish(canvas);
}

static bool key(AppCanvas *canvas, guint keyval, gunichar character)
{
    if (!canvas->has_segment_start)
        return false;
    if (app_canvas_edit_typed(canvas, keyval, character))
        return true;
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && canvas->typed[0]) {
        finish(canvas);
        return true;
    }
    return false;
}

static bool cancel(AppCanvas *canvas)
{
    if (!canvas->has_segment_start)
        return false;
    canvas->has_segment_start = false;
    canvas->typed[0] = '\0';
    app_canvas_clear_guides(canvas);
    return true;
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    if (canvas->has_segment_start) {
        TlVec2 end = app_canvas_typed_target(canvas, canvas->segment_start, canvas->cursor);
        kind_of(canvas)->draw_preview(canvas, cr, canvas->segment_start, end);
    }
    if (canvas->measure_snap.kind == TL_SNAP_NONE)
        app_canvas_draw_cross(canvas, cr, canvas->cursor);
}

#define SEGMENT_TOOL_HANDLERS                                                                                       \
    .cursor = "crosshair", .press = press, .motion = track, .release = release, .key = key, .cancel = cancel,          \
    .secondary = cancel, .draw = draw

const AppTool APP_RULER_TOOL = {SEGMENT_TOOL_HANDLERS};
const AppTool APP_EXPANSION_JOINT_TOOL = {SEGMENT_TOOL_HANDLERS};

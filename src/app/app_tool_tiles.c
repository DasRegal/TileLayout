/* Инструмент «Сдвиг плитки»: перетаскивание сетки плитки комнаты мышью. */
#include "app_canvas_private.h"

static void motion(AppCanvas *canvas, const AppPointer *pointer)
{
    if (canvas->drag.kind != APP_DRAG_NONE) {
        app_select_motion(canvas, pointer);
        return;
    }
    bool over_room = tl_project_space_at(canvas->document->project, pointer->world) != NULL;
    app_canvas_set_cursor(canvas, over_room ? "move" : "default");
}

const AppTool APP_TILES_TOOL = {
    .cursor = "default",
    .press = app_select_begin_tiles_drag,
    .motion = motion,
    .release = app_select_release,
};

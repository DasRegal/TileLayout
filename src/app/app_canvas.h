/* Канва: план, выделение, инструменты рисования и правки. */
#ifndef APP_CANVAS_H
#define APP_CANVAS_H

#include <gtk/gtk.h>

#include "app_document.h"

typedef enum AppToolId {
    APP_TOOL_SELECT,
    APP_TOOL_RECT,
    APP_TOOL_CONTOUR,
    APP_TOOL_PARTITION,
    APP_TOOL_OPENING,
    APP_TOOL_TILES,
    APP_TOOL_RULER,
    APP_TOOL_EXPANSION_JOINT,
    APP_TOOL_PIPE,
    APP_TOOL_DIVIDER,
    APP_TOOL_FIXTURE,
    APP_TOOL_DRAIN,
    APP_TOOL_UNDERLAY, /* калибровка и сдвиг подложки — с панели, без кнопки инструмента */
    APP_TOOL_COUNT
} AppToolId;

typedef struct AppCanvas AppCanvas;

typedef void (*AppCanvasToolListener)(AppToolId tool, void *user_data);
typedef void (*AppCanvasPointerListener)(TlVec2 world, void *user_data);
typedef void (*AppCanvasSpaceListener)(TlSpace *space, void *user_data);

AppCanvas *app_canvas_new(AppDocument *document);
GtkWidget *app_canvas_widget(AppCanvas *canvas);

void app_canvas_set_tool(AppCanvas *canvas, AppToolId tool);
AppToolId app_canvas_tool(const AppCanvas *canvas);
void app_canvas_on_tool_changed(AppCanvas *canvas, AppCanvasToolListener listener, void *user_data);
void app_canvas_on_pointer(AppCanvas *canvas, AppCanvasPointerListener listener, void *user_data);
/* Двойной щелчок по комнате — открыть её стены. */
void app_canvas_on_open_space(AppCanvas *canvas, AppCanvasSpaceListener listener, void *user_data);

void app_canvas_fit(AppCanvas *canvas);
/* Подложка: move — тянуть мышью, иначе — калибровка по двум точкам. */
void app_canvas_edit_underlay(AppCanvas *canvas, bool move);
void app_canvas_zoom(AppCanvas *canvas, double factor);
/* Esc: отменить построение → выйти из инструмента → снять выделение. */
void app_canvas_escape(AppCanvas *canvas);

/* Отрисовка в произвольный контекст (для снимков окна и экспорта). */
void app_canvas_render(AppCanvas *canvas, cairo_t *cr, int width, int height);

#endif

/*
 * Внутреннее устройство канвы — общее для файлов инструментов.
 *
 * Инструмент (паттерн «Стратегия») — набор обработчиков событий мыши и
 * клавиатуры и отрисовки подсказки поверх плана. Канва сама обрабатывает
 * панораму, масштаб и общие клавиши и передаёт остальное текущему
 * инструменту.
 */
#ifndef APP_CANVAS_PRIVATE_H
#define APP_CANVAS_PRIVATE_H

#include "app_canvas.h"
#include "app_snap.h"
#include "app_view.h"

typedef struct AppPointer {
    TlVec2 screen;
    TlVec2 world;
    guint button;
    bool shift;     /* без привязок */
    bool control;   /* сдвиг плитки */
} AppPointer;

typedef struct AppTool {
    const char *cursor;   /* имя курсора GDK */
    void (*activate)(AppCanvas *canvas);
    void (*press)(AppCanvas *canvas, const AppPointer *pointer);
    void (*motion)(AppCanvas *canvas, const AppPointer *pointer);
    void (*release)(AppCanvas *canvas, const AppPointer *pointer);
    void (*double_click)(AppCanvas *canvas, const AppPointer *pointer);
    /* Клавиша (набор длины и т.п.); true — обработана. */
    bool (*key)(AppCanvas *canvas, guint keyval, gunichar character);
    /* Esc: true — отменено незаконченное построение. */
    bool (*cancel)(AppCanvas *canvas);
    /* Правая кнопка: true — обработана (иначе — панорама). */
    bool (*secondary)(AppCanvas *canvas);
    void (*draw)(AppCanvas *canvas, cairo_t *cr);
} AppTool;

extern const AppTool APP_SELECT_TOOL;
extern const AppTool APP_RECT_TOOL;
extern const AppTool APP_CONTOUR_TOOL;
extern const AppTool APP_PARTITION_TOOL;
extern const AppTool APP_OPENING_TOOL;
extern const AppTool APP_TILES_TOOL;

typedef enum AppDragKind {
    APP_DRAG_NONE,
    APP_DRAG_VERTEX,
    APP_DRAG_WALL,
    APP_DRAG_HOUSE,
    APP_DRAG_PARTITION,
    APP_DRAG_OPENING,
    APP_DRAG_TILES
} AppDragKind;

/* Перетаскивание в инструменте «Выбор». */
typedef struct AppDrag {
    AppDragKind kind;
    bool moved;
    TlVec2 start;                   /* мир, где нажали */
    TlOutline *outline;
    int index;
    TlPolygon original_corners;
    TlWallRefList links;            /* смежные стены соседних домов */
    TlPolygonList links_original;
    TlPartition *partition;
    TlVec2 original_start, original_end;
    TlOpening *opening;
    TlBand band;
    double original_along;
    TlSpace *space;
    double original_offset_x, original_offset_y;
    TlEditToken *token;
} AppDrag;

struct AppCanvas {
    GtkWidget *area;
    AppDocument *document;
    AppView view;
    bool fitted;

    AppToolId tool_id;
    const AppTool *tool;
    AppSelection hover;
    TlVec2 cursor;                  /* мир; для инструментов рисования — с привязкой */

    /* панорама */
    bool panning;
    TlVec2 pan_origin_screen;
    double pan_origin_x, pan_origin_y;

    /* построения */
    TlPolygon draw_points;
    char typed[32];                 /* набранная с клавиатуры длина */
    bool has_rect_start;
    TlVec2 rect_start;
    bool has_wall_start;
    TlVec2 wall_start;
    AppSnapResult wall_snap;
    bool has_opening_preview;
    TlBand opening_band;
    double opening_along;

    AppDrag drag;

    AppCanvasToolListener tool_listener;
    void *tool_listener_data;
    AppCanvasPointerListener pointer_listener;
    void *pointer_listener_data;
};

/* ---- общее для инструментов --------------------------------------------- */

void app_canvas_redraw(AppCanvas *canvas);
void app_canvas_set_cursor(AppCanvas *canvas, const char *name);
AppSnapContext app_canvas_snap_context(AppCanvas *canvas);
/* Точка конца отрезка с учётом набранной длины. */
TlVec2 app_canvas_typed_target(const AppCanvas *canvas, TlVec2 start, TlVec2 toward);
/* Набор длины: цифры, точка/запятая, Backspace. true — клавиша съедена. */
bool app_canvas_edit_typed(AppCanvas *canvas, guint keyval, gunichar character);

/* Размер вдоль отрезка a-b (мир), зелёным или заданным цветом. */
void app_canvas_draw_dimension(AppCanvas *canvas, cairo_t *cr, TlVec2 a, TlVec2 b, bool bold,
                               const char *color_hex);
void app_canvas_draw_cross(AppCanvas *canvas, cairo_t *cr, TlVec2 world);
void app_canvas_draw_preview_polygon(AppCanvas *canvas, cairo_t *cr, const TlPolygon *polygon,
                                     bool closed);
/* Прямоугольник проёма с расстояниями до концов стены. */
void app_canvas_draw_opening_preview(AppCanvas *canvas, cairo_t *cr, const TlBand *band, double along,
                                     double width, const char *color, bool preview);
void app_canvas_start_pan(AppCanvas *canvas, TlVec2 screen);

/* Перетаскивание из «Выбора» — общее с инструментом «Сдвиг плитки». */
void app_select_begin_tiles_drag(AppCanvas *canvas, const AppPointer *pointer);
void app_select_motion(AppCanvas *canvas, const AppPointer *pointer);
void app_select_release(AppCanvas *canvas, const AppPointer *pointer);

#define APP_GREEN "#0b8043"
#define APP_BLUE "#1a73e8"

#endif

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
#include "tl_drag.h"
#include "tl_snap.h"
#include "app_view.h"
#include "tl_measure_snap.h"

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
extern const AppTool APP_RULER_TOOL;
extern const AppTool APP_EXPANSION_JOINT_TOOL;
extern const AppTool APP_PIPE_TOOL;

typedef enum AppDragKind {
    APP_DRAG_NONE,
    APP_DRAG_GEOMETRY /* предмет плана или сетка плитки — tl_drag */
} AppDragKind;

/* Перетаскивание в инструменте «Выбор». */
typedef struct AppDrag {
    AppDragKind kind;
    bool moved;
    TlVec2 start;                   /* мир, где нажали */
    TlDrag geometry;
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
    TlSnapResult wall_snap;
    bool has_opening_preview;
    TlBand opening_band;
    double opening_along;
    bool has_segment_start;
    TlVec2 segment_start;
    TlVec2 segment_press_screen;    /* где нажали: протянули — отрезок готов */
    TlSnapPoint measure_snap;       /* куда притянулся конец линейки или шва (для значка) */
    TlGuides guides;               /* вспомогательные линии выравнивания к cursor */
    TlVec2 guides_to;

    AppDrag drag;

    AppCanvasToolListener tool_listener;
    void *tool_listener_data;
    AppCanvasPointerListener pointer_listener;
    void *pointer_listener_data;
};

/* ---- общее для инструментов --------------------------------------------- */

void app_canvas_redraw(AppCanvas *canvas);
void app_canvas_set_cursor(AppCanvas *canvas, const char *name);
TlSnapContext app_canvas_snap_context(AppCanvas *canvas);
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
/* Проём с расстояниями до ближайших точек разбивки грани (концы стены, грани
   примкнувших перегородок, соседние проёмы); opening — сам проём (у будущего — NULL). */
void app_canvas_draw_opening_preview(AppCanvas *canvas, cairo_t *cr, const TlBand *band, double along,
                                     double width, const TlOpening *opening, const char *color, bool preview);
void app_canvas_start_pan(AppCanvas *canvas, TlVec2 screen);

/* Конец линейки или шва: с притягиванием к targets (если включено) и
   выравниванием по горизонтали/вертикали от anchor — другого конца (если
   включено и есть). free — без того и другого. Запоминает значок и
   вспомогательные линии. */
TlVec2 app_canvas_measure_point(AppCanvas *canvas, TlVec2 world, const TlVec2 *anchor, TlSnapTargets targets,
                                bool free);

/* Вспомогательные линии выравнивания к точке to (показываются до сброса). */
void app_canvas_show_guides(AppCanvas *canvas, const TlGuides *guides, TlVec2 to);
void app_canvas_clear_guides(AppCanvas *canvas);
void app_canvas_draw_snap_marker(AppCanvas *canvas, cairo_t *cr);

/* Перетаскивание из «Выбора» — общее с инструментом «Сдвиг плитки». */
void app_select_begin_tiles_drag(AppCanvas *canvas, const AppPointer *pointer);
void app_select_motion(AppCanvas *canvas, const AppPointer *pointer);
void app_select_release(AppCanvas *canvas, const AppPointer *pointer);

#define APP_SNAP_TOLERANCE_PX 10.0 /* притягивание курсора, px экрана */
#define APP_GREEN "#0b8043"
#define APP_BLUE "#1a73e8"

#endif

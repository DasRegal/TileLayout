#include "app_room_tabs.h"

#include <math.h>
#include <stdint.h>

#include "app_commands.h"
#include "app_icons.h"
#include "app_render.h"
#include "tl_layout.h"

#define PICK_PX 6.0   /* допуск выбора на развёртке, пиксели */
#define SNAP_PX 10.0  /* притягивание к швам и краям */
#define CLICK_PX 4.0  /* сдвиг меньше — щелчок, а не перетаскивание */

/* Что тянут на развёртке. */
typedef enum Grab {
    GRAB_NONE,
    GRAB_PAN,
    GRAB_TILES,        /* Ctrl + стена: сетка плитки этой стены */
    GRAB_PIPE,
    GRAB_PANNO,
    GRAB_RULER,        /* линейка целиком */
    GRAB_RULER_START,
    GRAB_RULER_END,
} Grab;

typedef struct RoomPage {
    AppRoomTabs *tabs;
    int space_id;
    GtkWidget *page, *area, *label;
    AppView view;
    bool fitted;                 /* вид подогнан и пользователь его не двигал */
    Grab grab;
    bool moved, checkpointed;
    size_t grab_wall;            /* стена тянутого (для плитки, отверстия, линейки) */
    size_t grab_index;
    TlVec2 press_screen, press_world;
    double pan_origin_x, pan_origin_y;
    TlVec2 grab_origin[2];       /* смещение плитки или точки предмета до перетаскивания */
    bool ruler_started;          /* инструмент «Линейка»: первый конец поставлен */
    size_t ruler_wall;
    TlVec2 ruler_start, hover;   /* hover — курсор (притянутый), в развёртке */
} RoomPage;

struct AppRoomTabs {
    AppDocument *document;
    AppCanvas *canvas;
    GtkWidget *notebook;
    GPtrArray *pages;            /* RoomPage * */
    bool switching;
};

static TlSpace *space_by_id(AppRoomTabs *tabs, int id)
{
    TlProject *project = tabs->document->project;
    for (size_t i = 0; i < project->spaces.count; i++)
        if (project->spaces.items[i]->id == id)
            return project->spaces.items[i];
    return NULL;
}

/* ---- развёртка: вид -------------------------------------------------------------- */

static void fit(RoomPage *page, TlSpace *space)
{
    app_view_fit(&page->view, app_render_walls_bounds(space), 24);
    page->fitted = true;
}

/* Выделенное на этой развёртке. */
static AppWallsMarks marks_of(RoomPage *page, TlSpace *space)
{
    const AppSelection *s = &page->tabs->document->selection;
    AppWallsMarks marks = {-1, -1, -1, -1};
    if (s->space != space)
        return marks;
    if (s->kind == APP_SELECT_ROOM_WALL)
        marks.wall = s->index;
    else if (s->kind == APP_SELECT_WALL_PIPE)
        marks.pipe = s->index;
    else if (s->kind == APP_SELECT_WALL_RULER)
        marks.ruler = s->index;
    else if (s->kind == APP_SELECT_PANNO)
        marks.panno = s->index;
    return marks;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, RoomPage *page)
{
    TlSpace *space = space_by_id(page->tabs, page->space_id);
    page->view.width = gtk_widget_get_allocated_width(widget);
    page->view.height = gtk_widget_get_allocated_height(widget);
    if (!space)
        return TRUE;
    if (page->fitted || page->view.scale <= 0)
        fit(page, space);
    AppRenderOptions options = app_render_screen_options();
    const AppViewFlags *flags = &page->tabs->document->view;
    options.show_tiles = flags->tiles;
    options.show_numbers = flags->numbers;
    options.show_dims = flags->dims;
    options.show_clips = flags->clips;
    AppWallsMarks marks = marks_of(page, space);
    app_render_walls(cr, page->tabs->document->project, space, &page->view, &options, &marks);
    if (page->ruler_started) { /* новая линейка — от первого конца к курсору */
        TlRuler preview = {tl_space_wall_to_development(space, page->ruler_wall, page->ruler_start), page->hover, ""};
        app_render_ruler(cr, &page->view, &preview, app_color_hex("#2563eb"), 1.5);
    }
    return TRUE;
}

/* ---- развёртка: предметы под курсором ---------------------------------------------- */

static TlVec2 pipe_point(TlSpace *space, const TlWallPipe *pipe, size_t *wall)
{
    *wall = tl_space_wall_of_edge(space, pipe->wall);
    return *wall == SIZE_MAX ? tl_vec2(INFINITY, INFINITY) : tl_space_wall_to_development(space, *wall, pipe->at);
}

static bool ruler_points(TlSpace *space, const TlWallRuler *ruler, size_t *wall, TlVec2 *start, TlVec2 *end)
{
    *wall = tl_space_wall_of_edge(space, ruler->wall);
    if (*wall == SIZE_MAX)
        return false;
    *start = tl_space_wall_to_development(space, *wall, ruler->start);
    *end = tl_space_wall_to_development(space, *wall, ruler->end);
    return true;
}

/* Нажатие в «Выборе»: что под курсором (screen — экран). */
static Grab pick(RoomPage *page, TlSpace *space, TlVec2 screen, size_t *index)
{
    const AppView *view = &page->view;
    for (size_t i = space->wall_pipes.count; i-- > 0;) {
        size_t wall;
        TlVec2 center = app_view_to_screen(view, pipe_point(space, &space->wall_pipes.items[i], &wall));
        double radius = fmax(space->wall_pipes.items[i].diameter * view->scale / 2, 4);
        if (wall != SIZE_MAX && tl_distance(center, screen) <= radius + PICK_PX) {
            *index = i;
            return GRAB_PIPE;
        }
    }
    for (size_t i = space->wall_rulers.count; i-- > 0;) {
        size_t wall;
        TlVec2 start, end;
        if (!ruler_points(space, &space->wall_rulers.items[i], &wall, &start, &end))
            continue;
        TlVec2 a = app_view_to_screen(view, start), b = app_view_to_screen(view, end);
        *index = i;
        if (tl_distance(a, screen) <= PICK_PX + 2)
            return GRAB_RULER_START;
        if (tl_distance(b, screen) <= PICK_PX + 2)
            return GRAB_RULER_END;
        double t;
        if (tl_point_segment_distance(screen, a, b, &t) <= PICK_PX)
            return GRAB_RULER;
    }
    TlVec2 world = app_view_to_world(view, screen);
    for (size_t i = space->pannos.count; space->walls.enabled && i-- > 0;) { /* панно на стенах */
        if (space->pannos.items[i].wall == TL_PANNO_FLOOR)
            continue;
        TlPolygon outline = tl_space_panno_outline(space, i);
        bool inside = outline.count && tl_polygon_contains(&outline, world);
        tl_polygon_free(&outline);
        if (inside) {
            *index = i;
            return GRAB_PANNO;
        }
    }
    return GRAB_NONE;
}

/* Стена под точкой развёртки (на самой стене, не в полосе пола и не в зазоре). */
static bool wall_under(TlSpace *space, TlVec2 world, size_t *wall)
{
    for (size_t w = 0; w < tl_space_wall_count(space); w++) {
        const TlRoomWall *candidate = tl_space_wall_at(space, w);
        if (world.x >= candidate->x && world.x <= candidate->x + candidate->length && world.y >= 0 &&
            world.y <= space->height) {
            *wall = w;
            return true;
        }
    }
    return false;
}

static TlVec2 snapped(RoomPage *page, TlSpace *space, TlVec2 world, bool free)
{
    return free ? world : tl_space_walls_snap(space, world, SNAP_PX / page->view.scale);
}

static void select(RoomPage *page, AppSelectionKind kind, TlSpace *space, size_t index)
{
    app_document_select(page->tabs->document, (AppSelection){.kind = kind, .space = space, .index = (int)index});
}

/* ---- развёртка: мышь ------------------------------------------------------------------ */

/* Инструменты «Линейка» и «Труба» — прямо на стене. */
static bool tool_click(RoomPage *page, TlSpace *space, TlVec2 world, bool free)
{
    AppToolId tool = app_canvas_tool(page->tabs->canvas);
    if (tool != APP_TOOL_RULER && tool != APP_TOOL_PIPE)
        return false;
    AppDocument *document = page->tabs->document;
    TlVec2 point = snapped(page, space, world, free), at;
    size_t wall;
    if (!tl_space_wall_from_development(space, point, &wall, &at)) {
        app_document_status(document, "Щёлкните по стене");
        return true;
    }
    if (tool == APP_TOOL_PIPE) {
        app_command_add_wall_pipe(document, space, wall, at, document->preferences.pipe_diameter);
        app_document_status(document, "Отверстие на стене: %.0f от левого угла, %.0f от пола", at.x, at.y);
    } else if (!page->ruler_started) {
        page->ruler_started = true;
        page->ruler_wall = wall;
        page->ruler_start = at;
        page->hover = point;
        app_document_status(document, "Второй конец линейки — на той же стене; Shift — без притягивания");
    } else {
        const TlRoomWall *room_wall = tl_space_wall_at(space, page->ruler_wall);
        TlVec2 end = {fmin(fmax(point.x - room_wall->x, 0), room_wall->length),
                      fmin(fmax(space->height - point.y, 0), space->height)};
        page->ruler_started = false;
        app_command_add_wall_ruler(document, space, page->ruler_wall, page->ruler_start, end);
    }
    gtk_widget_queue_draw(page->area);
    return true;
}

static gboolean on_press(GtkWidget *widget, GdkEventButton *event, RoomPage *page)
{
    TlSpace *space = space_by_id(page->tabs, page->space_id);
    if (!space)
        return TRUE;
    gtk_widget_grab_focus(widget);
    TlVec2 screen = {event->x, event->y}, world = app_view_to_world(&page->view, screen);
    if (event->type == GDK_2BUTTON_PRESS) { /* двойной щелчок — вписать */
        page->fitted = true;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    if (event->type != GDK_BUTTON_PRESS)
        return TRUE;
    page->press_screen = screen;
    page->press_world = world;
    page->moved = page->checkpointed = false;
    page->pan_origin_x = page->view.pan_x;
    page->pan_origin_y = page->view.pan_y;
    page->grab = GRAB_PAN;
    if (event->button != 1 || app_canvas_tool(page->tabs->canvas) != APP_TOOL_SELECT)
        return TRUE; /* средняя и правая — сдвиг; инструмент — по отпусканию */
    size_t index;
    Grab grab = pick(page, space, screen, &index);
    if (grab == GRAB_PIPE) {
        page->grab = grab;
        page->grab_index = index;
        size_t wall;
        page->grab_origin[0] = pipe_point(space, &space->wall_pipes.items[index], &wall);
        select(page, APP_SELECT_WALL_PIPE, space, index);
    } else if (grab == GRAB_PANNO) {
        page->grab = grab;
        page->grab_index = index;
        const TlPanno *panno = &space->pannos.items[index];
        size_t wall = tl_space_wall_of_edge(space, panno->wall);
        page->grab_origin[0] = tl_space_wall_to_development(space, wall, panno->at); /* левый нижний угол */
        select(page, APP_SELECT_PANNO, space, index);
    } else if (grab != GRAB_NONE) {
        page->grab = grab;
        page->grab_index = index;
        page->grab_origin[0] = space->wall_rulers.items[index].start;
        page->grab_origin[1] = space->wall_rulers.items[index].end;
        select(page, APP_SELECT_WALL_RULER, space, index);
    } else if ((event->state & GDK_CONTROL_MASK) && wall_under(space, world, &index)) {
        page->grab = GRAB_TILES;
        page->grab_wall = index;
        page->grab_origin[0] = tl_space_wall_offset(space, index);
        select(page, APP_SELECT_ROOM_WALL, space, index);
    }
    return TRUE;
}

/* Тянут предмет или плитку: правка модели на ходу, отмена — одним шагом. */
static void drag_to(RoomPage *page, TlSpace *space, TlVec2 world, bool free)
{
    AppDocument *document = page->tabs->document;
    if (!page->checkpointed) {
        app_document_checkpoint(document, "");
        page->checkpointed = true;
    }
    TlVec2 delta = tl_vec2_sub(world, page->press_world);
    if (page->grab == GRAB_TILES) {
        TlVec2 origin = page->grab_origin[0];
        tl_space_set_wall_offset(space, page->grab_wall, tl_vec2(origin.x + delta.x, origin.y - delta.y));
        app_document_status(document, "Плитка стены сдвинута на %.0f × %.0f мм", origin.x + delta.x,
                            origin.y - delta.y);
    } else if (page->grab == GRAB_PANNO) { /* угол панно притягивается к швам, углам, полу */
        TlPanno *panno = &space->pannos.items[page->grab_index];
        size_t wall;
        TlVec2 at, target = snapped(page, space, tl_vec2_add(page->grab_origin[0], delta), free);
        if (tl_space_wall_from_development(space, target, &wall, &at)) {
            panno->wall = tl_space_wall_at(space, wall)->edge;
            panno->at = at;
        }
    } else if (page->grab == GRAB_PIPE) {
        TlWallPipe *pipe = &space->wall_pipes.items[page->grab_index];
        size_t wall;
        TlVec2 at, target = snapped(page, space, tl_vec2_add(page->grab_origin[0], delta), free);
        if (tl_space_wall_from_development(space, target, &wall, &at)) {
            pipe->wall = tl_space_wall_at(space, wall)->edge;
            pipe->at = at;
        }
    } else { /* линейка: конец — с притягиванием, целиком — вдоль своей стены */
        TlWallRuler *ruler = &space->wall_rulers.items[page->grab_index];
        size_t wall = tl_space_wall_of_edge(space, ruler->wall);
        const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
        if (!room_wall)
            return;
        TlVec2 shift = {delta.x, -delta.y};
        for (int end = 0; end < 2; end++) {
            bool moving = page->grab == GRAB_RULER || (end == 0) == (page->grab == GRAB_RULER_START);
            if (!moving)
                continue;
            TlVec2 at = tl_vec2_add(page->grab_origin[end], shift);
            if (page->grab != GRAB_RULER) {
                TlVec2 point = snapped(page, space, tl_space_wall_to_development(space, wall, at), free);
                at = tl_vec2(point.x - room_wall->x, space->height - point.y);
            }
            at = tl_vec2(fmin(fmax(at.x, 0), room_wall->length), fmin(fmax(at.y, 0), space->height));
            *(end == 0 ? &ruler->start : &ruler->end) = at;
        }
    }
    app_document_notify(document, APP_CHANGED_MODEL);
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, RoomPage *page)
{
    TlSpace *space = space_by_id(page->tabs, page->space_id);
    if (!space)
        return FALSE;
    TlVec2 screen = {event->x, event->y}, world = app_view_to_world(&page->view, screen);
    bool free = event->state & GDK_SHIFT_MASK;
    if (page->ruler_started) {
        page->hover = snapped(page, space, world, free);
        gtk_widget_queue_draw(widget);
    }
    if (page->grab == GRAB_NONE)
        return FALSE;
    page->moved = page->moved || tl_distance(screen, page->press_screen) > CLICK_PX;
    if (!page->moved)
        return TRUE;
    if (page->grab == GRAB_PAN) {
        page->view.pan_x = page->pan_origin_x + screen.x - page->press_screen.x;
        page->view.pan_y = page->pan_origin_y + screen.y - page->press_screen.y;
        page->fitted = false;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    drag_to(page, space, world, free);
    return TRUE;
}

static gboolean on_release(GtkWidget *widget, GdkEventButton *event, RoomPage *page)
{
    TlSpace *space = space_by_id(page->tabs, page->space_id);
    Grab grab = page->grab;
    page->grab = GRAB_NONE;
    if (!space || event->button != 1)
        return TRUE;
    if (page->moved) {
        if (grab != GRAB_PAN)
            app_document_commit(page->tabs->document);
        return TRUE;
    }
    TlVec2 world = app_view_to_world(&page->view, (TlVec2){event->x, event->y});
    if (tool_click(page, space, world, event->state & GDK_SHIFT_MASK))
        return TRUE;
    size_t wall;
    if (grab == GRAB_PAN) /* щелчок по стене — стена, мимо — комната */
        select(page, wall_under(space, world, &wall) ? APP_SELECT_ROOM_WALL : APP_SELECT_SPACE, space,
               wall_under(space, world, &wall) ? wall : 0);
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, RoomPage *page)
{
    double factor = 1;
    if (event->direction == GDK_SCROLL_UP)
        factor = 1.15;
    else if (event->direction == GDK_SCROLL_DOWN)
        factor = 1 / 1.15;
    else if (event->direction == GDK_SCROLL_SMOOTH)
        factor = pow(1.15, -event->delta_y);
    app_view_zoom_at(&page->view, factor, tl_vec2(event->x, event->y));
    page->fitted = false;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

/* 2D | 3D справа вверху: 3D — просмотр, его пока нет. */
static GtkWidget *mode_switch(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "linked");
    GtkWidget *flat = gtk_toggle_button_new_with_label("2D");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(flat), TRUE);
    gtk_widget_set_tooltip_text(flat, "Развёртка стен");
    GtkWidget *volume = gtk_toggle_button_new_with_label("3D");
    gtk_widget_set_sensitive(volume, FALSE);
    gtk_widget_set_tooltip_text(volume, "Объёмный просмотр — в следующей версии");
    gtk_container_add(GTK_CONTAINER(box), flat);
    gtk_container_add(GTK_CONTAINER(box), volume);
    gtk_widget_set_can_focus(flat, FALSE);
    gtk_widget_set_halign(box, GTK_ALIGN_END);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    g_object_set(box, "margin", 8, NULL);
    return box;
}

/* ---- вкладки -------------------------------------------------------------------- */

static void close_page(AppRoomTabs *tabs, RoomPage *page)
{
    int number = gtk_notebook_page_num(GTK_NOTEBOOK(tabs->notebook), page->page);
    g_ptr_array_remove(tabs->pages, page);
    if (number >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(tabs->notebook), number);
    g_free(page);
}

static void on_close_clicked(GtkButton *button, RoomPage *page) { close_page(page->tabs, page); }

static GtkWidget *tab_label(RoomPage *page, const char *name)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    page->label = gtk_label_new(name);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(close, FALSE);
    gtk_widget_set_tooltip_text(close, "Закрыть вкладку");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), page);
    gtk_box_pack_start(GTK_BOX(box), page->label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    return box;
}

static RoomPage *page_at(AppRoomTabs *tabs, int number)
{
    GtkWidget *widget = gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabs->notebook), number);
    for (guint i = 0; i < tabs->pages->len; i++) {
        RoomPage *page = g_ptr_array_index(tabs->pages, i);
        if (page->page == widget)
            return page;
    }
    return NULL;
}

void app_room_tabs_open(AppRoomTabs *tabs, TlSpace *space)
{
    for (guint i = 0; i < tabs->pages->len; i++) {
        RoomPage *page = g_ptr_array_index(tabs->pages, i);
        if (page->space_id == space->id) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs->notebook),
                                          gtk_notebook_page_num(GTK_NOTEBOOK(tabs->notebook), page->page));
            return;
        }
    }
    RoomPage *page = g_new0(RoomPage, 1);
    page->tabs = tabs;
    page->space_id = space->id;
    page->fitted = true;
    page->area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(page->area, TRUE);
    gtk_widget_add_events(page->area, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_BUTTON_PRESS_MASK |
                                          GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(page->area, "draw", G_CALLBACK(on_draw), page);
    g_signal_connect(page->area, "scroll-event", G_CALLBACK(on_scroll), page);
    g_signal_connect(page->area, "button-press-event", G_CALLBACK(on_press), page);
    g_signal_connect(page->area, "button-release-event", G_CALLBACK(on_release), page);
    g_signal_connect(page->area, "motion-notify-event", G_CALLBACK(on_motion), page);
    page->page = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(page->page), page->area);
    gtk_overlay_add_overlay(GTK_OVERLAY(page->page), mode_switch());
    gtk_widget_show_all(page->page);
    g_ptr_array_add(tabs->pages, page);
    int number = gtk_notebook_append_page(GTK_NOTEBOOK(tabs->notebook), page->page, tab_label(page, space->name));
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(tabs->notebook), page->page, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs->notebook), number);
}

TlSpace *app_room_tabs_current(AppRoomTabs *tabs)
{
    RoomPage *page = page_at(tabs, gtk_notebook_get_current_page(GTK_NOTEBOOK(tabs->notebook)));
    return page ? space_by_id(tabs, page->space_id) : NULL;
}

void app_room_tabs_tool_changed(AppRoomTabs *tabs, AppToolId tool)
{
    for (guint i = 0; i < tabs->pages->len; i++) { /* недорисованная линейка — отменить */
        RoomPage *page = g_ptr_array_index(tabs->pages, i);
        page->ruler_started = false;
        gtk_widget_queue_draw(page->area);
    }
    if (app_room_tabs_current(tabs) && tool != APP_TOOL_SELECT && tool != APP_TOOL_RULER && tool != APP_TOOL_PIPE)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs->notebook), 0); /* рисуют план — на план */
}

/* Вкладка помещения — выделить его комнату (панель покажет её стены); план — выделенное на стенах
   становится комнатой. */
static void on_switch_page(GtkNotebook *notebook, GtkWidget *widget, guint number, AppRoomTabs *tabs)
{
    RoomPage *page = page_at(tabs, (int)number);
    TlSpace *space = page ? space_by_id(tabs, page->space_id) : NULL;
    AppDocument *document = tabs->document;
    document->walls_space = space ? space->id : 0;
    if (tabs->switching)
        return;
    tabs->switching = true;
    const AppSelection *s = &document->selection;
    if (space && !(s->space == space && (s->kind == APP_SELECT_SPACE || s->kind == APP_SELECT_PANNO ||
                                         app_selection_on_walls(s))))
        app_document_select(document, (AppSelection){.kind = APP_SELECT_SPACE, .space = space});
    else if (!space && app_selection_on_walls(s))
        app_document_select(document, (AppSelection){.kind = APP_SELECT_SPACE, .space = s->space});
    else
        app_document_notify(document, APP_CHANGED_SELECTION); /* панель: стены или пол */
    tabs->switching = false;
}

/* Правка: перерисовать, переименовать, закрыть вкладки исчезнувших комнат. */
static void on_document_changed(AppDocument *document, unsigned changes, void *data)
{
    AppRoomTabs *tabs = data;
    if (!(changes & (APP_CHANGED_MODEL | APP_CHANGED_VIEW | APP_CHANGED_FILE | APP_CHANGED_SELECTION)))
        return;
    for (guint i = tabs->pages->len; i-- > 0;) {
        RoomPage *page = g_ptr_array_index(tabs->pages, i);
        TlSpace *space = space_by_id(tabs, page->space_id);
        if (!space) {
            close_page(tabs, page);
            continue;
        }
        if (g_strcmp0(gtk_label_get_text(GTK_LABEL(page->label)), space->name) != 0)
            gtk_label_set_text(GTK_LABEL(page->label), space->name);
        gtk_widget_queue_draw(page->area);
    }
}

AppRoomTabs *app_room_tabs_new(AppDocument *document, GtkWidget *plan, AppCanvas *canvas)
{
    AppRoomTabs *tabs = g_new0(AppRoomTabs, 1);
    tabs->document = document;
    tabs->canvas = canvas;
    tabs->pages = g_ptr_array_new();
    tabs->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabs->notebook), TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs->notebook), plan, gtk_label_new("План"));
    g_signal_connect(tabs->notebook, "switch-page", G_CALLBACK(on_switch_page), tabs);
    app_document_add_listener(document, on_document_changed, tabs);
    return tabs;
}

GtkWidget *app_room_tabs_widget(AppRoomTabs *tabs) { return tabs->notebook; }

#include "app_canvas_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_commands.h"
#include "app_draw.h"
#include "app_hit.h"
#include "app_render.h"
#include "tl_align.h"

#define FIT_MARGIN 70
#define ZOOM_STEP 1.15

static const AppTool *const TOOLS[APP_TOOL_COUNT] = {
    &APP_SELECT_TOOL, &APP_RECT_TOOL, &APP_CONTOUR_TOOL,
    &APP_PARTITION_TOOL, &APP_OPENING_TOOL, &APP_TILES_TOOL, &APP_RULER_TOOL,
    &APP_EXPANSION_JOINT_TOOL,
};

#define RULER_SNAP_RADIUS_PX 10.0
#define SNAP_MARKER_PX 6.5
#define COLOR_SNAP_MARKER "#e8710a"
#define GUIDE_OVERSHOOT_PX 40.0

/* ---- общее для инструментов ---------------------------------------------- */

void app_canvas_redraw(AppCanvas *canvas) { gtk_widget_queue_draw(canvas->area); }

/* Имя курсора, который система точно покажет. GTK для Windows знает только
 * курсоры из своей таблицы системных («default», «crosshair», «move»…); для
 * незнакомого имени, например «grab», он молча создаёт пустой — невидимый —
 * курсор. Там «схватить» и «тащить» показываем курсором перемещения. */
static const char *visible_cursor_name(const char *name)
{
    if (!name)
        return "default";
#ifdef G_OS_WIN32
    if (strcmp(name, "grab") == 0 || strcmp(name, "grabbing") == 0)
        return "move";
#endif
    return name;
}

void app_canvas_set_cursor(AppCanvas *canvas, const char *name)
{
    GdkWindow *window = gtk_widget_get_window(canvas->area);
    if (!window)
        return;
    GdkCursor *cursor = gdk_cursor_new_from_name(gdk_window_get_display(window), visible_cursor_name(name));
    gdk_window_set_cursor(window, cursor);
    if (cursor)
        g_object_unref(cursor);
}

AppSnapContext app_canvas_snap_context(AppCanvas *canvas)
{
    return (AppSnapContext){.project = canvas->document->project, .view = &canvas->view,
                            .grid_step = canvas->document->preferences.snap_step, .skip_vertex = -1,
                            .align = canvas->document->preferences.auto_align};
}

void app_canvas_show_guides(AppCanvas *canvas, const AppGuides *guides, TlVec2 to)
{
    canvas->guides = *guides;
    canvas->guides_to = to;
}

void app_canvas_clear_guides(AppCanvas *canvas) { canvas->guides.count = 0; }

/* Выпрямление по горизонтали/вертикали от anchor. Если конец притянут к
   линии — точка на пересечении этой линии с горизонталью/вертикалью. */
static TlVec2 align_ruler_end(AppCanvas *canvas, TlVec2 point, TlVec2 anchor, double radius)
{
    static const TlVec2 AXES[] = {{1, 0}, {0, 1}};
    TlVec2 aligned;
    size_t which;
    if (!tl_align_direction(anchor, point, AXES, 2, radius, &aligned, &which))
        return point;
    TlSnapPoint *snap = &canvas->measure_snap;
    TlVec2 crossing;
    bool on_line = snap->kind != TL_SNAP_NONE &&
                   tl_line_intersection(anchor, AXES[which], snap->point, snap->direction, &crossing) &&
                   tl_distance(crossing, point) < radius;
    if (on_line)
        aligned = crossing;
    else
        snap->kind = TL_SNAP_NONE;
    AppGuides guides = {0};
    app_guides_add(&guides, anchor, which == 1);
    app_canvas_show_guides(canvas, &guides, aligned);
    return aligned;
}

TlVec2 app_canvas_measure_point(AppCanvas *canvas, TlVec2 world, const TlVec2 *anchor, TlSnapTargets targets,
                                bool free)
{
    const AppEditPreferences *preferences = &canvas->document->preferences;
    canvas->measure_snap = (TlSnapPoint){world, TL_SNAP_NONE, {0, 0}};
    app_canvas_clear_guides(canvas);
    if (free)
        return world;
    double radius = app_view_pixels_to_mm(&canvas->view, RULER_SNAP_RADIUS_PX);
    if (preferences->measure_magnet)
        tl_measure_snap(canvas->document->project, world, radius, targets, &canvas->measure_snap);
    TlVec2 point = canvas->measure_snap.point;
    bool on_point = canvas->measure_snap.kind != TL_SNAP_NONE && !tl_snap_kind_is_line(canvas->measure_snap.kind);
    if (preferences->auto_align && anchor && !on_point)
        point = align_ruler_end(canvas, point, *anchor, radius);
    return point;
}

static void draw_guides(AppCanvas *canvas, cairo_t *cr)
{
    for (int i = 0; i < canvas->guides.count; i++) {
        const AppGuide *guide = &canvas->guides.items[i];
        TlVec2 to = guide->vertical ? tl_vec2(guide->from.x, canvas->guides_to.y)
                                    : tl_vec2(canvas->guides_to.x, guide->from.y);
        /* чуть дальше точки — чтобы линия была видна и вдоль самой линейки */
        TlVec2 beyond = tl_vec2_scale(tl_vec2_normalized(tl_vec2_sub(to, guide->from)),
                                      app_view_pixels_to_mm(&canvas->view, GUIDE_OVERSHOOT_PX));
        app_path_line(cr, &canvas->view, tl_vec2_sub(guide->from, beyond), tl_vec2_add(to, beyond));
        app_stroke_dashed(cr, app_color_hex(COLOR_SNAP_MARKER), 1, 4);
    }
}

/* Значок притяжения: квадрат — угол, треугольник — середина грани,
   ромб — пересечение швов, кружок — грань или шов. */
void app_canvas_draw_snap_marker(AppCanvas *canvas, cairo_t *cr)
{
    const TlSnapPoint *snap = &canvas->measure_snap;
    if (snap->kind == TL_SNAP_NONE)
        return;
    TlVec2 s = app_view_to_screen(&canvas->view, snap->point);
    const double r = SNAP_MARKER_PX;
    switch (snap->kind) {
    case TL_SNAP_CORNER: cairo_rectangle(cr, s.x - r, s.y - r, 2 * r, 2 * r); break;
    case TL_SNAP_WALL_MIDDLE:
        cairo_move_to(cr, s.x, s.y - r);
        cairo_line_to(cr, s.x + r, s.y + r * 0.8);
        cairo_line_to(cr, s.x - r, s.y + r * 0.8);
        cairo_close_path(cr);
        break;
    case TL_SNAP_JOINT_CROSSING:
        cairo_move_to(cr, s.x, s.y - r);
        cairo_line_to(cr, s.x + r, s.y);
        cairo_line_to(cr, s.x, s.y + r);
        cairo_line_to(cr, s.x - r, s.y);
        cairo_close_path(cr);
        break;
    default: cairo_arc(cr, s.x, s.y, r * 0.8, 0, 2 * G_PI); break;
    }
    app_stroke(cr, app_color_hex(COLOR_SNAP_MARKER), 1.3);
}

TlVec2 app_canvas_typed_target(const AppCanvas *canvas, TlVec2 start, TlVec2 toward)
{
    if (!canvas->typed[0])
        return toward;
    double length = g_ascii_strtod(canvas->typed, NULL);
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(toward, start));
    if (tl_vec2_is_zero(direction))
        direction = tl_vec2(1, 0);
    return tl_vec2_add(start, tl_vec2_scale(direction, length));
}

bool app_canvas_edit_typed(AppCanvas *canvas, guint keyval, gunichar character)
{
    size_t length = strlen(canvas->typed);
    bool has_point = strchr(canvas->typed, '.') != NULL;
    if (g_unichar_isdigit(character) || ((character == '.' || character == ',') && !has_point)) {
        if (length + 1 < sizeof canvas->typed) {
            canvas->typed[length] = g_unichar_isdigit(character) ? (char)character : '.';
            canvas->typed[length + 1] = '\0';
        }
        app_canvas_redraw(canvas);
        return true;
    }
    if (keyval == GDK_KEY_BackSpace && length) {
        canvas->typed[length - 1] = '\0';
        app_canvas_redraw(canvas);
        return true;
    }
    return false;
}

void app_canvas_draw_dimension(AppCanvas *canvas, cairo_t *cr, TlVec2 a, TlVec2 b, bool bold,
                               const char *color_hex)
{
    double length = tl_distance(a, b);
    if (length < 1)
        return;
    char text[48];
    if (bold && canvas->typed[0])
        snprintf(text, sizeof text, "⌨ %s", canvas->typed);
    else
        snprintf(text, sizeof text, "%.0f", length);
    app_render_dimension_text(cr, app_view_to_screen(&canvas->view, a), app_view_to_screen(&canvas->view, b),
                              12, text, color_hex ? color_hex : APP_GREEN, bold);
}

void app_canvas_draw_cross(AppCanvas *canvas, cairo_t *cr, TlVec2 world)
{
    TlVec2 s = app_view_to_screen(&canvas->view, world);
    cairo_move_to(cr, s.x - 8, s.y);
    cairo_line_to(cr, s.x + 8, s.y);
    cairo_move_to(cr, s.x, s.y - 8);
    cairo_line_to(cr, s.x, s.y + 8);
    app_stroke(cr, app_color_hex(APP_GREEN), 1);
}

static void draw_handle(cairo_t *cr, TlVec2 screen, double radius, bool active, const char *color)
{
    cairo_arc(cr, screen.x, screen.y, radius, 0, 2 * G_PI);
    app_set_color(cr, active ? app_color_hex(color) : app_color_hex("#ffffff"));
    cairo_fill_preserve(cr);
    app_stroke(cr, app_color_hex(color), 1.5);
}

/* Квадратный маркер угла тела перегородки. */
static void draw_corner_handle(cairo_t *cr, TlVec2 screen, bool active)
{
    double half = active ? 4.5 : 3.5;
    cairo_rectangle(cr, screen.x - half, screen.y - half, 2 * half, 2 * half);
    app_set_color(cr, active ? app_color_hex(APP_BLUE) : app_color_hex("#ffffff"));
    cairo_fill_preserve(cr);
    app_stroke(cr, app_color_hex(APP_BLUE), 1.2);
}

void app_canvas_draw_preview_polygon(AppCanvas *canvas, cairo_t *cr, const TlPolygon *polygon, bool closed)
{
    for (size_t i = 0; i < polygon->count; i++) {
        TlVec2 s = app_view_to_screen(&canvas->view, polygon->items[i]);
        (i ? cairo_line_to : cairo_move_to)(cr, s.x, s.y);
    }
    if (closed && polygon->count) {
        cairo_close_path(cr);
        app_set_color(cr, app_color_rgba(11, 128, 67, 0.12));
        cairo_fill_preserve(cr);
    }
    app_stroke_dashed(cr, app_color_hex(APP_GREEN), 2, 6);
    for (size_t i = 0; i < polygon->count; i++)
        draw_handle(cr, app_view_to_screen(&canvas->view, polygon->items[i]), 3.5, false, APP_GREEN);
}

/* ---- выделение и наведение ------------------------------------------------ */

static void fill_and_outline(cairo_t *cr, AppColor fill, const char *edge, double width)
{
    app_set_color(cr, fill);
    cairo_fill_preserve(cr);
    app_stroke(cr, app_color_hex(edge), width);
}

/* Точки узла: середина — круг, углы — квадраты; active — индекс выделенной
   (−1 — никакой). */
static void draw_node_points(AppCanvas *canvas, cairo_t *cr, const TlNode *node, int active)
{
    TlNodeCorner corners[TL_NODE_MAX];
    size_t count = tl_node_corners(node, corners);
    for (size_t k = 0; k < count; k++)
        draw_corner_handle(cr, app_view_to_screen(&canvas->view, corners[k].first), active == (int)k + 1);
    TlVec2 center = app_view_to_screen(&canvas->view, tl_node_center(node));
    draw_handle(cr, center, active == 0 ? 6 : 4.5, active == 0, APP_BLUE);
}

/* Точки примыкания у перегородки: конец, упёршийся в бок, и бок, в который упираются. */
static void draw_tee_points(AppCanvas *canvas, cairo_t *cr, const TlPartition *partition)
{
    TlTeeList tees = tl_project_tees(canvas->document->project);
    for (size_t i = 0; i < tees.count; i++)
        if (tees.items[i].branch == partition || tees.items[i].host == partition)
            draw_handle(cr, app_view_to_screen(&canvas->view, tees.items[i].point), 4.5, false, APP_BLUE);
    tl_tee_list_free(&tees);
}

/* Координаты выделенной точки — рядом с ней. */
static void draw_point_coordinates(AppCanvas *canvas, cairo_t *cr, TlVec2 world)
{
    char text[64];
    snprintf(text, sizeof text, "%.0f; %.0f", world.x, world.y);
    AppTextStyle style = {10.5, true, app_color_hex(APP_BLUE), app_color_rgba(255, 255, 255, 0.88)};
    TlVec2 at = app_view_to_screen(&canvas->view, world);
    app_text_centered(cr, text, &style, tl_vec2(at.x, at.y - 16), 0);
}

static void draw_hover(AppCanvas *canvas, cairo_t *cr)
{
    const AppSelection *hover = &canvas->hover;
    if (hover->kind == APP_SELECT_WALL) {
        TlPolygon quad = tl_outline_wall_quad(hover->outline, (size_t)hover->index, NULL);
        app_path_polygon(cr, &canvas->view, &quad);
        app_set_color(cr, app_color_rgba(80, 160, 255, 0.35));
        cairo_fill(cr);
        tl_polygon_free(&quad);
    } else if (hover->kind == APP_SELECT_PARTITION) {
        TlPolygon body = tl_project_partition_body(canvas->document->project, hover->partition, 0);
        app_path_polygon(cr, &canvas->view, &body);
        app_set_color(cr, app_color_rgba(80, 160, 255, 0.35));
        cairo_fill(cr);
        tl_polygon_free(&body);
    } else if (hover->kind == APP_SELECT_RULER) {
        app_render_ruler(cr, &canvas->view, hover->ruler, app_color_hex(APP_BLUE), 1);
    } else if (hover->kind == APP_SELECT_OPENING && hover->index >= APP_OPENING_THRESHOLD) {
        TlVec2 from, to;
        if (tl_project_opening_threshold(canvas->document->project, hover->opening, &from, &to))
            app_render_threshold(cr, &canvas->view, from, to, app_color_rgba(80, 160, 255, 1), 3);
    } else if (hover->kind == APP_SELECT_EXPANSION_JOINT) {
        app_render_expansion_joint(cr, &canvas->view, hover->expansion_joint, app_color_rgba(80, 160, 255, 1));
    } else if (hover->kind == APP_SELECT_TEE) {
        TlTee tee;
        if (app_selection_tee(hover, canvas->document->project, &tee)) {
            TlVec2 point = app_view_to_screen(&canvas->view, tee.point);
            cairo_arc(cr, point.x, point.y, 8, 0, 2 * G_PI);
            app_set_color(cr, app_color_rgba(80, 160, 255, 0.45));
            cairo_fill(cr);
        }
    } else if (hover->kind == APP_SELECT_NODE) {
        TlNode node;
        if (app_selection_node(hover, canvas->document->project, &node)) {
            TlVec2 point = app_view_to_screen(&canvas->view, app_node_point(&node, hover->index));
            cairo_arc(cr, point.x, point.y, 8, 0, 2 * G_PI);
            app_set_color(cr, app_color_rgba(80, 160, 255, 0.45));
            cairo_fill(cr);
        }
    }
}

/* Расстояния от краёв проёма до концов стены — по стороне комнаты. */
/* Расстояния от краёв проёма до ближайших точек разбивки грани: концов стены,
   граней примкнувших перегородок, соседних проёмов. У стены — по внутренней
   грани, у перегородки — по обоим бокам. */
static void draw_opening_distances(AppCanvas *canvas, cairo_t *cr, const TlBand *band, double along, double width,
                                   const TlOpening *opening, const char *color)
{
    TlProject *project = canvas->document->project;
    bool wall = band->host.kind == TL_HOST_WALL;
    const int sides[2] = {wall ? 0 : +1, -1};
    for (int s = 0; s < (wall ? 1 : 2); s++) {
        double across = wall ? band->near : sides[s] > 0 ? band->far : band->near;
        TlVec2 normal = tl_vec2_scale(band->normal, wall || sides[s] < 0 ? -1 : 1);
        double from = along - width / 2, to = along + width / 2, before, after;
        if (!tl_project_opening_gaps(project, band, sides[s], opening, from, to, &before, &after))
            continue;
        const double edges[2][2] = {{before, from}, {to, after}};
        for (int i = 0; i < 2; i++) {
            if (edges[i][1] - edges[i][0] < 1)
                continue;
            TlVec2 a = app_view_to_screen(&canvas->view, tl_band_point(band, edges[i][0], across));
            TlVec2 b = app_view_to_screen(&canvas->view, tl_band_point(band, edges[i][1], across));
            char text[32];
            snprintf(text, sizeof text, "%.0f", edges[i][1] - edges[i][0]);
            AppTextStyle style = {11.3, true, app_color_hex(color), app_color_rgba(255, 255, 255, 0.84)};
            app_text_along(cr, text, &style, a, b, normal, 12);
        }
    }
}

void app_canvas_draw_opening_preview(AppCanvas *canvas, cairo_t *cr, const TlBand *band, double along,
                                     double width, const TlOpening *opening, const char *color, bool preview)
{
    TlPolygon shape = tl_project_opening_shape(canvas->document->project, band, along, width);
    app_path_polygon(cr, &canvas->view, &shape);
    AppColor fill = preview ? app_color_rgba(11, 128, 67, 0.43) : app_color_rgba(26, 115, 232, 0.27);
    fill_and_outline(cr, fill, color, preview ? 1.5 : 2.2);
    tl_polygon_free(&shape);
    draw_opening_distances(canvas, cr, band, along, width, opening, color);
}

static void draw_selection(AppCanvas *canvas, cairo_t *cr)
{
    const AppSelection *s = &canvas->document->selection;
    AppView *view = &canvas->view;
    if (s->kind == APP_SELECT_EXPANSION_JOINT) {
        const TlExpansionJoint *joint = s->expansion_joint;
        app_render_expansion_joint(cr, view, joint, app_color_hex(APP_BLUE));
        draw_handle(cr, app_view_to_screen(view, joint->start), s->index == 1 ? 5 : 3.5, s->index == 1, APP_BLUE);
        draw_handle(cr, app_view_to_screen(view, joint->end), s->index == 2 ? 5 : 3.5, s->index == 2, APP_BLUE);
        app_canvas_draw_dimension(canvas, cr, joint->start, joint->end, false, APP_BLUE);
        return;
    }
    if (s->kind == APP_SELECT_RULER) {
        app_render_ruler(cr, view, s->ruler, app_color_hex(APP_BLUE), 1.5);
        draw_handle(cr, app_view_to_screen(view, s->ruler->start), s->index == 1 ? 5 : 3.5, s->index == 1, APP_BLUE);
        draw_handle(cr, app_view_to_screen(view, s->ruler->end), s->index == 2 ? 5 : 3.5, s->index == 2, APP_BLUE);
        return;
    }
    if (s->kind == APP_SELECT_PARTITION) {
        TlProject *project = canvas->document->project;
        TlPolygon body = tl_project_partition_body(project, s->partition, 0);
        app_path_polygon(cr, view, &body);
        fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.35), APP_BLUE, 2.2);
        /* у свободного конца — сам конец и углы тела, у конца в узле — точки узла */
        const TlPartitionEnd ENDS[2] = {TL_PARTITION_START, TL_PARTITION_END};
        for (int e = 0; e < 2; e++) {
            TlNode node;
            if (tl_project_node_of(project, s->partition, ENDS[e], &node)) {
                draw_node_points(canvas, cr, &node, -1);
                continue;
            }
            for (size_t corner = 0; corner < body.count; corner++)
                if (tl_partition_corner_end((TlPartitionCorner)corner) == ENDS[e])
                    draw_corner_handle(cr, app_view_to_screen(view, body.items[corner]),
                                       s->index == APP_PARTITION_FIRST_CORNER + (int)corner);
            int which = e + 1;
            draw_handle(cr, app_view_to_screen(view, *tl_partition_end_point(s->partition, ENDS[e])),
                        s->index == which ? 6 : 4.5, s->index == which, APP_BLUE);
        }
        tl_polygon_free(&body);
        draw_tee_points(canvas, cr, s->partition);
        app_canvas_draw_dimension(canvas, cr, s->partition->start, s->partition->end, false, APP_BLUE);
        return;
    }
    if (s->kind == APP_SELECT_TEE) {
        TlProject *project = canvas->document->project;
        TlTee tee;
        if (!app_selection_tee(s, project, &tee))
            return;
        const TlPartition *parts[2] = {tee.branch, tee.host};
        for (int i = 0; i < 2; i++) {
            TlPolygon body = tl_project_partition_body(project, parts[i], 0);
            app_path_polygon(cr, view, &body);
            fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.18), APP_BLUE, 1.2);
            tl_polygon_free(&body);
        }
        draw_handle(cr, app_view_to_screen(view, tee.point), 6, true, APP_BLUE);
        draw_point_coordinates(canvas, cr, tee.point);
        return;
    }
    if (s->kind == APP_SELECT_NODE) {
        TlProject *project = canvas->document->project;
        TlNode node;
        if (!app_selection_node(s, project, &node))
            return;
        for (size_t i = 0; i < node.count; i++) {
            TlPolygon body = tl_project_partition_body(project, node.ends[i].partition, 0);
            app_path_polygon(cr, view, &body);
            fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.18), APP_BLUE, 1.2);
            tl_polygon_free(&body);
        }
        draw_node_points(canvas, cr, &node, s->index);
        draw_point_coordinates(canvas, cr, app_node_point(&node, s->index));
        return;
    }
    TlVec2 threshold_from, threshold_to;
    if (s->kind == APP_SELECT_OPENING && s->index >= APP_OPENING_THRESHOLD &&
        tl_project_opening_threshold(canvas->document->project, s->opening, &threshold_from, &threshold_to)) {
        app_render_threshold(cr, view, threshold_from, threshold_to, app_color_hex(APP_BLUE), 3);
        bool from_active = s->index == APP_OPENING_THRESHOLD_FROM, to_active = s->index == APP_OPENING_THRESHOLD_TO;
        draw_handle(cr, app_view_to_screen(view, threshold_from), from_active ? 5.5 : 4, from_active, APP_BLUE);
        draw_handle(cr, app_view_to_screen(view, threshold_to), to_active ? 5.5 : 4, to_active, APP_BLUE);
        return;
    }
    if (s->kind == APP_SELECT_OPENING) {
        TlBand band;
        if (tl_project_opening_host(canvas->document->project, s->opening, &band)) {
            double along;
            tl_band_local(&band, s->opening->center, &along, NULL);
            app_canvas_draw_opening_preview(canvas, cr, &band, along, s->opening->width, s->opening, APP_BLUE, false);
        }
        return;
    }
    TlOutline *outline = app_selection_outline(s);
    if (!outline)
        return;
    if (s->kind == APP_SELECT_SPACE) {
        app_path_polygon_with_holes(cr, view, &s->space->outline, &s->space->holes);
        fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.14), APP_BLUE, 2.2);
    } else if (s->kind == APP_SELECT_WALL) {
        TlPolygon quad = tl_outline_wall_quad(outline, (size_t)s->index, NULL);
        app_path_polygon(cr, view, &quad);
        fill_and_outline(cr, app_color_rgba(26, 115, 232, 0.43), APP_BLUE, 2.2);
        tl_polygon_free(&quad);
    }
    for (size_t i = 0; i < outline->inner.count; i++) {
        bool active = s->kind == APP_SELECT_VERTEX && (size_t)s->index == i;
        bool hovered = canvas->hover.kind == APP_SELECT_VERTEX && canvas->hover.outline == outline &&
                       (size_t)canvas->hover.index == i;
        draw_handle(cr, app_view_to_screen(view, outline->inner.items[i]), active || hovered ? 6 : 4.5, active, APP_BLUE);
    }
}

void app_canvas_render(AppCanvas *canvas, cairo_t *cr, int width, int height)
{
    canvas->view.width = width;
    canvas->view.height = height;
    if (!canvas->fitted && width > 1) {
        app_canvas_fit(canvas);
        canvas->fitted = true;
    }
    const AppViewFlags *flags = &canvas->document->view;
    AppRenderOptions options = app_render_screen_options();
    options.show_tiles = flags->tiles;
    options.show_clips = flags->clips;
    options.show_numbers = flags->numbers;
    options.show_dims = flags->dims;
    options.show_grid = flags->grid;
    options.show_expansion_joints = flags->expansion_joints;
    options.highlighted_outline = app_selection_outline(&canvas->document->selection);
    app_render_plan(cr, canvas->document->project, &canvas->view, &options);
    if (canvas->tool_id == APP_TOOL_SELECT)
        draw_hover(canvas, cr);
    draw_selection(canvas, cr);
    if (canvas->tool->draw)
        canvas->tool->draw(canvas, cr);
    draw_guides(canvas, cr);
    app_canvas_draw_snap_marker(canvas, cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    app_canvas_render(data, cr, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));
    return FALSE;
}

/* ---- мышь ----------------------------------------------------------------- */

static AppPointer pointer_from(AppCanvas *canvas, double x, double y, guint button, GdkModifierType state)
{
    TlVec2 screen = tl_vec2(x, y);
    return (AppPointer){screen, app_view_to_world(&canvas->view, screen), button,
                        (state & GDK_SHIFT_MASK) != 0, (state & GDK_CONTROL_MASK) != 0};
}

void app_canvas_start_pan(AppCanvas *canvas, TlVec2 screen)
{
    canvas->panning = true;
    canvas->pan_origin_screen = screen;
    canvas->pan_origin_x = canvas->view.pan_x;
    canvas->pan_origin_y = canvas->view.pan_y;
    app_canvas_set_cursor(canvas, "grabbing");
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    AppCanvas *canvas = data;
    gtk_widget_grab_focus(widget);
    AppPointer pointer = pointer_from(canvas, event->x, event->y, event->button, event->state);
    if (event->type == GDK_2BUTTON_PRESS) {
        if (event->button == 1 && canvas->tool->double_click)
            canvas->tool->double_click(canvas, &pointer);
        return TRUE;
    }
    if (event->type != GDK_BUTTON_PRESS)
        return TRUE;
    if (event->button == 3 && canvas->tool->secondary && canvas->tool->secondary(canvas)) {
        app_canvas_redraw(canvas);
        return TRUE;
    }
    if (event->button == 2 || event->button == 3) {
        app_canvas_start_pan(canvas, pointer.screen);
        return TRUE;
    }
    if (event->button == 1 && canvas->tool->press)
        canvas->tool->press(canvas, &pointer);
    return TRUE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    AppCanvas *canvas = data;
    AppPointer pointer = pointer_from(canvas, event->x, event->y, 0, event->state);
    canvas->cursor = pointer.world;
    if (canvas->panning) {
        canvas->view.pan_x = canvas->pan_origin_x + pointer.screen.x - canvas->pan_origin_screen.x;
        canvas->view.pan_y = canvas->pan_origin_y + pointer.screen.y - canvas->pan_origin_screen.y;
        app_canvas_redraw(canvas);
        return TRUE;
    }
    if (canvas->tool->motion)
        canvas->tool->motion(canvas, &pointer);
    if (canvas->pointer_listener)
        canvas->pointer_listener(canvas->cursor, canvas->pointer_listener_data);
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    AppCanvas *canvas = data;
    AppPointer pointer = pointer_from(canvas, event->x, event->y, event->button, event->state);
    if (canvas->panning) {
        canvas->panning = false;
        app_canvas_set_cursor(canvas, canvas->tool->cursor);
        return TRUE;
    }
    if (event->button == 1 && canvas->tool->release)
        canvas->tool->release(canvas, &pointer);
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    AppCanvas *canvas = data;
    double factor = 1.0;
    if (event->direction == GDK_SCROLL_UP)
        factor = ZOOM_STEP;
    else if (event->direction == GDK_SCROLL_DOWN)
        factor = 1 / ZOOM_STEP;
    else if (event->direction == GDK_SCROLL_SMOOTH)
        factor = event->delta_y < 0 ? ZOOM_STEP : event->delta_y > 0 ? 1 / ZOOM_STEP : 1.0;
    app_view_zoom_at(&canvas->view, factor, tl_vec2(event->x, event->y));
    app_canvas_redraw(canvas);
    return TRUE;
}

/* ---- клавиатура ------------------------------------------------------------- */

static bool nudge_with_arrows(AppCanvas *canvas, GdkEventKey *event)
{
    double dx = 0, dy = 0;
    switch (event->keyval) {
    case GDK_KEY_Left: dx = -1; break;
    case GDK_KEY_Right: dx = 1; break;
    case GDK_KEY_Up: dy = -1; break;
    case GDK_KEY_Down: dy = 1; break;
    default: return false;
    }
    if (canvas->document->selection.kind == APP_SELECT_NONE)
        return false;
    double step = (event->state & GDK_SHIFT_MASK) ? 1.0
                  : canvas->document->preferences.snap_step > 0 ? canvas->document->preferences.snap_step
                                                                 : 1.0;
    bool tiles = (event->state & GDK_CONTROL_MASK) || canvas->tool_id == APP_TOOL_TILES;
    app_command_nudge(canvas->document, dx * step, dy * step, tiles);
    return true;
}

static bool switch_tool_by_letter(AppCanvas *canvas, gunichar character)
{
    static const struct {
        gunichar latin, cyrillic;
        int tool; /* -1 — «Вписать» */
    } LETTERS[] = {{'v', 0x043c, APP_TOOL_SELECT},  {'r', 0x043a, APP_TOOL_RECT},
                   {'p', 0x0437, APP_TOOL_CONTOUR}, {'w', 0x0446, APP_TOOL_PARTITION},
                   {'o', 0x0449, APP_TOOL_OPENING}, {'t', 0x0435, APP_TOOL_TILES},
                   {'m', 0x044c, APP_TOOL_RULER},   {'j', 0x043e, APP_TOOL_EXPANSION_JOINT},
                   {'f', 0x0430, -1}};
    gunichar lower = g_unichar_tolower(character);
    for (size_t i = 0; i < sizeof LETTERS / sizeof LETTERS[0]; i++)
        if (lower == LETTERS[i].latin || lower == LETTERS[i].cyrillic) {
            if (LETTERS[i].tool < 0)
                app_canvas_fit(canvas);
            else
                app_canvas_set_tool(canvas, (AppToolId)LETTERS[i].tool);
            return true;
        }
    return false;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    AppCanvas *canvas = data;
    gunichar character = gdk_keyval_to_unicode(event->keyval);
    if (canvas->tool->key && canvas->tool->key(canvas, event->keyval, character))
        return TRUE;
    if (event->keyval == GDK_KEY_Delete) {
        app_command_delete_selection(canvas->document);
        return TRUE;
    }
    if (nudge_with_arrows(canvas, event))
        return TRUE;
    if (!(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) && character && switch_tool_by_letter(canvas, character))
        return TRUE;
    return FALSE;
}

/* ---- публичное ---------------------------------------------------------------- */

static void on_document_changed(AppDocument *document, unsigned changes, void *data)
{
    if (changes & (APP_CHANGED_MODEL | APP_CHANGED_SELECTION | APP_CHANGED_VIEW))
        app_canvas_redraw(data);
}

AppCanvas *app_canvas_new(AppDocument *document)
{
    AppCanvas *canvas = g_new0(AppCanvas, 1);
    canvas->document = document;
    canvas->view = (AppView){0.1, 60, 60, 800, 600};
    canvas->tool = &APP_SELECT_TOOL;
    canvas->area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(canvas->area, TRUE);
    gtk_widget_set_size_request(canvas->area, 400, 300);
    gtk_widget_add_events(canvas->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                            GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
                                            GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(canvas->area, "draw", G_CALLBACK(on_draw), canvas);
    g_signal_connect(canvas->area, "button-press-event", G_CALLBACK(on_button_press), canvas);
    g_signal_connect(canvas->area, "button-release-event", G_CALLBACK(on_button_release), canvas);
    g_signal_connect(canvas->area, "motion-notify-event", G_CALLBACK(on_motion), canvas);
    g_signal_connect(canvas->area, "scroll-event", G_CALLBACK(on_scroll), canvas);
    g_signal_connect(canvas->area, "key-press-event", G_CALLBACK(on_key_press), canvas);
    app_document_add_listener(document, on_document_changed, canvas);
    return canvas;
}

GtkWidget *app_canvas_widget(AppCanvas *canvas) { return canvas->area; }

void app_canvas_set_tool(AppCanvas *canvas, AppToolId tool)
{
    canvas->tool_id = tool;
    canvas->tool = TOOLS[tool];
    tl_polygon_clear(&canvas->draw_points);
    canvas->typed[0] = '\0';
    canvas->has_rect_start = canvas->has_wall_start = canvas->has_opening_preview = false;
    canvas->has_segment_start = false;
    canvas->measure_snap.kind = TL_SNAP_NONE;
    app_canvas_clear_guides(canvas);
    canvas->wall_snap.on_face = false;
    canvas->hover = (AppSelection){0};
    if (canvas->tool->activate)
        canvas->tool->activate(canvas);
    app_canvas_set_cursor(canvas, canvas->tool->cursor);
    gtk_widget_grab_focus(canvas->area);
    if (canvas->tool_listener)
        canvas->tool_listener(tool, canvas->tool_listener_data);
    app_canvas_redraw(canvas);
}

AppToolId app_canvas_tool(const AppCanvas *canvas) { return canvas->tool_id; }

void app_canvas_on_tool_changed(AppCanvas *canvas, AppCanvasToolListener listener, void *user_data)
{
    canvas->tool_listener = listener;
    canvas->tool_listener_data = user_data;
}

void app_canvas_on_pointer(AppCanvas *canvas, AppCanvasPointerListener listener, void *user_data)
{
    canvas->pointer_listener = listener;
    canvas->pointer_listener_data = user_data;
}

void app_canvas_fit(AppCanvas *canvas)
{
    TlRect bounds;
    if (app_render_plan_bounds(canvas->document->project, &bounds))
        app_view_fit(&canvas->view, bounds, FIT_MARGIN);
    else
        canvas->view = (AppView){0.1, 60, 60, canvas->view.width, canvas->view.height};
    app_canvas_redraw(canvas);
}

void app_canvas_zoom(AppCanvas *canvas, double factor)
{
    app_view_zoom_at(&canvas->view, factor, tl_vec2(canvas->view.width / 2, canvas->view.height / 2));
    app_canvas_redraw(canvas);
}

void app_canvas_escape(AppCanvas *canvas)
{
    if (canvas->tool->cancel && canvas->tool->cancel(canvas))
        app_canvas_redraw(canvas);
    else if (canvas->tool_id != APP_TOOL_SELECT)
        app_canvas_set_tool(canvas, APP_TOOL_SELECT);
    else
        app_document_clear_selection(canvas->document);
}

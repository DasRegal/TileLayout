/*
 * Развёртка стен комнаты: стены в ряд, как видно из комнаты (tl_space_walls.c).
 * Слои: стена, плитка полос, проёмы, пол под стенами, верх облицовки, отверстия, линейки, номера
 * плиток, подписи и размеры, выделенное.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "app_draw.h"
#include "app_render.h"
#include "tl_layout.h"
#include "tl_report.h"

#define COLOR_WALL_FACE "#fbfaf6"
#define COLOR_WALL_EDGE "#2c3038"
#define COLOR_FLOOR "#6f7681"
#define COLOR_OPENING "#ffffff"
#define COLOR_OPENING_EDGE "#7a6a4f"
#define COLOR_DIM "#1f4e9c"
#define COLOR_FINISH "#8a5a00"
#define COLOR_SELECTION "#2563eb"
#define COLOR_PIPE "#3d5a80"
#define COLOR_RULER "#5f6b7a"
#define COLOR_FLOOR_STRIP "#f0ede6"

#define LABEL_SPACE_MM 500.0 /* над стенами — подписи, под полом — размеры */

TlRect app_render_walls_bounds(TlSpace *space)
{
    TlRect bounds = tl_space_walls_bounds(space);
    bounds.top -= LABEL_SPACE_MM;
    bounds.bottom += LABEL_SPACE_MM;
    bounds.left -= LABEL_SPACE_MM;
    if (tl_space_stairs_count(space) > 0) /* подпись лестницы шире её столбца */
        bounds.right += LABEL_SPACE_MM;
    return bounds;
}

static TlPolygon rect_polygon(TlRect rect)
{
    const TlVec2 corners[] = {
        {rect.left, rect.top}, {rect.right, rect.top}, {rect.right, rect.bottom}, {rect.left, rect.bottom}};
    return tl_polygon_from_points(corners, 4);
}

static void fill_rect(cairo_t *cr, const AppView *view, TlRect rect, AppColor fill, AppColor edge, double width_px)
{
    TlPolygon polygon = rect_polygon(rect);
    app_path_polygon(cr, view, &polygon);
    app_set_fill_color(cr, fill);
    cairo_fill_preserve(cr);
    app_stroke(cr, edge, width_px);
    tl_polygon_free(&polygon);
}

/* Плитка стен; reveals — только откосы (их — поверх проёмов), иначе — без них. */
static void render_wall_tiles(cairo_t *cr, const AppView *view, const AppRenderOptions *options, TlSpace *space,
                              bool reveals)
{
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
        TlWallBand band;
        TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, &band);
        if (layout->too_many || (tl_space_wall_layout_reveal(space, i) != SIZE_MAX) != reveals)
            continue;
        cairo_save(cr);
        app_render_tiles(cr, view, options, tl_space_wall_tile(space, band), layout);
        cairo_restore(cr);
    }
}

static void render_numbers(cairo_t *cr, const AppView *view, const AppRenderOptions *options, TlProject *project,
                           TlSpace *space)
{
    tl_report_assign_numbers(project);
    AppTextStyle style = {options->printing ? 8.7 : 10, true, app_color_hex("#20242a"),
                          app_color_rgba(255, 255, 255, options->printing ? 1.0 : 0.67)};
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
        TlWallBand band;
        TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, &band);
        const TlTileSettings *tile = tl_space_wall_tile(space, band);
        double smallest = fmin(tile->tile_width, tile->tile_height) * view->scale;
        if (layout->too_many || smallest < (options->printing ? 10 : 18)) /* фриз мельче номера — без номеров */
            continue;
        app_render_layout_numbers(cr, view, &style, layout);
    }
}

/* Проём: белый прямоугольник; у двери — линии открывания, подпись — имя и размеры. */
static void render_holes(cairo_t *cr, const AppView *view, TlProject *project, TlSpace *space)
{
    AppTextStyle style = {10, true, app_color_hex(COLOR_OPENING_EDGE), APP_TRANSPARENT};
    for (size_t i = 0; i < tl_space_wall_hole_count(space); i++) {
        const TlWallHole *hole = tl_space_wall_hole_at(space, i);
        if (hole->fixture < project->fixtures.count) { /* предмет закрывает стену — вид спереди */
            const TlFixture *fixture = &project->fixtures.items[hole->fixture];
            fill_rect(cr, view, hole->rect, app_color_hex("#eceef1"), app_color_hex("#5d6470"), 1.4);
            AppTextStyle name = {10, true, app_color_hex("#5d6470"), APP_TRANSPARENT};
            TlVec2 middle = {(hole->rect.left + hole->rect.right) / 2, (hole->rect.top + hole->rect.bottom) / 2};
            double text_w;
            app_text_size(tl_fixture_kind_name(fixture->kind), &name, &text_w, NULL);
            if (text_w < tl_rect_width(hole->rect) * view->scale)
                app_text_centered(cr, tl_fixture_kind_name(fixture->kind), &name, app_view_to_screen(view, middle), 0);
            continue;
        }
        fill_rect(cr, view, hole->rect, app_color_hex(COLOR_OPENING), app_color_hex(COLOR_OPENING_EDGE), 1.4);
        if (hole->opening >= project->openings.count)
            continue;
        const TlOpening *opening = project->openings.items[hole->opening];
        if (opening->kind == TL_OPENING_DOOR) { /* полотно: от низа углов к середине верха */
            TlVec2 top = {(hole->rect.left + hole->rect.right) / 2, hole->rect.top};
            app_path_line(cr, view, tl_vec2(hole->rect.left, hole->rect.bottom), top);
            app_path_line(cr, view, top, tl_vec2(hole->rect.right, hole->rect.bottom));
            app_stroke_dashed(cr, app_color_hex(COLOR_OPENING_EDGE), 0.8, 5);
        } else if (opening->kind == TL_OPENING_WINDOW) { /* рама: крест */
            TlRect r = hole->rect;
            double middle_x = (r.left + r.right) / 2, middle_y = (r.top + r.bottom) / 2;
            app_path_line(cr, view, tl_vec2(middle_x, r.top), tl_vec2(middle_x, r.bottom));
            app_path_line(cr, view, tl_vec2(r.left, middle_y), tl_vec2(r.right, middle_y));
            app_stroke(cr, app_color_hex(COLOR_OPENING_EDGE), 0.8);
        }
        char text[96];
        snprintf(text, sizeof text, "%s %.0f×%.0f", opening->name, tl_rect_width(hole->rect),
                 tl_rect_height(hole->rect));
        TlVec2 center = {(hole->rect.left + hole->rect.right) / 2, hole->rect.top + tl_rect_height(hole->rect) / 4};
        double text_w;
        app_text_size(text, &style, &text_w, NULL);
        if (text_w < tl_rect_width(hole->rect) * view->scale + 40)
            app_text_centered(cr, text, &style, app_view_to_screen(view, center), 0);
    }
}

/* Лестницы — столбцами справа от стен: подступенки и проступи снизу вверх (outline — только контуры). */
static void render_stairs(cairo_t *cr, const AppView *view, TlSpace *space, bool outline)
{
    for (size_t k = 0; k < tl_space_stairs_count(space); k++) {
        TlRect column;
        if (!tl_space_stairs_column(space, k, &column))
            continue;
        const TlFixture *stairs = &space->stairs.items[k];
        double riser = stairs->height / stairs->steps, tread = stairs->depth / stairs->steps, y = space->height;
        for (int step = 0; step < stairs->steps; step++)
            for (int part = 0; part < 2; part++) {
                double size = part ? tread : riser;
                TlRect rect = {column.left, y - size, column.right, y};
                if (outline) {
                    TlPolygon polygon = rect_polygon(rect);
                    app_path_polygon(cr, view, &polygon);
                    tl_polygon_free(&polygon);
                    app_stroke(cr, app_color_hex(part ? COLOR_WALL_EDGE : "#8a919c"), part ? 1.2 : 0.8);
                } else {
                    fill_rect(cr, view, rect, app_color_hex(part ? "#e6e1d8" : COLOR_WALL_FACE), app_color_hex(COLOR_WALL_EDGE), 0);
                }
                y -= size;
            }
        if (outline) {
            AppTextStyle name = {10, true, app_color_hex("#5d6470"), APP_TRANSPARENT};
            char text[64];
            snprintf(text, sizeof text, "Лестница");
            TlVec2 at = app_view_to_screen(view, tl_vec2((column.left + column.right) / 2, column.top));
            at.y -= 12;
            app_text_centered(cr, text, &name, at, 0);
            app_path_line(cr, view, tl_vec2(column.left - 60, space->height), tl_vec2(column.right + 60, space->height));
            app_stroke(cr, app_color_hex(COLOR_FLOOR), 3);
        }
    }
}

/* Верх облицовки и границы полос — пунктиром поперёк всех стен. */
static void render_band_lines(cairo_t *cr, const AppView *view, TlSpace *space)
{
    if (!space->walls.enabled)
        return;
    double top = tl_space_walls_finish_top(space);
    for (size_t w = 0; w < tl_space_wall_count(space); w++) {
        const TlRoomWall *wall = tl_space_wall_at(space, w);
        if (top < space->height - 0.5) {
            double y = space->height - top;
            app_path_line(cr, view, tl_vec2(wall->x, y), tl_vec2(wall->x + wall->length, y));
        }
    }
    app_stroke_dashed(cr, app_color_hex(COLOR_FINISH), 1.2, 6);
    if (top < space->height - 0.5 && tl_space_wall_count(space) > 0) {
        char text[64];
        snprintf(text, sizeof text, "облицовка до %.0f", top);
        AppTextStyle style = {10, false, app_color_hex(COLOR_FINISH), app_color_rgba(255, 255, 255, 0.8)};
        const TlRoomWall *first = tl_space_wall_at(space, 0);
        TlVec2 at = app_view_to_screen(view, tl_vec2(first->x, space->height - top));
        double text_w;
        app_text_size(text, &style, &text_w, NULL);
        app_text_centered(cr, text, &style, tl_vec2(at.x + text_w / 2 + 6, at.y - 9), 0);
    }
}

/* Под стеной — пол у неё, как будто откинут вниз: плитка пола и зон в полосе TL_WALL_FLOOR_DEPTH. */
static void render_floor_strips(cairo_t *cr, const AppView *view, const AppRenderOptions *options, TlSpace *space)
{
    const TlTileSettings *room_tile = tl_space_effective_tile(space);
    for (size_t w = 0; w < tl_space_wall_count(space); w++) {
        const TlRoomWall *wall = tl_space_wall_at(space, w);
        TlRect strip = {wall->x, space->height, wall->x + wall->length, space->height + TL_WALL_FLOOR_DEPTH};
        cairo_save(cr);
        TlPolygon polygon = rect_polygon(strip);
        app_path_polygon(cr, view, &polygon);
        tl_polygon_free(&polygon);
        app_set_fill_color(cr, app_color_hex(COLOR_FLOOR_STRIP));
        cairo_fill_preserve(cr);
        cairo_clip(cr);
        TlAffine to_development = tl_space_wall_floor_affine(space, w);
        for (size_t l = 0; options->show_tiles && room_tile->enabled && l < tl_space_floor_layout_count(space); l++) {
            TlLayout layout = *tl_space_layout_at(space, l); /* та же раскладка, оси — в развёртке */
            if (layout.too_many)
                continue;
            layout.local_to_world = tl_affine_multiply(&to_development, &layout.local_to_world);
            cairo_save(cr);
            app_render_tiles(cr, view, options, tl_space_layout_tile(space, l), &layout);
            cairo_restore(cr);
        }
        for (size_t p = 0; options->show_tiles && room_tile->enabled && p < space->pannos.count; p++)
            if (space->pannos.items[p].wall == TL_PANNO_FLOOR)
                app_render_panno(cr, view, options, space, p, &to_development);
        cairo_restore(cr);
        /* к низу — светлее: это пол, а не стена */
        TlVec2 top = app_view_to_screen(view, tl_vec2(strip.left, strip.top));
        TlVec2 bottom = app_view_to_screen(view, tl_vec2(strip.right, strip.bottom));
        cairo_pattern_t *fade = cairo_pattern_create_linear(0, top.y, 0, bottom.y);
        cairo_pattern_add_color_stop_rgba(fade, 0, 1, 1, 1, 0);
        cairo_pattern_add_color_stop_rgba(fade, 1, 1, 1, 1, options->printing ? 0.6 : 0.75);
        cairo_rectangle(cr, top.x, top.y, bottom.x - top.x, bottom.y - top.y);
        cairo_set_source(cr, fade);
        cairo_fill(cr);
        cairo_pattern_destroy(fade);
    }
}

static void render_pipes(cairo_t *cr, const AppView *view, TlSpace *space, long selected)
{
    for (size_t i = 0; i < space->wall_pipes.count; i++) {
        const TlWallPipe *pipe = &space->wall_pipes.items[i];
        size_t wall = tl_space_wall_of_edge(space, pipe->wall);
        if (wall == SIZE_MAX)
            continue;
        TlPipe shown = {tl_space_wall_to_development(space, wall, pipe->at), pipe->diameter};
        app_render_pipe(cr, view, &shown, app_color_hex((long)i == selected ? COLOR_SELECTION : COLOR_PIPE), true);
    }
}

static void render_rulers(cairo_t *cr, const AppView *view, TlSpace *space, long selected)
{
    for (size_t i = 0; i < space->wall_rulers.count; i++) {
        const TlWallRuler *ruler = &space->wall_rulers.items[i];
        size_t wall = tl_space_wall_of_edge(space, ruler->wall);
        if (wall == SIZE_MAX)
            continue;
        TlRuler shown = {tl_space_wall_to_development(space, wall, ruler->start),
                         tl_space_wall_to_development(space, wall, ruler->end), ""};
        bool chosen = (long)i == selected;
        app_render_ruler(cr, view, &shown, app_color_hex(chosen ? COLOR_SELECTION : COLOR_RULER), chosen ? 2 : 1.3);
    }
}

/* Над стеной — «Стена 1», под полом — длина; слева от первой — высота помещения. */
static void render_labels(cairo_t *cr, const AppView *view, TlSpace *space)
{
    AppTextStyle title = {12, true, app_color_hex(COLOR_WALL_EDGE), APP_TRANSPARENT};
    for (size_t w = 0; w < tl_space_wall_count(space); w++) {
        const TlRoomWall *wall = tl_space_wall_at(space, w);
        char text[64];
        snprintf(text, sizeof text, "Стена %zu", w + 1);
        TlVec2 top = app_view_to_screen(view, tl_vec2(wall->x + wall->length / 2, 0));
        app_text_centered(cr, text, &title, tl_vec2(top.x, top.y - 14), 0);
        snprintf(text, sizeof text, "%.0f", wall->length);
        double below = space->height + TL_WALL_FLOOR_DEPTH;
        app_render_dimension_text(cr, app_view_to_screen(view, tl_vec2(wall->x + wall->length, below)),
                                  app_view_to_screen(view, tl_vec2(wall->x, below)), 12,
                                  text, COLOR_DIM, false);
    }
    if (tl_space_wall_count(space) > 0) {
        char text[32];
        snprintf(text, sizeof text, "%.0f", space->height);
        app_render_dimension_text(cr, app_view_to_screen(view, tl_vec2(0, space->height)),
                                  app_view_to_screen(view, tl_vec2(0, 0)), 16, text, COLOR_DIM, false);
    }
}

void app_render_walls(cairo_t *cr, TlProject *project, TlSpace *space, const AppView *view,
                      const AppRenderOptions *options, const AppWallsMarks *marks)
{
    bool previous_mono = app_draw_is_mono();
    app_draw_set_mono(options->mono);
    if (!options->printing) {
        app_set_color(cr, app_color_hex("#f4f5f7"));
        cairo_paint(cr);
    }
    for (size_t w = 0; w < tl_space_wall_count(space); w++) {
        const TlRoomWall *wall = tl_space_wall_at(space, w);
        TlRect rect = {wall->x, 0, wall->x + wall->length, space->height};
        fill_rect(cr, view, rect, app_color_hex(COLOR_WALL_FACE), app_color_hex(COLOR_WALL_EDGE), 0);
    }
    render_stairs(cr, view, space, false);
    if (options->show_tiles)
        render_wall_tiles(cr, view, options, space, false);
    for (size_t p = 0; options->show_tiles && space->walls.enabled && p < space->pannos.count; p++)
        if (space->pannos.items[p].wall != TL_PANNO_FLOOR)
            app_render_panno(cr, view, options, space, p, NULL);
    render_floor_strips(cr, view, options, space);
    render_holes(cr, view, project, space);
    if (options->show_tiles) /* откосы — внутри проёмов, поверх них */
        render_wall_tiles(cr, view, options, space, true);
    render_band_lines(cr, view, space);
    for (size_t w = 0; w < tl_space_wall_count(space); w++) { /* контур стены и пол */
        const TlRoomWall *wall = tl_space_wall_at(space, w);
        TlPolygon polygon = rect_polygon((TlRect){wall->x, 0, wall->x + wall->length, space->height});
        app_path_polygon(cr, view, &polygon);
        app_stroke(cr, app_color_hex(COLOR_WALL_EDGE), 1.2);
        tl_polygon_free(&polygon);
        app_path_line(cr, view, tl_vec2(wall->x - 60, space->height), tl_vec2(wall->x + wall->length + 60, space->height));
        app_stroke(cr, app_color_hex(COLOR_FLOOR), 3);
    }
    render_stairs(cr, view, space, true);
    if (options->show_numbers && options->show_tiles)
        render_numbers(cr, view, options, project, space);
    const AppWallsMarks none = {-1, -1, -1, -1};
    if (!marks)
        marks = &none;
    render_pipes(cr, view, space, marks->pipe);
    render_rulers(cr, view, space, marks->ruler);
    if (marks->panno >= 0 && (size_t)marks->panno < space->pannos.count &&
        space->pannos.items[marks->panno].wall != TL_PANNO_FLOOR) {
        TlPolygon outline = tl_space_panno_outline(space, (size_t)marks->panno);
        app_path_polygon(cr, view, &outline);
        tl_polygon_free(&outline);
        app_stroke_dashed(cr, app_color_hex(COLOR_SELECTION), 2.5, 7);
    }
    const TlRoomWall *chosen = marks->wall >= 0 ? tl_space_wall_at(space, (size_t)marks->wall) : NULL;
    if (chosen) {
        TlPolygon polygon = rect_polygon((TlRect){chosen->x, 0, chosen->x + chosen->length, space->height});
        app_path_polygon(cr, view, &polygon);
        tl_polygon_free(&polygon);
        app_stroke_dashed(cr, app_color_hex(COLOR_SELECTION), 2.5, 7);
    }
    if (options->show_dims)
        render_labels(cr, view, space);
    app_draw_set_mono(previous_mono);
}

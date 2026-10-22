#include "app_render.h"

#include <math.h>
#include <stdio.h>

#include "app_draw.h"
#include "app_icons.h"
#include "tl_layout.h"
#include "tl_report.h"

#define COLOR_BACKGROUND "#f4f5f7"
#define COLOR_GRID_MINOR "#e3e6ea"
#define COLOR_GRID_MAJOR "#cfd4da"
#define COLOR_WALL "#6f7681"
#define COLOR_WALL_SELECTED "#5d6470"
#define COLOR_WALL_EDGE "#2c3038"
#define COLOR_GROUT "#8f8a82"
#define COLOR_NARROW "#e8826e"
#define COLOR_GAP "#f3eee4"
#define COLOR_OPENING_FLOOR "#ece5d5"
#define COLOR_OPENING_EDGE "#7a6a4f"
#define COLOR_CLIP "#c2410c"
#define COLOR_INNER_DIM "#1f4e9c"
#define COLOR_OUTER_DIM "#8a5a00"
#define COLOR_OPENING_DIM "#6b3fa0"

AppRenderOptions app_render_screen_options(void)
{
    return (AppRenderOptions){.show_tiles = true, .show_clips = true, .show_dims = true,
                              .show_grid = true, .show_room_labels = true, .show_locks = true};
}

bool app_render_plan_bounds(TlProject *project, TlRect *bounds)
{
    bool found = false;
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlPolygon outer = tl_outline_outer(project->outlines.items[o]);
        TlRect r = tl_polygon_bounds(&outer);
        *bounds = found ? tl_rect_united(*bounds, r) : r;
        found = true;
        tl_polygon_free(&outer);
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        TlRect r = {fmin(partition->start.x, partition->end.x), fmin(partition->start.y, partition->end.y),
                    fmax(partition->start.x, partition->end.x), fmax(partition->start.y, partition->end.y)};
        *bounds = found ? tl_rect_united(*bounds, r) : r;
        found = true;
    }
    return found;
}

/* ---- сетка ------------------------------------------------------------------ */

static void render_grid(cairo_t *cr, const AppView *view)
{
    static const double steps[] = {10, 50, 100, 500, 1000, 5000, 10000, 50000};
    const size_t step_count = sizeof steps / sizeof steps[0];
    double minor = steps[step_count - 1], major = minor * 10;
    for (size_t i = 0; i < step_count; i++)
        if (steps[i] * view->scale >= 14) {
            minor = steps[i];
            break;
        }
    major = minor * 10;
    for (size_t i = 0; i < step_count; i++)
        if (steps[i] >= minor * 5 && steps[i] * view->scale >= 70) {
            major = steps[i];
            break;
        }
    TlVec2 top_left = app_view_to_world(view, tl_vec2(0, 0));
    TlVec2 bottom_right = app_view_to_world(view, tl_vec2(view->width, view->height));
    const double spacings[2] = {minor, major};
    const char *colors[2] = {COLOR_GRID_MINOR, COLOR_GRID_MAJOR};
    for (int level = 0; level < 2; level++) {
        double s = spacings[level];
        for (double x = floor(top_left.x / s) * s; x <= bottom_right.x; x += s) {
            double sx = round(x * view->scale + view->pan_x) + 0.5;
            cairo_move_to(cr, sx, 0);
            cairo_line_to(cr, sx, view->height);
        }
        for (double y = floor(top_left.y / s) * s; y <= bottom_right.y; y += s) {
            double sy = round(y * view->scale + view->pan_y) + 0.5;
            cairo_move_to(cr, 0, sy);
            cairo_line_to(cr, view->width, sy);
        }
        app_stroke(cr, app_color_hex(colors[level]), 1);
    }
}

/* ---- пол и плитка ----------------------------------------------------------- */

/* Преобразование «оси раскладки → экран». */
static cairo_matrix_t local_to_screen(const AppView *view, const TlAffine *local_to_world)
{
    cairo_matrix_t m;
    double s = view->scale;
    cairo_matrix_init(&m, s * local_to_world->a, s * local_to_world->b, s * local_to_world->c,
                      s * local_to_world->d, s * local_to_world->tx + view->pan_x,
                      s * local_to_world->ty + view->pan_y);
    return m;
}

static void local_polygon_path(cairo_t *cr, const TlPolygon *polygon)
{
    for (size_t i = 0; i < polygon->count; i++)
        (i ? cairo_line_to : cairo_move_to)(cr, polygon->items[i].x, polygon->items[i].y);
    if (polygon->count)
        cairo_close_path(cr);
}

static AppColor tile_color(const TlTileSettings *tile, TlTileKind kind)
{
    AppColor base = app_color_hex(tile->color);
    if (kind == TL_TILE_NARROW)
        return app_color_hex(COLOR_NARROW);
    return kind == TL_TILE_CUT ? app_color_darker(base, 1.12) : base;
}

static void render_tiles(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                         TlSpace *space, TlLayout *layout)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    cairo_matrix_t to_screen = local_to_screen(view, &layout->local_to_world);
    cairo_transform(cr, &to_screen);
    local_polygon_path(cr, &layout->floor_outline);
    for (size_t i = 0; i < layout->floor_holes.count; i++)
        local_polygon_path(cr, &layout->floor_holes.items[i]);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_clip_preserve(cr);
    app_set_color(cr, app_color_hex(COLOR_GROUT));
    cairo_fill(cr);

    double pixel = 1.0 / view->scale; /* один логический пиксель в осях раскладки */
    bool thin_joints = tile->joint_width * view->scale < 1.2;
    for (size_t i = 0; i < layout->tiles.count; i++) {
        const TlTile *t = &layout->tiles.items[i];
        cairo_rectangle(cr, t->rect.left, t->rect.top, tl_rect_width(t->rect), tl_rect_height(t->rect));
        app_set_fill_color(cr, app_draw_is_mono() ? (AppColor){1, 1, 1, 1} : tile_color(tile, t->kind));
        if (app_draw_is_mono() || thin_joints) {
            cairo_fill_preserve(cr);
            cairo_set_line_width(cr, (app_draw_is_mono() ? 0.5 : 0.6) * pixel);
            app_set_color(cr, app_color_darker(app_color_hex(tile->color), 1.5));
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
    }
    if (options->show_clips && fmin(tile->tile_width, tile->tile_height) * view->scale > 24) {
        app_set_color(cr, app_color_hex(COLOR_CLIP));
        double size = 5 * pixel;
        for (size_t i = 0; i < layout->clip_points.count; i++) {
            TlVec2 p = layout->clip_points.items[i];
            cairo_rectangle(cr, p.x - size / 2, p.y - size / 2, size, size);
        }
        cairo_fill(cr);
    }
}

static void render_floor(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                         TlSpace *space)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    cairo_save(cr);
    app_path_polygon_with_holes(cr, view, &space->outline, &space->holes);
    if (!options->show_tiles || !tile->enabled) {
        app_set_fill_color(cr, app_color_hex("#ffffff"));
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }
    cairo_clip_preserve(cr);
    TlLayout *layout = tl_space_layout(space);
    if (layout->too_many) {
        app_set_fill_color(cr, app_color_hex(tile->color));
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }
    if (tile->wall_gap > 0) {
        app_set_fill_color(cr, app_color_hex(COLOR_GAP));
        cairo_fill(cr);
    }
    cairo_new_path(cr);
    render_tiles(cr, view, options, space, layout);
    cairo_restore(cr);
}

/* ---- стены и перегородки ---------------------------------------------------- */

static void fill_hatch(cairo_t *cr, const AppRenderOptions *options)
{
    if (options->printing || app_draw_is_mono())
        return;
    cairo_surface_t *tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
    cairo_t *pattern_cr = cairo_create(tile);
    cairo_set_source_rgba(pattern_cr, 1, 1, 1, 0.16);
    cairo_set_line_width(pattern_cr, 1);
    cairo_move_to(pattern_cr, 0, 8);
    cairo_line_to(pattern_cr, 8, 0);
    cairo_stroke(pattern_cr);
    cairo_destroy(pattern_cr);
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(tile);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cr, pattern);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(pattern);
    cairo_surface_destroy(tile);
}

static void render_outline_walls(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                                 TlOutline *outline)
{
    TlPolygon outer = tl_outline_outer(outline);
    app_path_polygon(cr, view, &outer);
    app_path_polygon(cr, view, &outline->inner);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    bool highlighted = options->highlighted_outline == outline;
    app_set_color(cr, app_color_hex(highlighted ? COLOR_WALL_SELECTED : COLOR_WALL));
    cairo_fill_preserve(cr);
    fill_hatch(cr, options);
    cairo_new_path(cr);
    app_path_polygon(cr, view, &outer);
    app_path_polygon(cr, view, &outline->inner);
    app_stroke(cr, app_color_hex(COLOR_WALL_EDGE), 1.4);
    tl_polygon_free(&outer);
}

/* Тела перегородок одним силуэтом: сначала обводка вдвое толще, затем заливка
 * поверх — внутренние стыки тел скрываются. */
static void render_partitions(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                              TlProject *project)
{
    TlPolygonList bodies = tl_project_partition_bodies(project, 0);
    for (size_t i = 0; i < bodies.count; i++)
        app_path_polygon(cr, view, &bodies.items[i]);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    app_stroke(cr, app_color_hex(COLOR_WALL_EDGE), 2.8);
    for (size_t i = 0; i < bodies.count; i++)
        app_path_polygon(cr, view, &bodies.items[i]);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    app_set_color(cr, app_color_hex(COLOR_WALL));
    cairo_fill_preserve(cr);
    fill_hatch(cr, options);
    cairo_new_path(cr);
    tl_polygon_list_free_all(&bodies);
}

/* ---- проёмы ----------------------------------------------------------------- */

static void render_openings(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                            TlProject *project)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        const TlOpening *opening = project->openings.items[i];
        TlPolygon cut;
        if (!tl_project_opening_cut(project, opening, &cut)) {
            if (!options->printing) {
                TlVec2 c = app_view_to_screen(view, opening->center);
                cairo_arc(cr, c.x, c.y, 7, 0, 2 * G_PI);
                app_stroke(cr, app_color_hex("#d93025"), 2);
            }
            continue;
        }
        app_path_polygon(cr, view, &cut);
        app_set_fill_color(cr, app_color_hex(COLOR_OPENING_FLOOR));
        cairo_fill(cr);
        app_path_line(cr, view, cut.items[1], cut.items[2]);   /* откосы */
        app_path_line(cr, view, cut.items[3], cut.items[0]);
        app_stroke(cr, app_color_hex(COLOR_WALL_EDGE), 1.4);
        app_path_line(cr, view, cut.items[0], cut.items[1]);   /* линии порога */
        app_path_line(cr, view, cut.items[3], cut.items[2]);
        app_stroke_dashed(cr, app_color_hex(COLOR_OPENING_EDGE), 1, 4);
        if (options->show_dims) {
            TlVec2 a = app_view_to_screen(view, tl_vec2_midpoint(cut.items[0], cut.items[3]));
            TlVec2 b = app_view_to_screen(view, tl_vec2_midpoint(cut.items[1], cut.items[2]));
            if (tl_distance(a, b) > 22) {
                char text[32];
                snprintf(text, sizeof text, "%.0f", opening->width);
                app_render_dimension_text(cr, a, b, 0, text, COLOR_OPENING_DIM, true);
            }
        }
        tl_polygon_free(&cut);
    }
}

/* ---- номера плиток ---------------------------------------------------------- */

static void render_tile_numbers(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                                TlProject *project)
{
    tl_report_assign_numbers(project);
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        const TlTileSettings *tile = tl_space_effective_tile(space);
        TlLayout *layout = tl_space_layout(space);
        if (!tile->enabled || layout->too_many)
            continue;
        if (!options->printing && fmin(tile->tile_width, tile->tile_height) * view->scale < 18)
            continue;
        AppTextStyle style = {options->printing ? 8.7 : 10, true, app_color_hex("#20242a"),
                              app_color_rgba(255, 255, 255, options->printing ? 1.0 : 0.67)};
        for (size_t i = 0; i < layout->tiles.count; i++) {
            const TlTile *t = &layout->tiles.items[i];
            char text[16];
            snprintf(text, sizeof text, "%d", t->number);
            double box_w = (t->kind == TL_TILE_WHOLE ? tile->tile_width : t->piece_width) * view->scale;
            double box_h = (t->kind == TL_TILE_WHOLE ? tile->tile_height : t->piece_height) * view->scale;
            double text_w;
            app_text_size(text, &style, &text_w, NULL);
            double angle = tile->rotation_degrees;
            if (text_w + 4 > box_w && box_h > box_w)
                angle += 90; /* узкая полоса — вдоль неё */
            angle = fmod(angle + 90 + 3600, 180) - 90;
            TlVec2 world = tl_affine_apply(&layout->local_to_world, t->center);
            app_text_centered(cr, text, &style, app_view_to_screen(view, world), angle);
        }
    }
}

/* ---- размеры ---------------------------------------------------------------- */

void app_render_dimension_text(cairo_t *cr, TlVec2 a, TlVec2 b, double offset_px, const char *text,
                               const char *color_hex, bool bold)
{
    TlVec2 d = tl_vec2_normalized(tl_vec2_sub(b, a));
    AppTextStyle style = {11.3, bold, app_color_hex(color_hex), app_color_rgba(255, 255, 255, 0.84)};
    app_text_along(cr, text, &style, a, b, tl_vec2(d.y, -d.x), offset_px);
}

/* Белая подпись толщины на теле стены — белая и в чёрно-белом режиме. */
static void draw_white_label(cairo_t *cr, const char *text, TlVec2 center)
{
    bool mono = app_draw_is_mono();
    app_draw_set_mono(false);
    AppTextStyle white = {9.3, false, {1, 1, 1, 1}, APP_TRANSPARENT};
    app_text_centered(cr, text, &white, center, 0);
    app_draw_set_mono(mono);
}

static bool wall_faces_other_outline(TlProject *project, TlOutline *outline, const TlPolygon *outer,
                                     size_t wall, double probe_mm)
{
    TlVec2 normal;
    tl_outline_wall_frame(outline, wall, NULL, &normal);
    TlVec2 middle = tl_vec2_midpoint(outer->items[wall], outer->items[(wall + 1) % outer->count]);
    TlVec2 probe = tl_vec2_add(middle, tl_vec2_scale(normal, probe_mm));
    for (size_t o = 0; o < project->outlines.count; o++)
        if (project->outlines.items[o] != outline && tl_outline_contains(project->outlines.items[o], probe))
            return true;
    return false;
}

static void render_outer_dimensions(cairo_t *cr, const AppView *view, TlProject *project,
                                    TlOutline *outline)
{
    TlPolygon outer = tl_outline_outer(outline);
    size_t n = tl_outline_wall_count(outline);
    for (size_t wall = 0; wall < n; wall++) {
        double thickness = outline->thickness.items[wall];
        if (thickness <= 0 ||
            wall_faces_other_outline(project, outline, &outer, wall, app_view_pixels_to_mm(view, 6)))
            continue;
        TlVec2 normal;
        tl_outline_wall_frame(outline, wall, NULL, &normal);
        TlVec2 oa = app_view_to_screen(view, outer.items[wall]);
        TlVec2 ob = app_view_to_screen(view, outer.items[(wall + 1) % n]);
        double length_px = tl_distance(oa, ob);
        if (length_px < 28)
            continue;
        const double offset = 18;
        TlVec2 da = tl_vec2_add(oa, tl_vec2_scale(normal, offset));
        TlVec2 db = tl_vec2_add(ob, tl_vec2_scale(normal, offset));
        cairo_move_to(cr, da.x, da.y);
        cairo_line_to(cr, db.x, db.y);
        cairo_move_to(cr, oa.x, oa.y); /* выносные линии */
        cairo_line_to(cr, oa.x + normal.x * (offset + 4), oa.y + normal.y * (offset + 4));
        cairo_move_to(cr, ob.x, ob.y);
        cairo_line_to(cr, ob.x + normal.x * (offset + 4), ob.y + normal.y * (offset + 4));
        TlVec2 along = tl_vec2_normalized(tl_vec2_sub(db, da));
        TlVec2 tick = tl_vec2_scale(tl_vec2(along.x + along.y, along.y - along.x), 3); /* засечка 45° */
        const TlVec2 ends[2] = {da, db};
        for (int e = 0; e < 2; e++) {
            cairo_move_to(cr, ends[e].x - tick.x, ends[e].y - tick.y);
            cairo_line_to(cr, ends[e].x + tick.x, ends[e].y + tick.y);
        }
        app_stroke(cr, app_color_hex(COLOR_OUTER_DIM), 1);
        char text[32];
        snprintf(text, sizeof text, "%.0f", tl_distance(outer.items[wall], outer.items[(wall + 1) % n]));
        AppTextStyle style = {11.3, false, app_color_hex(COLOR_OUTER_DIM), app_color_rgba(255, 255, 255, 0.84)};
        app_text_along(cr, text, &style, da, db, tl_vec2(0, 0), 0);

        if (thickness * view->scale > 11 && length_px > 90) {
            TlVec2 a = tl_outline_wall_start(outline, wall), b = tl_outline_wall_end(outline, wall);
            TlVec2 middle = tl_vec2_add(tl_vec2_midpoint(a, b), tl_vec2_scale(normal, thickness / 2));
            snprintf(text, sizeof text, "%.0f", thickness);
            draw_white_label(cr, text, app_view_to_screen(view, middle));
        }
    }
    tl_polygon_free(&outer);
}

static void render_space_label(cairo_t *cr, const AppView *view, const AppRenderOptions *options,
                               TlSpace *space)
{
    if (options->show_dims) {
        for (size_t i = 0; i < space->outline.count; i++) {
            TlVec2 a = app_view_to_screen(view, tl_polygon_edge_start(&space->outline, i));
            TlVec2 b = app_view_to_screen(view, tl_polygon_edge_end(&space->outline, i));
            if (tl_distance(a, b) < 28)
                continue;
            TlVec2 normal;
            tl_polygon_edge_frame(&space->outline, i, NULL, &normal);
            char text[32];
            snprintf(text, sizeof text, "%.0f", tl_polygon_edge_length(&space->outline, i));
            AppTextStyle style = {11.3, false, app_color_hex(COLOR_INNER_DIM), app_color_rgba(255, 255, 255, 0.84)};
            app_text_along(cr, text, &style, a, b, tl_vec2_scale(normal, -1), 11);
        }
    }
    if (!options->show_room_labels)
        return;
    char area[48], tiles[48], source[160];
    const char *lines[4] = {space->name, area};
    size_t count = 2;
    snprintf(area, sizeof area, "%.2f м²", tl_space_area(space) / 1e6);
    const TlTileSettings *tile = tl_space_effective_tile(space);
    if (tile->enabled) {
        TlLayout *layout = tl_space_layout(space);
        if (!layout->too_many) {
            snprintf(tiles, sizeof tiles, "плиток: %d", tl_layout_total(layout));
            lines[count++] = tiles;
        }
    }
    if (tl_space_has_shared_floor(space)) {
        snprintf(source, sizeof source, "пол как: %s", space->layout_source->name);
        lines[count++] = source;
    }
    app_text_box(cr, lines, count, 12.7, app_view_to_screen(view, tl_space_label_point(space)));
}

static void render_partition_thickness(cairo_t *cr, const AppView *view, TlProject *project)
{
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        TlVec2 a = app_view_to_screen(view, partition->start), b = app_view_to_screen(view, partition->end);
        if (partition->thickness * view->scale <= 11 || tl_distance(a, b) <= 90)
            continue;
        char text[32];
        snprintf(text, sizeof text, "%.0f", partition->thickness);
        draw_white_label(cr, text, tl_vec2_midpoint(a, b));
    }
}

/* ---- замки и линейка -------------------------------------------------------- */

static void render_lock_at(cairo_t *cr, const AppView *view, TlVec2 world)
{
    TlVec2 s = app_view_to_screen(view, world);
    app_icon_draw(cr, "lock", s.x - 8, s.y - 8, 16);
}

static void render_locks(cairo_t *cr, const AppView *view, TlProject *project)
{
    double offset = app_view_pixels_to_mm(view, 30);
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        for (size_t wall = 0; wall < tl_outline_wall_count(outline); wall++)
            if (outline->locked.items[wall]) {
                TlBand band = tl_band_of_wall(outline, wall);
                render_lock_at(cr, view, tl_band_point(&band, band.length / 2 + offset, tl_band_middle(&band)));
            }
    }
    for (size_t i = 0; i < project->partitions.count; i++)
        if (project->partitions.items[i]->locked) {
            TlBand band = tl_band_of_partition(project->partitions.items[i]);
            render_lock_at(cr, view, tl_band_point(&band, band.length / 2 + offset, 0));
        }
    for (size_t i = 0; i < project->openings.count; i++) {
        TlBand band;
        const TlOpening *opening = project->openings.items[i];
        if (opening->locked && tl_project_opening_host(project, opening, &band)) {
            double along;
            tl_band_local(&band, opening->center, &along, NULL);
            render_lock_at(cr, view, tl_band_point(&band, along + offset, tl_band_middle(&band)));
        }
    }
}

static void render_scale_bar(cairo_t *cr, const AppView *view)
{
    static const double steps[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
    double target = 120 / view->scale, mm = steps[8];
    for (size_t i = 0; i < 9; i++)
        if (steps[i] >= target * 0.6) {
            mm = steps[i];
            break;
        }
    double length = mm * view->scale, x = 16, y = view->height - 22;
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + length, y);
    cairo_move_to(cr, x, y - 5);
    cairo_line_to(cr, x, y + 5);
    cairo_move_to(cr, x + length, y - 5);
    cairo_line_to(cr, x + length, y + 5);
    app_stroke(cr, app_color_hex("#333333"), 2);
    char text[32];
    if (mm >= 1000)
        snprintf(text, sizeof text, "%g м", mm / 1000.0);
    else
        snprintf(text, sizeof text, "%.0f мм", mm);
    AppTextStyle style = {11.3, false, app_color_hex("#333333"), APP_TRANSPARENT};
    app_text_in_rect(cr, text, &style, x + length + 8, y - 8, 100, 16, -1);
}

/* ---- весь план -------------------------------------------------------------- */

void app_render_plan(cairo_t *cr, TlProject *project, const AppView *view,
                     const AppRenderOptions *options)
{
    bool previous_mono = app_draw_is_mono();
    app_draw_set_mono(options->mono);
    cairo_save(cr);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (!options->printing) {
        app_set_fill_color(cr, app_color_hex(COLOR_BACKGROUND));
        cairo_paint(cr);
        if (options->show_grid)
            render_grid(cr, view);
    }
    for (size_t s = 0; s < project->spaces.count; s++)
        render_floor(cr, view, options, project->spaces.items[s]);
    for (size_t o = 0; o < project->outlines.count; o++)
        render_outline_walls(cr, view, options, project->outlines.items[o]);
    render_partitions(cr, view, options, project);
    render_openings(cr, view, options, project);
    if (options->show_numbers && options->show_tiles)
        render_tile_numbers(cr, view, options, project);
    if (options->show_dims)
        for (size_t o = 0; o < project->outlines.count; o++)
            render_outer_dimensions(cr, view, project, project->outlines.items[o]);
    for (size_t s = 0; s < project->spaces.count; s++)
        render_space_label(cr, view, options, project->spaces.items[s]);
    if (options->show_dims)
        render_partition_thickness(cr, view, project);
    if (options->show_locks && !options->printing)
        render_locks(cr, view, project);
    render_scale_bar(cr, view);
    cairo_restore(cr);
    app_draw_set_mono(previous_mono);
}

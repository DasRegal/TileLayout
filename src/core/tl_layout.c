#include "tl_layout.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tl_regions.h"

#define FULL_TILE_RATIO 0.999   /* кусок не меньше этой доли плитки — целая плитка */
#define FIGURED_RATIO 0.99      /* площадь меньше этой доли габарита — фигурный кусок */
#define MIN_PIECE_AREA 1.0      /* мм² */
#define CLIP_PROBE 3.0          /* мм от шва внутрь плитки при проверке СВП */
#define CLIP_MIN_EDGE 120.0     /* мм: на короткие стороны (срезанный угол, вставка, мозаика) СВП не ставят */
#define OPTIMIZE_STEPS 12

/* Пол комнаты в осях раскладки (с отступом от стен). */
typedef struct LocalFloor {
    TlPolygon outline;
    TlPolygonList holes;
    TlPolygonList joint_strips; /* деформационные швы — без отступа: зазор шва задан им самим */
} LocalFloor;

struct TlLayoutCache {
    TlLayout *layout;
    TlPolygon outline, frame;
    TlPolygonList holes, joint_strips, cutouts;
    TlCircleList pipes;
    TlTileSettings tile;
};

/* ---- оси раскладки ---------------------------------------------------------- */

static TlAffine local_frame(const TlPolygon *frame, double rotation_degrees)
{
    TlRect bounds = tl_polygon_bounds(frame);
    return tl_affine_rotation_translation(tl_vec2(bounds.left, bounds.top), rotation_degrees);
}

static TlAffine surface_frame(const TlSurface *surface)
{
    return tl_affine_rotation_translation(surface->origin, surface->angle + surface->tile->rotation_degrees);
}

static TlAffine world_to_local(TlSpace *space)
{
    TlAffine to_world = local_frame(tl_space_frame(space), tl_space_effective_tile(space)->rotation_degrees);
    return tl_affine_inverted(&to_world);
}

#define ON_THRESHOLD_MM 0.3

/* Край пола лежит на стыке в проёме: там плитка без отступа от стен — зазор задан швом двери. */
static bool on_threshold(const TlPolygonList *threshold_strips, TlVec2 a, TlVec2 b)
{
    TlVec2 middle = tl_vec2_midpoint(a, b);
    for (size_t i = 0; i < threshold_strips->count; i++) {
        const TlPolygon *strip = &threshold_strips->items[i];
        if (tl_polygon_contains(strip, middle))
            return true;
        for (size_t k = 0; k < strip->count; k++)
            if (tl_point_segment_distance(middle, strip->items[k], strip->items[(k + 1) % strip->count], NULL) <
                ON_THRESHOLD_MM)
                return true;
    }
    return false;
}

static void push_local(TlPolygonList *list, const TlAffine *to_local, const TlPolygonList *world)
{
    for (size_t i = 0; world && i < world->count; i++)
        tl_polygon_list_push(list, tl_affine_apply_polygon(to_local, &world->items[i]));
}

static LocalFloor make_local_floor(const TlSurface *surface, const TlAffine *to_local)
{
    LocalFloor floor = {0};
    double wall_gap = surface->tile->wall_gap;
    const TlPolygon *world = surface->outline;
    TlPolygon outline = tl_affine_apply_polygon(to_local, world);
    double *distances = tl_malloc((outline.count ? outline.count : 1) * sizeof(double));
    bool any = false;
    for (size_t i = 0; i < outline.count; i++) {
        if (surface->insets)
            distances[i] = -surface->insets[i];
        else
            distances[i] = surface->threshold_strips &&
                                   on_threshold(surface->threshold_strips, world->items[i], world->items[(i + 1) % world->count])
                               ? 0
                               : -wall_gap;
        any |= distances[i] != 0;
    }
    floor.outline = any ? tl_polygon_offset(&outline, distances) : tl_polygon_copy(&outline);
    free(distances);
    tl_polygon_free(&outline);
    for (size_t i = 0; surface->holes && i < surface->holes->count; i++) {
        TlPolygon hole = tl_affine_apply_polygon(to_local, &surface->holes->items[i]);
        tl_polygon_list_push(&floor.holes,
                             wall_gap > 0 ? tl_polygon_offset_uniform(&hole, wall_gap) : tl_polygon_copy(&hole));
        tl_polygon_free(&hole);
    }
    push_local(&floor.holes, to_local, surface->cutouts); /* зоны: шов к ним уже учтён */
    push_local(&floor.joint_strips, to_local, surface->joint_strips);
    return floor;
}

static bool inside_any(const TlPolygonList *polygons, TlVec2 point)
{
    for (size_t i = 0; i < polygons->count; i++)
        if (tl_polygon_contains(&polygons->items[i], point))
            return true;
    return false;
}

/* Точка лежит на полу под плитку: внутри контура, не в перегородке и не в шве. */
static bool floor_contains(const TlLayout *layout, TlVec2 point)
{
    return tl_polygon_contains(&layout->floor_outline, point) && !inside_any(&layout->floor_holes, point) &&
           !inside_any(&layout->joint_strips, point);
}

/* ---- классификация плитки ------------------------------------------------- */

static void free_tile(TlTile *tile)
{
    tl_polygon_free(&tile->outline);
    tl_polygon_list_free_all(&tile->shapes);
    tl_circle_list_free(&tile->drills);
    tl_polygon_list_free_all(&tile->miters);
}

#define MITER_MM 0.6 /* сторона куска на линии запила — не дальше */

static bool on_segment(TlVec2 p, TlVec2 a, TlVec2 b)
{
    double t;
    return tl_point_segment_distance(p, a, b, &t) < MITER_MM;
}

void tl_layout_mark_miters(TlLayout *layout, const TlPolygonList *lines)
{
    TlAffine to_local = tl_affine_inverted(&layout->local_to_world);
    TlPolygonList local = {0};
    for (size_t i = 0; lines && i < lines->count; i++)
        if (lines->items[i].count == 2)
            tl_polygon_list_push(&local, tl_affine_apply_polygon(&to_local, &lines->items[i]));
    for (size_t t = 0; local.count && t < layout->tiles.count; t++) {
        TlTile *tile = &layout->tiles.items[t];
        TlPolygon whole = {0};
        size_t shapes = tile->shapes.count ? tile->shapes.count : 1;
        if (!tile->shapes.count) /* целая плитка — её прямоугольник от угла */
            whole = tl_polygon_from_rect((TlRect){0, 0, tl_rect_width(tile->rect), tl_rect_height(tile->rect)});
        TlVec2 corner = {tile->rect.left, tile->rect.top};
        for (size_t k = 0; k < shapes; k++) { /* и вырезы: у края плитки вырез — «дыра» на её краю */
            const TlPolygon *shape = tile->shapes.count ? &tile->shapes.items[k] : &whole;
            for (size_t e = 0; e < shape->count; e++) {
                TlVec2 p = tl_polygon_edge_start(shape, e), q = tl_polygon_edge_end(shape, e);
                if (tl_distance(p, q) < 1)
                    continue;
                for (size_t l = 0; l < local.count; l++) {
                    TlVec2 a = local.items[l].items[0], b = local.items[l].items[1];
                    if (on_segment(tl_vec2_add(p, corner), a, b) && on_segment(tl_vec2_add(q, corner), a, b)) {
                        const TlVec2 edge[2] = {p, q};
                        tl_polygon_list_push(&tile->miters, tl_polygon_from_points(edge, 2));
                        break;
                    }
                }
            }
        }
        tl_polygon_free(&whole);
    }
    tl_polygon_list_free_all(&local);
}

static TlPolygon shape_from_tile_corner(TlPolygon clipped, TlRect rect)
{
    tl_polygon_clean(&clipped, 0.01);
    tl_polygon_translate(&clipped, tl_vec2(-rect.left, -rect.top));
    return clipped;
}

static bool crosses_joint(const LocalFloor *floor, const TlPolygon *tile_outline)
{
    TlRect rect = tl_polygon_bounds(tile_outline);
    for (size_t i = 0; i < floor->joint_strips.count; i++)
        if (tl_rect_intersects(tl_polygon_bounds(&floor->joint_strips.items[i]), rect) &&
            tl_polygons_overlap_area(&floor->joint_strips.items[i], tile_outline) >= MIN_PIECE_AREA)
            return true;
    return false;
}

/* Многоугольники для разбиения: плитка, пол, затем всё, что пол вырезает. */
static bool keep_piece_of_floor(const bool *inside, size_t polygon_count, void *context)
{
    (void)context;
    if (!inside[0] || !inside[1])
        return false;
    for (size_t i = 2; i < polygon_count; i++)
        if (inside[i])
            return false;
    return true;
}

static void set_piece_size(TlTile *tile, TlRect piece_bounds)
{
    tile->piece_width = fmin(tl_rect_width(piece_bounds), tl_rect_width(tile->rect));
    tile->piece_height = fmin(tl_rect_height(piece_bounds), tl_rect_height(tile->rect));
    tile->center = tl_rect_center(piece_bounds);
}

static double rect_area(TlRect rect) { return tl_rect_width(rect) * tl_rect_height(rect); }

/* Плитка-многоугольник или плитка, через которую проходит шов: куски — разбиением
   плоскости (плитка ∩ пол − перегородки − швы). Номер — на самом большом куске,
   размеры подрезки — по нему же; узкая — если узкий хоть один кусок. */
static bool classify_by_regions(const LocalFloor *floor, const TlPolygon *tile_outline, TlRect rect,
                                const TlTileSettings *settings, bool fast, TlTile *tile)
{
    TlPolygonList cutters = {0};
    tl_polygon_list_push(&cutters, tl_polygon_copy(tile_outline));
    tl_polygon_list_push(&cutters, tl_polygon_copy(&floor->outline));
    for (size_t i = 0; i < floor->holes.count; i++)
        tl_polygon_list_push(&cutters, tl_polygon_copy(&floor->holes.items[i]));
    for (size_t i = 0; i < floor->joint_strips.count; i++)
        tl_polygon_list_push(&cutters, tl_polygon_copy(&floor->joint_strips.items[i]));
    TlRegionList pieces = tl_regions_build(cutters.items, cutters.count, keep_piece_of_floor, NULL);
    tl_polygon_list_free_all(&cutters);

    double area = 0, largest_area = 0, narrowest = INFINITY;
    bool figured = false;
    TlRect largest_bounds = {0};
    for (size_t i = 0; i < pieces.count; i++) {
        double piece_area = tl_region_area(&pieces.items[i]);
        TlRect bounds = tl_polygon_bounds(&pieces.items[i].outline);
        area += piece_area;
        narrowest = fmin(narrowest, fmin(tl_rect_width(bounds), tl_rect_height(bounds)));
        figured |= pieces.items[i].holes.count > 0 ||
                   piece_area < tl_rect_width(bounds) * tl_rect_height(bounds) * FIGURED_RATIO;
        if (piece_area > largest_area) {
            largest_area = piece_area;
            largest_bounds = bounds;
        }
    }
    if (area < MIN_PIECE_AREA) {
        tl_region_list_free_all(&pieces);
        return false;
    }
    *tile = (TlTile){.rect = rect, .center = tl_rect_center(rect)};
    if (area >= tl_polygon_area(tile_outline) * FULL_TILE_RATIO) {
        tile->kind = TL_TILE_WHOLE;
        tl_region_list_free_all(&pieces);
        return true;
    }
    set_piece_size(tile, largest_bounds);
    tile->figured = figured;
    tile->kind = narrowest < settings->min_cut_width ? TL_TILE_NARROW : TL_TILE_CUT;
    for (size_t i = 0; i < pieces.count && !fast; i++) {
        tl_polygon_list_push(&tile->shapes, shape_from_tile_corner(tl_polygon_copy(&pieces.items[i].outline), rect));
        for (size_t h = 0; h < pieces.items[i].holes.count; h++)
            tl_polygon_list_push(&tile->shapes,
                                 shape_from_tile_corner(tl_polygon_copy(&pieces.items[i].holes.items[h]), rect));
    }
    tl_region_list_free_all(&pieces);
    return true;
}

/* Прямоугольная плитка: пол обрезается по ней (быстро). */
static bool classify_rect(const LocalFloor *floor, TlRect rect, const TlTileSettings *settings, bool fast,
                          TlTile *tile)
{
    TlPolygon clipped = tl_polygon_clip_rect(&floor->outline, rect);
    double area = tl_polygon_area(&clipped);
    TlPolygonList hole_pieces = {0};
    for (size_t i = 0; i < floor->holes.count && area >= MIN_PIECE_AREA; i++) {
        TlPolygon hole_piece = tl_polygon_clip_rect(&floor->holes.items[i], rect);
        double hole_area = tl_polygon_area(&hole_piece);
        area -= hole_area;
        if (hole_area >= MIN_PIECE_AREA && !fast)
            tl_polygon_list_push(&hole_pieces, hole_piece);
        else
            tl_polygon_free(&hole_piece);
    }
    if (area < MIN_PIECE_AREA) {
        tl_polygon_free(&clipped);
        tl_polygon_list_free_all(&hole_pieces);
        return false;
    }

    *tile = (TlTile){.rect = rect, .center = tl_rect_center(rect)};
    if (area >= rect_area(rect) * FULL_TILE_RATIO) {
        tile->kind = TL_TILE_WHOLE;
        tl_polygon_free(&clipped);
        tl_polygon_list_free_all(&hole_pieces);
        return true;
    }
    TlRect piece_bounds = tl_polygon_bounds(&clipped);
    set_piece_size(tile, piece_bounds);
    tile->figured = area < tile->piece_width * tile->piece_height * FIGURED_RATIO;
    tile->kind = fmin(tile->piece_width, tile->piece_height) < settings->min_cut_width ? TL_TILE_NARROW
                                                                                       : TL_TILE_CUT;
    if (fast) {
        tl_polygon_free(&clipped);
        return true;
    }
    tl_polygon_list_push(&tile->shapes, shape_from_tile_corner(clipped, rect));
    for (size_t i = 0; i < hole_pieces.count; i++)
        tl_polygon_list_push(&tile->shapes, shape_from_tile_corner(hole_pieces.items[i], rect));
    tl_polygon_list_free(&hole_pieces);
    return true;
}

/* Заполняет tile, если плитка задевает пол; false — не задевает. */
static bool classify_tile(const LocalFloor *floor, const TlPlacedTile *placed, const TlTileSettings *settings,
                          bool fast, TlTile *tile)
{
    TlPolygon outline = placed->shape_count ? tl_polygon_from_points(placed->shape, placed->shape_count)
                                            : tl_polygon_from_rect(placed->rect);
    bool touches = placed->shape_count || crosses_joint(floor, &outline)
                       ? classify_by_regions(floor, &outline, placed->rect, settings, fast, tile)
                       : classify_rect(floor, placed->rect, settings, fast, tile);
    if (touches && placed->shape_count && !fast) /* контур плитки — для чертежа и рисования */
        tile->outline = shape_from_tile_corner(tl_polygon_copy(&outline), placed->rect);
    tl_polygon_free(&outline);
    return touches;
}

/* ---- СВП -------------------------------------------------------------------- */

/* Контур плитки в осях раскладки: прямоугольник или многоугольник. */
static TlPolygon tile_polygon(const TlTile *tile)
{
    if (!tile->outline.count)
        return tl_polygon_from_rect(tile->rect);
    TlPolygon polygon = tl_polygon_copy(&tile->outline);
    tl_polygon_translate(&polygon, tl_vec2(tile->rect.left, tile->rect.top));
    return polygon;
}

/* СВП — в швах между двумя плитками: по сторонам каждой плитки, чья наружная
 * нормаль смотрит вправо (или строго вниз), — так шов между соседями считается
 * один раз. На длинную сторону — clips_per_long_side, на остальные —
 * clips_per_short_side; на стороны короче CLIP_MIN_EDGE — нет. Точка ставится, только если по обе стороны шва есть
 * пол: у стен и перегородок СВП нет. */
static void place_clips(TlLayout *layout, const TlTileSettings *settings)
{
    double gap = settings->joint_width;
    for (size_t i = 0; i < layout->tiles.count; i++) {
        TlPolygon polygon = tile_polygon(&layout->tiles.items[i]);
        double orientation = tl_polygon_signed_area(&polygon) > 0 ? 1 : -1;
        double longest = 0;
        for (size_t e = 0; e < polygon.count; e++)
            longest = fmax(longest, tl_polygon_edge_length(&polygon, e));
        for (size_t e = 0; e < polygon.count; e++) {
            TlVec2 a = tl_polygon_edge_start(&polygon, e), b = tl_polygon_edge_end(&polygon, e);
            double length = tl_distance(a, b);
            if (length < CLIP_MIN_EDGE)
                continue;
            TlVec2 direction = tl_vec2_scale(tl_vec2_sub(b, a), 1 / length);
            /* наружу: при обходе против часовой (площадь > 0 в осях y вниз — по часовой на экране) */
            TlVec2 normal = tl_vec2_scale(tl_vec2(direction.y, -direction.x), orientation);
            if (!(normal.x > 1e-6 || (fabs(normal.x) <= 1e-6 && normal.y > 0)))
                continue;
            int count = length >= longest - 0.5 ? settings->clips_per_long_side : settings->clips_per_short_side;
            for (int k = 0; k < count; k++) {
                TlVec2 p = tl_vec2_lerp(a, b, (k + 0.5) / count);
                if (floor_contains(layout, tl_vec2_sub(p, tl_vec2_scale(normal, CLIP_PROBE))) &&
                    floor_contains(layout, tl_vec2_add(p, tl_vec2_scale(normal, gap + CLIP_PROBE))))
                    tl_polygon_push(&layout->clip_points, tl_vec2_add(p, tl_vec2_scale(normal, gap / 2)));
            }
        }
        tl_polygon_free(&polygon);
    }
}

/* ---- крестики ---------------------------------------------------------------- */

static int compare_points(const void *left, const void *right)
{
    const TlVec2 *a = left, *b = right;
    if (a->x != b->x)
        return a->x < b->x ? -1 : 1;
    return a->y < b->y ? -1 : a->y > b->y;
}

/* Пересечение швов — у каждого угла плитки на полшва наружу от обеих её сторон; у соседних
 * плиток эти точки совпадают (сравниваются с округлением до 0,1 мм). Крестик ставится, если
 * вокруг точки — пол со всех сторон. */
static void place_crosses(TlLayout *layout, const TlTileSettings *settings)
{
    double half = settings->joint_width / 2;
    TlPolygon points = {0};
    for (size_t i = 0; i < layout->tiles.count; i++) {
        TlPolygon polygon = tile_polygon(&layout->tiles.items[i]);
        double orientation = tl_polygon_signed_area(&polygon) > 0 ? 1 : -1;
        for (size_t v = 0; v < polygon.count; v++) {
            size_t previous = (v + polygon.count - 1) % polygon.count;
            TlVec2 d1 = tl_vec2_normalized(tl_vec2_sub(polygon.items[v], polygon.items[previous]));
            TlVec2 d2 = tl_vec2_normalized(tl_vec2_sub(polygon.items[(v + 1) % polygon.count], polygon.items[v]));
            TlVec2 n1 = tl_vec2_scale(tl_vec2(d1.y, -d1.x), orientation), n2 = tl_vec2_scale(tl_vec2(d2.y, -d2.x), orientation);
            double denominator = 1 + tl_vec2_dot(n1, n2);
            if (denominator < 0.2) /* острый угол (шеврон) — точка далеко, крестик там не ставят */
                continue;
            TlVec2 p = tl_vec2_add(polygon.items[v], tl_vec2_scale(tl_vec2_add(n1, n2), half / denominator));
            tl_polygon_push(&points, tl_vec2(round(p.x * 10) / 10, round(p.y * 10) / 10));
        }
        tl_polygon_free(&polygon);
    }
    if (points.count)
        qsort(points.items, points.count, sizeof(TlVec2), compare_points);
    double probe = half + CLIP_PROBE;
    for (size_t i = 0; i < points.count; i++) {
        TlVec2 p = points.items[i];
        if (i > 0 && tl_distance(p, points.items[i - 1]) < 0.3)
            continue;
        bool inside = floor_contains(layout, tl_vec2(p.x - probe, p.y - probe)) &&
                      floor_contains(layout, tl_vec2(p.x + probe, p.y - probe)) &&
                      floor_contains(layout, tl_vec2(p.x - probe, p.y + probe)) &&
                      floor_contains(layout, tl_vec2(p.x + probe, p.y + probe));
        if (inside)
            tl_polygon_push(&layout->cross_points, p);
    }
    tl_polygon_free(&points);
}

/* ---- раскладка -------------------------------------------------------------- */

static void count_tile(TlLayout *layout, TlTileKind kind)
{
    if (kind == TL_TILE_WHOLE)
        layout->whole_count++;
    else if (kind == TL_TILE_CUT)
        layout->cut_count++;
    else
        layout->narrow_count++;
}

/* Отверстия труб в плитке: центр — от угла плитки. */
static void drill_tile(TlTile *tile, const TlCircleList *pipes)
{
    for (size_t i = 0; i < pipes->count; i++) {
        TlCircle pipe = pipes->items[i];
        if (!tl_circle_touches_rect(pipe, tile->rect))
            continue;
        if (tile->outline.count) { /* у многоугольника — по самому контуру */
            TlPolygon polygon = tile_polygon(tile);
            bool inside = tl_polygon_contains(&polygon, pipe.center);
            double distance = INFINITY;
            for (size_t e = 0; e < polygon.count; e++)
                distance = fmin(distance, tl_point_segment_distance(pipe.center, tl_polygon_edge_start(&polygon, e),
                                                                    tl_polygon_edge_end(&polygon, e), NULL));
            tl_polygon_free(&polygon);
            if (!inside && distance >= pipe.diameter / 2 - 0.5)
                continue;
        }
        pipe.center = tl_vec2_sub(pipe.center, tl_vec2(tile->rect.left, tile->rect.top));
        tl_circle_list_push(&tile->drills, pipe);
    }
}

typedef struct Placing {
    TlLayout *layout;
    const LocalFloor *floor;
    const TlTileSettings *settings;
    const TlCircleList *pipes;   /* в осях раскладки */
    bool fast;
} Placing;

static void place_tile(const TlPlacedTile *placed, void *context)
{
    Placing *placing = context;
    TlTile tile;
    if (!classify_tile(placing->floor, placed, placing->settings, placing->fast, &tile))
        return;
    tile.cell = placed->cell;
    tile.spec = placed->spec;
    tile.turned = placed->turned;
    tile.number = (int)placing->layout->tiles.count + 1; /* по рядам рисунка */
    if (!placing->fast)
        drill_tile(&tile, placing->pipes);
    tl_tile_list_push(&placing->layout->tiles, tile);
    count_tile(placing->layout, tile.kind);
}

static double polygons_area(const TlPolygonList *list)
{
    double area = 0;
    for (size_t i = 0; list && i < list->count; i++)
        area += tl_polygon_area(&list->items[i]);
    return area;
}

TlLayout *tl_layout_compute_surface(const TlSurface *surface, const TlLayoutOptions *options)
{
    const TlLayoutOptions defaults = {0};
    if (!options)
        options = &defaults;
    const TlTileSettings *settings = surface->tile;
    TlLayout *layout = TL_NEW(TlLayout);
    layout->local_to_world = surface_frame(surface);
    layout->floor_area =
        tl_polygon_area(surface->outline) - polygons_area(surface->holes) - polygons_area(surface->cutouts);
    if (surface->outline->count < 3)
        return layout;

    TlAffine to_local = tl_affine_inverted(&layout->local_to_world);
    LocalFloor room_floor = make_local_floor(surface, &to_local);
    layout->floor_outline = room_floor.outline;
    layout->floor_holes = room_floor.holes;
    layout->joint_strips = room_floor.joint_strips;
    TlRect bounds = tl_polygon_bounds(&room_floor.outline);
    layout->local_bounds = bounds;
    if (!tl_pattern_unit(settings, &layout->pattern) || room_floor.outline.count < 3)
        return layout;
    if (tl_pattern_estimate(&layout->pattern, bounds) > TL_LAYOUT_MAX_TILES) {
        layout->too_many = true;
        return layout;
    }

    TlCircleList pipes = {0};
    for (size_t i = 0; surface->pipes && i < surface->pipes->count; i++) {
        TlCircle pipe = surface->pipes->items[i];
        pipe.center = tl_affine_apply(&to_local, pipe.center);
        tl_circle_list_push(&pipes, pipe);
    }
    TlVec2 offset = options->use_offset ? tl_vec2(options->offset_x, options->offset_y)
                                        : tl_vec2(settings->offset_x, settings->offset_y);
    Placing placing = {layout, &room_floor, settings, &pipes, options->fast};
    tl_pattern_cover(&layout->pattern, offset, bounds, place_tile, &placing);
    tl_circle_list_free(&pipes);
    if (!options->fast) {
        place_clips(layout, settings);
        place_crosses(layout, settings);
    }
    return layout;
}

/* Основной пол комнаты: оси — от угла габарита (у общего пола — комнаты-источника). */
static TlSurface main_surface(TlSpace *space, const TlPolygonList *cutouts)
{
    TlRect bounds = tl_polygon_bounds(tl_space_frame(space));
    return (TlSurface){.outline = &space->outline,
                       .holes = &space->holes,
                       .cutouts = cutouts,
                       .joint_strips = &space->cut_strips,
                       .threshold_strips = &space->threshold_strips,
                       .pipes = &space->pipes,
                       .origin = tl_vec2(bounds.left, bounds.top),
                       .angle = 0,
                       .tile = tl_space_effective_tile(space)};
}

TlLayout *tl_layout_compute(TlSpace *space, const TlLayoutOptions *options)
{
    TlPolygonList cutouts = tl_space_zone_cutouts(space);
    TlSurface surface = main_surface(space, &cutouts);
    TlLayout *layout = tl_layout_compute_surface(&surface, options);
    tl_polygon_list_free_all(&cutouts);
    return layout;
}

void tl_layout_free(TlLayout *layout)
{
    if (!layout)
        return;
    for (size_t i = 0; i < layout->tiles.count; i++)
        free_tile(&layout->tiles.items[i]);
    tl_tile_list_free(&layout->tiles);
    tl_polygon_free(&layout->clip_points);
    tl_polygon_free(&layout->cross_points);
    tl_polygon_free(&layout->floor_outline);
    tl_polygon_list_free_all(&layout->floor_holes);
    tl_polygon_list_free_all(&layout->joint_strips);
    free(layout);
}

int tl_layout_total(const TlLayout *layout)
{
    return layout->whole_count + layout->cut_count + layout->narrow_count;
}

TlTile *tl_layout_tile_at(TlLayout *layout, TlCell cell)
{
    for (size_t i = 0; i < layout->tiles.count; i++)
        if (tl_compare_cells(layout->tiles.items[i].cell, cell) == 0)
            return &layout->tiles.items[i];
    return NULL;
}

const TlTileSpec *tl_layout_spec(const TlLayout *layout, const TlTile *tile)
{
    return &layout->pattern.specs[tile->spec];
}

void tl_tile_piece_size(const TlTile *tile, double *width, double *height)
{
    *width = tile->turned ? tile->piece_height : tile->piece_width;
    *height = tile->turned ? tile->piece_width : tile->piece_height;
}

void tl_tile_laid_size(const TlLayout *layout, const TlTile *tile, double *width, double *height)
{
    const TlTileSpec *spec = tl_layout_spec(layout, tile);
    *width = spec->laid_width;
    *height = spec->laid_height;
}

/* ---- кэш -------------------------------------------------------------------- */

static bool polygons_equal(const TlPolygon *a, const TlPolygon *b)
{
    return a->count == b->count && (a->count == 0 || memcmp(a->items, b->items, a->count * sizeof(TlVec2)) == 0);
}

static bool polygon_lists_equal(const TlPolygonList *a, const TlPolygonList *b)
{
    if (a->count != b->count)
        return false;
    for (size_t i = 0; i < a->count; i++)
        if (!polygons_equal(&a->items[i], &b->items[i]))
            return false;
    return true;
}

static bool circle_lists_equal(const TlCircleList *a, const TlCircleList *b)
{
    return a->count == b->count && (a->count == 0 || memcmp(a->items, b->items, a->count * sizeof(TlCircle)) == 0);
}

void tl_layout_cache_free(struct TlLayoutCache *cache)
{
    if (!cache)
        return;
    tl_layout_free(cache->layout);
    tl_polygon_free(&cache->outline);
    tl_polygon_free(&cache->frame);
    tl_polygon_list_free_all(&cache->holes);
    tl_polygon_list_free_all(&cache->joint_strips);
    tl_polygon_list_free_all(&cache->cutouts);
    tl_circle_list_free(&cache->pipes);
    free(cache);
}

static bool cache_is_current(const struct TlLayoutCache *cache, TlSpace *space, const TlPolygonList *cutouts)
{
    return cache && polygons_equal(&cache->outline, &space->outline) &&
           polygon_lists_equal(&cache->cutouts, cutouts) &&
           polygon_lists_equal(&cache->holes, &space->holes) &&
           polygon_lists_equal(&cache->joint_strips, &space->cut_strips) &&
           circle_lists_equal(&cache->pipes, &space->pipes) &&
           polygons_equal(&cache->frame, tl_space_frame(space)) &&
           tl_tile_settings_equal(&cache->tile, tl_space_effective_tile(space));
}

TlLayout *tl_space_layout(TlSpace *space)
{
    TlPolygonList cutouts = tl_space_zone_cutouts(space);
    if (cache_is_current(space->layout_cache, space, &cutouts)) {
        tl_polygon_list_free_all(&cutouts);
        return space->layout_cache->layout;
    }
    tl_layout_cache_free(space->layout_cache);
    struct TlLayoutCache *cache = TL_NEW(struct TlLayoutCache);
    TlSurface surface = main_surface(space, &cutouts);
    cache->layout = tl_layout_compute_surface(&surface, NULL);
    cache->cutouts = cutouts;
    cache->outline = tl_polygon_copy(&space->outline);
    cache->frame = tl_polygon_copy(tl_space_frame(space));
    cache->holes = tl_polygon_list_deep_copy(&space->holes);
    cache->joint_strips = tl_polygon_list_deep_copy(&space->cut_strips);
    cache->pipes = tl_circle_list_copy(&space->pipes);
    cache->tile = *tl_space_effective_tile(space);
    space->layout_cache = cache;
    return cache->layout;
}

/* ---- материалы -------------------------------------------------------------- */

static int bags_for(double amount, double per_bag)
{
    return per_bag > 0 ? (int)ceil(amount / per_bag - 1e-9) : 0;
}

double tl_layout_glue_kg(const TlTileSettings *tile, const TlLayout *layout)
{
    return layout->floor_area / 1e6 * tile->glue_kg_per_m2_mm * tile->glue_layer_mm;
}

int tl_layout_glue_bags(const TlTileSettings *tile, const TlLayout *layout)
{
    return bags_for(tl_layout_glue_kg(tile, layout), tile->glue_bag_kg);
}

int tl_layout_clip_packs(const TlTileSettings *tile, const TlLayout *layout)
{
    return bags_for((double)layout->clip_points.count, tile->clips_per_pack);
}

double tl_layout_joint_length(const TlLayout *layout)
{
    double length = 0;
    for (size_t i = 0; i < layout->tiles.count; i++) {
        const TlTile *tile = &layout->tiles.items[i];
        if (tile->kind != TL_TILE_WHOLE)
            length += tile->piece_width + tile->piece_height;
        else if (tile->outline.count)
            length += tl_polygon_perimeter(&tile->outline) / 2;
        else
            length += tl_rect_width(tile->rect) + tl_rect_height(tile->rect);
    }
    return length;
}

double tl_layout_grout_kg(const TlTileSettings *s, const TlLayout *layout)
{
    /* мм³ × г/см³ = 1e-6 кг */
    return tl_layout_joint_length(layout) * s->joint_width * s->tile_thickness * s->grout_density * 1e-6;
}

double tl_layout_primer_l(const TlTileSettings *tile, const TlLayout *layout)
{
    return layout->floor_area / 1e6 * tile->primer_l_per_m2;
}

/* Сторона куска лежит на кромке плитки (на стороне её контура). */
static bool on_tile_edge(const TlPolygon *tile, TlVec2 a, TlVec2 b)
{
    for (size_t e = 0; e < tile->count; e++) {
        TlVec2 p = tl_polygon_edge_start(tile, e), q = tl_polygon_edge_end(tile, e);
        if (tl_point_segment_distance(a, p, q, NULL) < 0.5 && tl_point_segment_distance(b, p, q, NULL) < 0.5)
            return true;
    }
    return false;
}

double tl_layout_cut_length(const TlLayout *layout)
{
    double length = 0;
    for (size_t i = 0; i < layout->tiles.count; i++) {
        const TlTile *tile = &layout->tiles.items[i];
        if (tile->kind == TL_TILE_WHOLE)
            continue;
        TlPolygon outline = tile->outline.count ? tl_polygon_copy(&tile->outline)
                                                : tl_polygon_from_rect((TlRect){0, 0, tl_rect_width(tile->rect),
                                                                                tl_rect_height(tile->rect)});
        for (size_t s = 0; s < tile->shapes.count; s++) {
            const TlPolygon *shape = &tile->shapes.items[s];
            for (size_t e = 0; e < shape->count; e++) {
                TlVec2 a = tl_polygon_edge_start(shape, e), b = tl_polygon_edge_end(shape, e);
                if (!on_tile_edge(&outline, a, b))
                    length += tl_distance(a, b);
            }
        }
        tl_polygon_free(&outline);
    }
    return length;
}

int tl_layout_cross_count(const TlLayout *layout) { return (int)layout->cross_points.count; }

/* ---- смещение сетки --------------------------------------------------------- */

TlAffine tl_layout_world_to_local(TlSpace *space) { return world_to_local(space); }

void tl_layout_normalize_offsets(const TlTileSettings *tile, double *offset_x, double *offset_y)
{
    TlPatternUnit unit;
    if (!tl_pattern_unit(tile, &unit))
        return;
    TlVec2 reduced = tl_pattern_reduce(&unit, tl_vec2(*offset_x, *offset_y));
    *offset_x = reduced.x;
    *offset_y = reduced.y;
}

/* Первая плитка модуля — от неё считаются «целая в углу» и «по центру». */
static TlRect first_tile(const TlTileSettings *tile)
{
    TlPatternUnit unit;
    return tl_pattern_unit(tile, &unit) ? unit.tiles[0].rect
                                        : (TlRect){0, 0, tl_tile_laid_width(tile), tl_tile_laid_height(tile)};
}

static void corner_signs(TlLayoutCorner corner, double *sign_x, double *sign_y)
{
    *sign_x = corner == TL_CORNER_TOP_LEFT || corner == TL_CORNER_BOTTOM_LEFT ? 1 : -1;
    *sign_y = corner == TL_CORNER_TOP_LEFT || corner == TL_CORNER_TOP_RIGHT ? 1 : -1;
}

TlVec2 tl_layout_local_corner(TlSpace *space, TlLayoutCorner corner)
{
    TlAffine to_local = world_to_local(space);
    double sign_x, sign_y;
    corner_signs(corner, &sign_x, &sign_y);
    TlVec2 best = {0, 0};
    const TlPolygon *room = tl_space_room_outline(space);
    for (size_t i = 0; i < room->count; i++) {
        TlVec2 local = tl_affine_apply(&to_local, room->items[i]);
        if (i == 0 || sign_x * local.x + sign_y * local.y < sign_x * best.x + sign_y * best.y)
            best = local;
    }
    return best;
}

void tl_layout_corner_offsets(TlSpace *space, TlLayoutCorner corner, double *offset_x, double *offset_y)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    TlRect first = first_tile(tile);
    double sign_x, sign_y;
    corner_signs(corner, &sign_x, &sign_y);
    TlVec2 vertex = tl_layout_local_corner(space, corner);
    *offset_x = vertex.x + sign_x * tile->wall_gap - (sign_x > 0 ? first.left : first.right);
    *offset_y = vertex.y + sign_y * tile->wall_gap - (sign_y > 0 ? first.top : first.bottom);
}

void tl_layout_center_offsets(TlSpace *space, bool on_joint, double *offset_x, double *offset_y)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    TlRect first = first_tile(tile);
    TlAffine to_local = world_to_local(space);
    TlPolygon local = tl_affine_apply_polygon(&to_local, tl_space_room_outline(space));
    TlVec2 center = tl_rect_center(tl_polygon_bounds(&local));
    tl_polygon_free(&local);
    if (on_joint) {
        *offset_x = center.x + tile->joint_width / 2 - first.left;
        *offset_y = center.y + tile->joint_width / 2 - first.top;
    } else {
        TlVec2 middle = tl_rect_center(first);
        *offset_x = center.x - middle.x;
        *offset_y = center.y - middle.y;
    }
}

bool tl_layout_optimize_narrow(TlSpace *space, double *offset_x, double *offset_y)
{
    TlPatternUnit unit;
    if (!tl_pattern_unit(tl_space_effective_tile(space), &unit))
        return false;
    bool found = false;
    int best_narrow = 0, best_total = 0, best_whole = 0;
    for (int a = 0; a < OPTIMIZE_STEPS; a++)
        for (int b = 0; b < OPTIMIZE_STEPS; b++) {
            TlVec2 offset = tl_pattern_lattice_point(&unit, (double)a / OPTIMIZE_STEPS, (double)b / OPTIMIZE_STEPS);
            TlLayoutOptions options = {true, true, offset.x, offset.y};
            TlLayout *layout = tl_layout_compute(space, &options);
            if (layout->too_many) {
                tl_layout_free(layout);
                return false;
            }
            int narrow = layout->narrow_count, total = tl_layout_total(layout), whole = layout->whole_count;
            bool better = !found || narrow < best_narrow ||
                          (narrow == best_narrow && (total < best_total ||
                                                     (total == best_total && whole > best_whole)));
            if (better) {
                found = true;
                best_narrow = narrow;
                best_total = total;
                best_whole = whole;
                *offset_x = options.offset_x;
                *offset_y = options.offset_y;
            }
            tl_layout_free(layout);
        }
    return found;
}

/*
 * Стены комнаты (tl_wall.h): развёртка и раскладки облицовки.
 *
 * Развёртка — стены в ряд, как их видно из комнаты, слева направо по обходу; между
 * стенами — зазор TL_WALL_GAP. Координаты развёртки: x — вдоль стен, y — вниз от
 * потолка (пол — на y = высота помещения), как у плана: рисовать можно тем же кодом.
 *
 * Раскладка — на каждый участок полосы (tl_wall_band_spans) каждой стены: прямоугольник
 * без проёмов, ряды — от низа участка целой плиткой, столбцы — от левого угла.
 * Между полосами — шов у верха нижней; у пола, верха облицовки и в углах — без отступа.
 * Сетку плитки стены можно сдвинуть (TlWallShift — у всех её полос); отверстия на стене
 * (TlWallPipe) сверлят плитку, как трубы в полу.
 *
 * Откосы проёма с откосами плиткой (TlWallOpening.reveal) — внутри проёма вдоль его краёв, как
 * развёрнутые в плоскость стены: слева, справа и сверху. Где плитка стены встречается с плиткой
 * откоса и на наружных углах стен (колонна, короб) — запил 45° (tl_layout_mark_miters).
 *
 * Лестницы комнаты — справа от стен, по столбцу на лестницу: снизу вверх подступенок, проступь,
 * подступенок… (развёрнутый профиль). Плитка — комнаты; у подступенка — от верха, у проступи — от
 * переднего края; на ребре ступени — запил 45°. Ступени считаются как пол.
 *
 * Под каждой стеной развёртки — полоса пола у этой стены глубиной TL_WALL_FLOOR_DEPTH, как
 * будто пол откинут вниз: видно, сходятся ли швы стены и пола («Швы как у пола»).
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "tl_layout.h"

#define MAX_WALL_LAYOUTS 256

TL_ARRAY_DEFINE(WallLayoutList, TlLayout *, wall_layout_list)
TL_ARRAY_DEFINE(RoomWallList, TlRoomWall, room_wall_list)
TL_ARRAY_DEFINE(WallHoleList, TlWallHole, wall_hole_list)

struct TlWallCache {
    /* по чему построено */
    TlWallFinish finish;
    double height;
    TlTileSettings room_tile;
    TlPolygon room_outline;
    TlWallOpeningList openings;
    TlWallPipeList pipes;
    TlWallShiftList shifts;
    TlPannoList pannos;
    TlFixtureList stairs;
    /* что построено */
    RoomWallList walls;
    TlDoubleArray stairs_x;  /* столбец каждой лестницы — начало в развёртке */
    double stairs_top;       /* верх самой высокой развёрнутой лестницы (y развёртки) */
    WallHoleList holes;
    TlTileSettings tiles[TL_WALL_BAND_COUNT];
    TlTileSettings stairs_tile;  /* плитка ступеней — комнаты */
    WallLayoutList layouts;
    size_t layout_wall[MAX_WALL_LAYOUTS]; /* параллельно layouts */
    TlWallBand layout_band[MAX_WALL_LAYOUTS];
    size_t layout_reveal[MAX_WALL_LAYOUTS]; /* откос: номер проёма в holes; иначе SIZE_MAX */
    size_t layout_stairs[MAX_WALL_LAYOUTS]; /* ступень: номер лестницы; иначе SIZE_MAX */
    int layout_step[MAX_WALL_LAYOUTS];      /* ступень × 2 + 1 у проступи */
    bool reflex[2 * 64];                     /* наружный угол у начала / конца стены (до 64 стен) */
};

/* ---- развёртка ---------------------------------------------------------------------- */

/* Смотрим из комнаты на стену edge: её левый конец — первый в развёртке? */
static bool edge_runs_left_to_right(const TlPolygon *outline, size_t edge)
{
    TlVec2 direction, normal;
    tl_polygon_edge_frame(outline, edge, &direction, &normal);
    TlVec2 right = {-normal.y, normal.x}; /* смотрим по внешней нормали; ось y плана — вниз */
    return tl_vec2_dot(direction, right) > 0;
}

/* Угол контура в вершине vertex — наружный (колонна, короб: внутренний угол комнаты больше 180°)? */
static bool reflex_vertex(const TlPolygon *outline, size_t vertex)
{
    size_t n = outline->count;
    TlVec2 prev = outline->items[(vertex + n - 1) % n], here = outline->items[vertex], next = outline->items[(vertex + 1) % n];
    double cross = tl_vec2_cross(tl_vec2_sub(here, prev), tl_vec2_sub(next, here));
    double area = 0;
    for (size_t i = 0; i < n; i++)
        area += tl_vec2_cross(outline->items[i], outline->items[(i + 1) % n]);
    return cross * area < -1e-6;
}

static void unfold(struct TlWallCache *cache, TlSpace *space)
{
    TlPolygon outline = tl_space_wall_outline(space);
    size_t count = outline.count;
    bool forward = count >= 3 && edge_runs_left_to_right(&outline, 0);
    double x = 0;
    for (size_t k = 0; k < count; k++) {
        size_t edge = forward ? k : count - 1 - k;
        TlVec2 a = tl_polygon_edge_start(&outline, edge), b = tl_polygon_edge_end(&outline, edge);
        double length = tl_distance(a, b);
        if (length < 1)
            continue;
        TlRoomWall wall = {forward ? a : b, forward ? b : a, length, x, edge, !forward};
        size_t index = cache->walls.count;
        if (index < 64) { /* левый конец — начало стороны (или конец у обратной) */
            cache->reflex[2 * index] = reflex_vertex(&outline, forward ? edge : (edge + 1) % count);
            cache->reflex[2 * index + 1] = reflex_vertex(&outline, forward ? (edge + 1) % count : edge);
        }
        room_wall_list_push(&cache->walls, wall);
        x += length + TL_WALL_GAP;
    }
    cache->stairs_top = 0;
    for (size_t k = 0; k < space->stairs.count; k++) { /* лестницы — столбцами справа от стен */
        tl_double_array_push(&cache->stairs_x, x);
        x += space->stairs.items[k].width + TL_WALL_GAP;
        double total = space->stairs.items[k].height + space->stairs.items[k].depth;
        cache->stairs_top = fmin(cache->stairs_top, space->height - total);
    }
    for (size_t i = 0; i < space->wall_openings.count; i++) {
        const TlWallOpening *opening = &space->wall_openings.items[i];
        for (size_t w = 0; w < cache->walls.count; w++) {
            const TlRoomWall *wall = &cache->walls.items[w];
            if (wall->edge != opening->wall)
                continue;
            double from = wall->reversed ? wall->length - opening->to : opening->from;
            double to = wall->reversed ? wall->length - opening->from : opening->to;
            TlRect rect = {wall->x + from, space->height - opening->top, wall->x + to, space->height - opening->bottom};
            wall_hole_list_push(&cache->holes, (TlWallHole){opening->opening, opening->fixture, opening->reveal, w, rect});
        }
    }
    tl_polygon_free(&outline);
}

/* ---- раскладки ------------------------------------------------------------------------ */

static TlPolygon rect_polygon(TlRect rect)
{
    const TlVec2 corners[] = {
        {rect.left, rect.top}, {rect.right, rect.top}, {rect.right, rect.bottom}, {rect.left, rect.bottom}};
    return tl_polygon_from_points(corners, 4);
}

static TlVec2 shift_of(const TlWallShiftList *shifts, size_t edge)
{
    for (size_t i = 0; i < shifts->count; i++)
        if (shifts->items[i].wall == edge)
            return shifts->items[i].offset;
    return tl_vec2(0, 0);
}

static void push_line(TlPolygonList *lines, TlVec2 a, TlVec2 b)
{
    const TlVec2 points[2] = {a, b};
    tl_polygon_list_push(lines, tl_polygon_from_points(points, 2));
}

/* Линии запила на стене w (в развёртке): края проёмов с откосами и наружные углы. */
static TlPolygonList wall_miters(const struct TlWallCache *cache, size_t w)
{
    TlPolygonList lines = {0};
    for (size_t i = 0; i < cache->holes.count; i++) {
        const TlWallHole *hole = &cache->holes.items[i];
        if (hole->wall != w || !(hole->reveal > 0))
            continue;
        TlRect r = hole->rect;
        push_line(&lines, tl_vec2(r.left, r.top), tl_vec2(r.left, r.bottom));
        push_line(&lines, tl_vec2(r.right, r.top), tl_vec2(r.right, r.bottom));
        push_line(&lines, tl_vec2(r.left, r.top), tl_vec2(r.right, r.top));
    }
    const TlRoomWall *wall = &cache->walls.items[w];
    for (int end = 0; w < 64 && end < 2; end++)
        if (cache->reflex[2 * w + end]) {
            double x = end ? wall->x + wall->length : wall->x;
            push_line(&lines, tl_vec2(x, 0), tl_vec2(x, cache->height));
        }
    return lines;
}

static void add_wall_layout(struct TlWallCache *cache, TlLayout *layout, size_t w, TlWallBand band, size_t reveal)
{
    if (cache->layouts.count >= MAX_WALL_LAYOUTS) {
        tl_layout_free(layout);
        return;
    }
    cache->layout_wall[cache->layouts.count] = w;
    cache->layout_band[cache->layouts.count] = band;
    cache->layout_reveal[cache->layouts.count] = reveal;
    cache->layout_stairs[cache->layouts.count] = SIZE_MAX;
    cache->layout_step[cache->layouts.count] = 0;
    wall_layout_list_push(&cache->layouts, layout);
}

/* Откосы проёма hole: слева и справа — от низа проёма до верхнего откоса, сверху — во всю ширину;
   плитка — от края проёма (там запил). */
static void lay_reveals(struct TlWallCache *cache, size_t hole_index)
{
    const TlWallHole *hole = &cache->holes.items[hole_index];
    const TlTileSettings *tile = &cache->tiles[TL_WALL_MAIN];
    TlRect r = hole->rect;
    double d = fmin(hole->reveal, fmin(tl_rect_width(r), tl_rect_height(r)) / 2);
    if (d < 1)
        return;
    TlPatternUnit unit;
    TlRect first = tl_pattern_unit(tile, &unit) ? unit.tiles[0].rect : (TlRect){0, 0, tile->tile_width, tile->tile_height};
    const struct {
        TlRect strip;
        double offset_x, offset_y; /* плитка — от края проёма */
        TlVec2 a, b;               /* край проёма — запил */
    } STRIPS[3] = {
        {{r.left, r.top + d, r.left + d, r.bottom}, 0, (r.bottom - r.top - d) - first.bottom, {r.left, r.top}, {r.left, r.bottom}},
        {{r.right - d, r.top + d, r.right, r.bottom}, d - first.right, (r.bottom - r.top - d) - first.bottom,
         {r.right, r.top}, {r.right, r.bottom}},
        {{r.left, r.top, r.right, r.top + d}, 0, 0, {r.left, r.top}, {r.right, r.top}},
    };
    for (int s = 0; s < 3; s++) {
        TlPolygon outline = rect_polygon(STRIPS[s].strip);
        const double insets[4] = {0, 0, 0, 0};
        TlSurface surface = {.outline = &outline, .insets = insets,
                             .origin = {STRIPS[s].strip.left, STRIPS[s].strip.top}, .angle = 0, .tile = tile};
        TlLayoutOptions options = {.use_offset = true, .offset_x = STRIPS[s].offset_x, .offset_y = STRIPS[s].offset_y};
        TlLayout *layout = tl_layout_compute_surface(&surface, &options);
        TlPolygonList miter = {0};
        push_line(&miter, STRIPS[s].a, STRIPS[s].b);
        tl_layout_mark_miters(layout, &miter);
        tl_polygon_list_free_all(&miter);
        add_wall_layout(cache, layout, hole->wall, TL_WALL_MAIN, hole_index);
        tl_polygon_free(&outline);
    }
}

/* Ступени лестницы k: подступенки — плитка от верха (под ребром), проступи — от переднего края. */
static void lay_stairs(struct TlWallCache *cache, size_t k)
{
    const TlFixture *stairs = &cache->stairs.items[k];
    const TlTileSettings *tile = &cache->stairs_tile;
    double x = cache->stairs_x.items[k], width = stairs->width;
    double riser = stairs->height / stairs->steps, tread = stairs->depth / stairs->steps, y = cache->height;
    TlPatternUnit unit;
    TlRect first = tl_pattern_unit(tile, &unit) ? unit.tiles[0].rect : (TlRect){0, 0, tile->tile_width, tile->tile_height};
    for (int step = 0; step < stairs->steps; step++) {
        for (int part = 0; part < 2; part++) { /* 0 — подступенок, 1 — проступь */
            double size = part ? tread : riser;
            TlRect rect = {x, y - size, x + width, y};
            TlPolygon outline = rect_polygon(rect);
            const double insets[4] = {0, 0, 0, 0};
            TlSurface surface = {.outline = &outline, .insets = insets, .origin = {rect.left, rect.top}, .angle = 0,
                                 .tile = tile};
            /* по ширине — плитка по середине марша; по высоте: подступенок — от верха, проступь — от низа (края) */
            TlLayoutOptions options = {.use_offset = true, .offset_x = (width - (first.right - first.left)) / 2,
                                       .offset_y = part ? size - first.bottom : 0};
            TlLayout *layout = tl_layout_compute_surface(&surface, &options);
            TlPolygonList miter = {0}; /* ребро ступени: верх подступенка = передний край проступи */
            double edge = part ? rect.bottom : rect.top;
            push_line(&miter, tl_vec2(rect.left, edge), tl_vec2(rect.right, edge));
            tl_layout_mark_miters(layout, &miter);
            tl_polygon_list_free_all(&miter);
            add_wall_layout(cache, layout, SIZE_MAX, TL_WALL_MAIN, SIZE_MAX);
            if (cache->layouts.items[cache->layouts.count - 1] == layout) {
                cache->layout_stairs[cache->layouts.count - 1] = k;
                cache->layout_step[cache->layouts.count - 1] = step * 2 + part;
            }
            tl_polygon_free(&outline);
            y -= size;
        }
    }
}

static void lay_span(struct TlWallCache *cache, size_t w, TlWallBand band, const double span[2], double limit)
{
    if (cache->layouts.count >= MAX_WALL_LAYOUTS)
        return;
    const TlRoomWall *wall = &cache->walls.items[w];
    const TlTileSettings *tile = &cache->tiles[band];
    TlRect rect = {wall->x, cache->height - span[1], wall->x + wall->length, cache->height - span[0]};
    TlPolygon outline = rect_polygon(rect);
    /* стороны: верх, право, низ, лево */
    const double insets[4] = {span[1] < limit - 0.5 ? tile->joint_width : 0, 0, 0, 0};
    TlPolygonList cutouts = {0};
    for (size_t i = 0; i < cache->holes.count; i++)
        if (cache->holes.items[i].wall == w)
            tl_polygon_list_push(&cutouts, rect_polygon(cache->holes.items[i].rect));
    for (size_t i = 0; i < cache->pannos.count; i++) { /* панно — со швом плитки вокруг */
        const TlPanno *panno = &cache->pannos.items[i];
        if (panno->wall != wall->edge)
            continue;
        double margin = tile->joint_width;
        TlRect around = {wall->x + panno->at.x - margin, cache->height - panno->at.y - tl_panno_height(panno) - margin,
                         wall->x + panno->at.x + tl_panno_width(panno) + margin, cache->height - panno->at.y + margin};
        tl_polygon_list_push(&cutouts, rect_polygon(around));
    }
    TlCircleList pipes = {0};
    for (size_t i = 0; i < cache->pipes.count; i++) {
        const TlWallPipe *pipe = &cache->pipes.items[i];
        if (pipe->wall == wall->edge)
            tl_circle_list_push(&pipes, (TlCircle){{wall->x + pipe->at.x, cache->height - pipe->at.y}, pipe->diameter});
    }
    TlSurface surface = {.outline = &outline,
                         .insets = insets,
                         .cutouts = &cutouts,
                         .pipes = &pipes,
                         .origin = {rect.left, rect.top},
                         .angle = 0,
                         .tile = tile};
    TlPatternUnit unit;
    double first_bottom = tl_pattern_unit(tile, &unit) ? unit.tiles[0].rect.bottom : tl_tile_laid_height(tile);
    TlVec2 shift = shift_of(&cache->shifts, wall->edge);
    TlLayoutOptions options = {.use_offset = true,
                               .offset_x = tile->offset_x + shift.x,
                               .offset_y = tile->offset_y - shift.y + (rect.bottom - rect.top) - first_bottom};
    TlLayout *layout = tl_layout_compute_surface(&surface, &options);
    TlPolygonList miters = wall_miters(cache, w);
    tl_layout_mark_miters(layout, &miters);
    tl_polygon_list_free_all(&miters);
    add_wall_layout(cache, layout, w, band, SIZE_MAX);
    tl_polygon_list_free_all(&cutouts);
    tl_circle_list_free(&pipes);
    tl_polygon_free(&outline);
}

static double finish_limit(const TlWallFinish *finish, double height)
{
    return finish->height > 0 ? fmin(finish->height, height) : height;
}

static void lay_walls(struct TlWallCache *cache)
{
    double limit = finish_limit(&cache->finish, cache->height);
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++) {
        double spans[2][2];
        size_t count = tl_wall_band_spans(&cache->finish, (TlWallBand)band, cache->height, spans);
        for (size_t s = 0; s < count; s++)
            for (size_t w = 0; w < cache->walls.count; w++)
                lay_span(cache, w, (TlWallBand)band, spans[s], limit);
    }
    for (size_t i = 0; i < cache->holes.count; i++)
        if (cache->holes.items[i].reveal > 0)
            lay_reveals(cache, i);
}

/* ---- кэш ------------------------------------------------------------------------------ */

static bool same_openings(const TlWallOpeningList *a, const TlWallOpeningList *b)
{
    return a->count == b->count && (a->count == 0 || memcmp(a->items, b->items, a->count * sizeof *a->items) == 0);
}

static bool cache_is_current(const struct TlWallCache *cache, TlSpace *space)
{
    const TlPolygon *room = tl_space_room_outline(space);
    return cache && cache->height == space->height && tl_wall_finishes_equal(&cache->finish, &space->walls) &&
           tl_tile_settings_equal(&cache->room_tile, tl_space_effective_tile(space)) &&
           cache->room_outline.count == room->count &&
           (room->count == 0 || memcmp(cache->room_outline.items, room->items, room->count * sizeof(TlVec2)) == 0) &&
           same_openings(&cache->openings, &space->wall_openings) &&
           cache->pipes.count == space->wall_pipes.count &&
           (cache->pipes.count == 0 ||
            memcmp(cache->pipes.items, space->wall_pipes.items, cache->pipes.count * sizeof(TlWallPipe)) == 0) &&
           cache->pannos.count == space->pannos.count &&
           (cache->pannos.count == 0 ||
            memcmp(cache->pannos.items, space->pannos.items, cache->pannos.count * sizeof(TlPanno)) == 0) &&
           cache->stairs.count == space->stairs.count &&
           (cache->stairs.count == 0 ||
            memcmp(cache->stairs.items, space->stairs.items, cache->stairs.count * sizeof(TlFixture)) == 0) &&
           cache->shifts.count == space->wall_shifts.count &&
           (cache->shifts.count == 0 ||
            memcmp(cache->shifts.items, space->wall_shifts.items, cache->shifts.count * sizeof(TlWallShift)) == 0);
}

void tl_wall_cache_free(struct TlWallCache *cache)
{
    if (!cache)
        return;
    for (size_t i = 0; i < cache->layouts.count; i++)
        tl_layout_free(cache->layouts.items[i]);
    wall_layout_list_free(&cache->layouts);
    room_wall_list_free(&cache->walls);
    wall_hole_list_free(&cache->holes);
    tl_polygon_free(&cache->room_outline);
    tl_wall_opening_list_free(&cache->openings);
    tl_wall_pipe_list_free(&cache->pipes);
    tl_wall_shift_list_free(&cache->shifts);
    tl_panno_list_free(&cache->pannos);
    tl_fixture_list_free(&cache->stairs);
    tl_double_array_free(&cache->stairs_x);
    free(cache);
}

static struct TlWallCache *wall_cache(TlSpace *space)
{
    if (cache_is_current(space->wall_cache, space))
        return space->wall_cache;
    tl_wall_cache_free(space->wall_cache);
    struct TlWallCache *cache = TL_NEW(struct TlWallCache);
    const TlTileSettings *room_tile = tl_space_effective_tile(space);
    cache->finish = space->walls;
    cache->height = space->height;
    cache->room_tile = *room_tile;
    cache->room_outline = tl_polygon_copy(tl_space_room_outline(space));
    cache->openings = tl_wall_opening_list_copy(&space->wall_openings);
    cache->pipes = tl_wall_pipe_list_copy(&space->wall_pipes);
    cache->shifts = tl_wall_shift_list_copy(&space->wall_shifts);
    cache->pannos = tl_panno_list_copy(&space->pannos);
    cache->stairs = tl_fixture_list_copy(&space->stairs);
    cache->stairs_tile = *room_tile;
    cache->stairs_tile.enabled = true;
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++) {
        cache->tiles[band] = tl_zone_tile(&space->walls.bands[band], room_tile);
        cache->tiles[band].enabled = true;
    }
    unfold(cache, space);
    if (space->walls.enabled)
        lay_walls(cache);
    for (size_t k = 0; k < cache->stairs.count; k++) /* ступени — и без облицовки стен */
        lay_stairs(cache, k);
    space->wall_cache = cache;
    return cache;
}

/* ---- доступ ------------------------------------------------------------------------- */

size_t tl_space_wall_count(TlSpace *space) { return wall_cache(space)->walls.count; }

const TlRoomWall *tl_space_wall_at(TlSpace *space, size_t index)
{
    struct TlWallCache *cache = wall_cache(space);
    return index < cache->walls.count ? &cache->walls.items[index] : NULL;
}

size_t tl_space_wall_hole_count(TlSpace *space) { return wall_cache(space)->holes.count; }

const TlWallHole *tl_space_wall_hole_at(TlSpace *space, size_t index)
{
    struct TlWallCache *cache = wall_cache(space);
    return index < cache->holes.count ? &cache->holes.items[index] : NULL;
}

TlRect tl_space_walls_bounds(TlSpace *space)
{
    struct TlWallCache *cache = wall_cache(space);
    double width = 0;
    if (cache->walls.count > 0) {
        const TlRoomWall *last = &cache->walls.items[cache->walls.count - 1];
        width = last->x + last->length;
    }
    if (cache->stairs_x.count) {
        size_t last = cache->stairs_x.count - 1;
        width = cache->stairs_x.items[last] + cache->stairs.items[last].width;
    }
    return (TlRect){0, cache->stairs_top, width, space->height + TL_WALL_FLOOR_DEPTH};
}

double tl_space_walls_finish_top(TlSpace *space) { return finish_limit(&space->walls, space->height); }

size_t tl_space_wall_layout_count(TlSpace *space) { return wall_cache(space)->layouts.count; }

TlLayout *tl_space_wall_layout_at(TlSpace *space, size_t index, size_t *wall, TlWallBand *band)
{
    struct TlWallCache *cache = wall_cache(space);
    if (index >= cache->layouts.count)
        return NULL;
    if (wall)
        *wall = cache->layout_wall[index];
    if (band)
        *band = cache->layout_band[index];
    return cache->layouts.items[index];
}

const TlTileSettings *tl_space_wall_tile(TlSpace *space, TlWallBand band) { return &wall_cache(space)->tiles[band]; }

double tl_space_walls_area(TlSpace *space)
{
    double area = 0;
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++)
        if (tl_space_wall_layout_stairs(space, i, NULL, NULL) == SIZE_MAX) /* ступени — как пол */
            area += tl_space_wall_layout_at(space, i, NULL, NULL)->floor_area;
    return area;
}

/* ---- точки на стенах ------------------------------------------------------------------ */

size_t tl_space_wall_of_edge(TlSpace *space, size_t edge)
{
    struct TlWallCache *cache = wall_cache(space);
    for (size_t w = 0; w < cache->walls.count; w++)
        if (cache->walls.items[w].edge == edge)
            return w;
    return SIZE_MAX;
}

TlVec2 tl_space_wall_to_development(TlSpace *space, size_t wall, TlVec2 at)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    return room_wall ? tl_vec2(room_wall->x + at.x, space->height - at.y) : at;
}

bool tl_space_wall_from_development(TlSpace *space, TlVec2 point, size_t *wall, TlVec2 *at)
{
    struct TlWallCache *cache = wall_cache(space);
    size_t best = SIZE_MAX;
    double best_distance = TL_WALL_GAP / 2;
    for (size_t w = 0; w < cache->walls.count; w++) {
        const TlRoomWall *candidate = &cache->walls.items[w];
        double distance = fmax(0, fmax(candidate->x - point.x, point.x - candidate->x - candidate->length));
        if (distance <= best_distance) {
            best = w;
            best_distance = distance;
        }
    }
    if (best == SIZE_MAX || point.y < -TL_WALL_GAP / 2 || point.y > space->height)
        return false;
    const TlRoomWall *found = &cache->walls.items[best];
    *wall = best;
    *at = tl_vec2(fmin(fmax(point.x - found->x, 0), found->length), fmin(fmax(space->height - point.y, 0), space->height));
    return true;
}

TlVec2 tl_space_wall_offset(TlSpace *space, size_t wall)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    return room_wall ? shift_of(&space->wall_shifts, room_wall->edge) : tl_vec2(0, 0);
}

void tl_space_set_wall_offset(TlSpace *space, size_t wall, TlVec2 offset)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    if (!room_wall)
        return;
    size_t edge = room_wall->edge;
    for (size_t i = 0; i < space->wall_shifts.count; i++)
        if (space->wall_shifts.items[i].wall == edge) {
            space->wall_shifts.items[i].offset = offset;
            return;
        }
    tl_wall_shift_list_push(&space->wall_shifts, (TlWallShift){edge, offset});
}

/* ---- пол под стеной ---------------------------------------------------------------------- */

TlAffine tl_space_wall_floor_affine(TlSpace *space, size_t wall)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    if (!room_wall)
        return tl_affine_identity();
    TlVec2 along = tl_vec2_normalized(tl_vec2_sub(room_wall->end, room_wall->start));
    /* в комнату: вид из комнаты — левый конец start, правый end; комната — «перед» стеной */
    TlVec2 inward = {along.y, -along.x};
    TlPolygon outline = tl_space_wall_outline(space);
    TlVec2 middle = tl_vec2_midpoint(room_wall->start, room_wall->end);
    if (!tl_polygon_contains(&outline, tl_vec2_add(middle, tl_vec2_scale(inward, 5))))
        inward = tl_vec2_scale(inward, -1);
    tl_polygon_free(&outline);
    /* развёртка: x = wall.x + (p − start)·along, y = height + (p − start)·inward */
    TlAffine m = {along.x, inward.x, along.y, inward.y, 0, 0};
    TlVec2 start = tl_affine_apply(&m, room_wall->start);
    m.tx = room_wall->x - start.x;
    m.ty = space->height - start.y;
    return m;
}

/* Левый и правый края плиток раскладки, которые доходят до линии y (в развёртке): там их швы. */
static void bottom_edges(const TlLayout *layout, const TlAffine *to_development, double y, double tolerance,
                         TlDoubleArray *edges)
{
    for (size_t t = 0; t < layout->tiles.count; t++) {
        const TlTile *tile = &layout->tiles.items[t];
        TlVec2 corners[4] = {{tile->rect.left, tile->rect.top}, {tile->rect.right, tile->rect.top},
                             {tile->rect.right, tile->rect.bottom}, {tile->rect.left, tile->rect.bottom}};
        TlRect box = {INFINITY, INFINITY, -INFINITY, -INFINITY};
        for (int k = 0; k < 4; k++) {
            TlVec2 p = tl_affine_apply(to_development, corners[k]);
            box.left = fmin(box.left, p.x);
            box.right = fmax(box.right, p.x);
            box.top = fmin(box.top, p.y);
            box.bottom = fmax(box.bottom, p.y);
        }
        if (box.top <= y + tolerance && box.bottom >= y - tolerance) { /* у подрезки rect — вся плитка */
            tl_double_array_push(edges, box.left);
            tl_double_array_push(edges, box.right);
        }
    }
}

/* Оси раскладки пола повёрнуты на угол, кратный 90° к стене? */
static bool square_to_wall(const TlAffine *m)
{
    double angle = fmod(fabs(atan2(m->b, m->a) * 180 / TL_PI), 90);
    return angle < 0.01 || angle > 89.99;
}

bool tl_space_wall_align_floor(TlSpace *space, size_t wall)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    if (!room_wall || !space->walls.enabled)
        return false;
    TlAffine to_development = tl_space_wall_floor_affine(space, wall);
    /* швы пола у стены: края плиток, касающихся стены (основной пол или бордюр) */
    TlDoubleArray floor_edges = {0};
    double floor_gap = 0;
    for (size_t l = 0; l < tl_space_floor_layout_count(space); l++) {
        const TlLayout *layout = tl_space_layout_at(space, l);
        const TlTileSettings *tile = tl_space_layout_tile(space, l);
        TlAffine m = tl_affine_multiply(&to_development, &layout->local_to_world);
        if (layout->too_many || !square_to_wall(&m) || !tile->enabled)
            continue;
        size_t before = floor_edges.count;
        bottom_edges(layout, &m, space->height + tile->wall_gap, 1.0, &floor_edges);
        if (floor_edges.count > before)
            floor_gap = tile->joint_width;
    }
    /* швы стены — у нижнего ряда (полоса от пола) */
    TlDoubleArray wall_edges = {0};
    double wall_gap = 0;
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
        size_t layout_wall = SIZE_MAX;
        TlWallBand band = TL_WALL_MAIN;
        const TlLayout *layout = tl_space_wall_layout_at(space, i, &layout_wall, &band);
        if (!layout || layout_wall != wall || layout->too_many)
            continue;
        size_t before = wall_edges.count;
        bottom_edges(layout, &layout->local_to_world, space->height, 0.5, &wall_edges);
        if (wall_edges.count > before) {
            wall_gap = tl_space_wall_tile(space, band)->joint_width;
            break;
        }
    }
    /* сдвиг, при котором шов стены ближе всего к шву пола: середины швов совпадают */
    double best = INFINITY;
    for (size_t f = 0; f < floor_edges.count; f++) {
        double floor_joint = floor_edges.items[f] + (f % 2 ? floor_gap / 2 : -floor_gap / 2);
        if (floor_joint < room_wall->x + 1 || floor_joint > room_wall->x + room_wall->length - 1)
            continue; /* в углу шва нет */
        for (size_t w = 0; w < wall_edges.count; w++) {
            double wall_joint = wall_edges.items[w] + (w % 2 ? wall_gap / 2 : -wall_gap / 2);
            if (fabs(floor_joint - wall_joint) < fabs(best))
                best = floor_joint - wall_joint;
        }
    }
    tl_double_array_free(&floor_edges);
    tl_double_array_free(&wall_edges);
    if (!isfinite(best))
        return false;
    TlVec2 offset = tl_space_wall_offset(space, wall);
    tl_space_set_wall_offset(space, wall, tl_vec2(offset.x + best, offset.y));
    return true;
}

/* ---- привязка на развёртке ------------------------------------------------------------- */

static void consider(double value, double target, double tolerance, double *best, double *best_distance)
{
    double distance = fabs(value - target);
    if (distance < tolerance && distance < *best_distance) {
        *best = value;
        *best_distance = distance;
    }
}

TlVec2 tl_space_walls_snap(TlSpace *space, TlVec2 point, double tolerance)
{
    struct TlWallCache *cache = wall_cache(space);
    double x = point.x, y = point.y, dx = INFINITY, dy = INFINITY;
    consider(space->height, point.y, tolerance, &y, &dy); /* пол и потолок */
    consider(0, point.y, tolerance, &y, &dy);
    for (size_t w = 0; w < cache->walls.count; w++) { /* углы */
        consider(cache->walls.items[w].x, point.x, tolerance, &x, &dx);
        consider(cache->walls.items[w].x + cache->walls.items[w].length, point.x, tolerance, &x, &dx);
    }
    for (size_t i = 0; i < cache->holes.count; i++) { /* края проёмов */
        TlRect r = cache->holes.items[i].rect;
        consider(r.left, point.x, tolerance, &x, &dx);
        consider(r.right, point.x, tolerance, &x, &dx);
        consider(r.top, point.y, tolerance, &y, &dy);
        consider(r.bottom, point.y, tolerance, &y, &dy);
    }
    for (size_t l = 0; l < cache->layouts.count; l++) { /* швы плитки: середины */
        const TlLayout *layout = cache->layouts.items[l];
        double half = cache->tiles[cache->layout_band[l]].joint_width / 2;
        TlVec2 origin = tl_affine_apply(&layout->local_to_world, tl_vec2(0, 0));
        if (fabs(layout->local_to_world.b) > 1e-9) /* повёрнутая плитка — швы не по осям */
            continue;
        for (size_t t = 0; t < layout->tiles.count && !layout->too_many; t++) {
            TlRect r = layout->tiles.items[t].rect;
            if (point.y < origin.y + r.top - tolerance || point.y > origin.y + r.bottom + tolerance ||
                point.x < origin.x + r.left - tolerance || point.x > origin.x + r.right + tolerance)
                continue;
            consider(origin.x + r.left - half, point.x, tolerance, &x, &dx);
            consider(origin.x + r.right + half, point.x, tolerance, &x, &dx);
            consider(origin.y + r.top - half, point.y, tolerance, &y, &dy);
            consider(origin.y + r.bottom + half, point.y, tolerance, &y, &dy);
        }
    }
    return tl_vec2(x, y);
}

size_t tl_space_wall_layout_reveal(TlSpace *space, size_t index)
{
    struct TlWallCache *cache = wall_cache(space);
    return index < cache->layouts.count ? cache->layout_reveal[index] : SIZE_MAX;
}

size_t tl_space_wall_layout_stairs(TlSpace *space, size_t index, int *step, bool *tread)
{
    struct TlWallCache *cache = wall_cache(space);
    if (index >= cache->layouts.count || cache->layout_stairs[index] == SIZE_MAX)
        return SIZE_MAX;
    if (step)
        *step = cache->layout_step[index] / 2;
    if (tread)
        *tread = cache->layout_step[index] % 2;
    return cache->layout_stairs[index];
}

size_t tl_space_stairs_count(TlSpace *space) { return wall_cache(space)->stairs.count; }

bool tl_space_stairs_column(TlSpace *space, size_t index, TlRect *column)
{
    struct TlWallCache *cache = wall_cache(space);
    if (index >= cache->stairs.count)
        return false;
    const TlFixture *stairs = &cache->stairs.items[index];
    double x = cache->stairs_x.items[index];
    *column = (TlRect){x, space->height - stairs->height - stairs->depth, x + stairs->width, space->height};
    return true;
}

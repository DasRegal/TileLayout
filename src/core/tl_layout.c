#include "tl_layout.h"

#include <math.h>
#include <string.h>

#include "tl_regions.h"

#define FULL_TILE_RATIO 0.999   /* кусок не меньше этой доли плитки — целая плитка */
#define FIGURED_RATIO 0.99      /* площадь меньше этой доли габарита — фигурный кусок */
#define MIN_PIECE_AREA 1.0      /* мм² */
#define CLIP_PROBE 3.0          /* мм от шва внутрь плитки при проверке СВП */
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
    TlPolygonList holes, joint_strips;
    TlTileSettings tile;
};

/* ---- оси раскладки ---------------------------------------------------------- */

static TlAffine local_frame(const TlPolygon *frame, double rotation_degrees)
{
    TlRect bounds = tl_polygon_bounds(frame);
    return tl_affine_rotation_translation(tl_vec2(bounds.left, bounds.top), rotation_degrees);
}

static TlAffine world_to_local(TlSpace *space)
{
    TlAffine to_world = local_frame(tl_space_frame(space), tl_space_effective_tile(space)->rotation_degrees);
    return tl_affine_inverted(&to_world);
}

static LocalFloor make_local_floor(TlSpace *space, const TlAffine *to_local, double wall_gap)
{
    LocalFloor floor = {0};
    TlPolygon outline = tl_affine_apply_polygon(to_local, &space->outline);
    floor.outline = wall_gap > 0 ? tl_polygon_offset_uniform(&outline, -wall_gap) : tl_polygon_copy(&outline);
    tl_polygon_free(&outline);
    for (size_t i = 0; i < space->holes.count; i++) {
        TlPolygon hole = tl_affine_apply_polygon(to_local, &space->holes.items[i]);
        tl_polygon_list_push(&floor.holes,
                             wall_gap > 0 ? tl_polygon_offset_uniform(&hole, wall_gap) : tl_polygon_copy(&hole));
        tl_polygon_free(&hole);
    }
    for (size_t i = 0; i < space->joint_strips.count; i++)
        tl_polygon_list_push(&floor.joint_strips, tl_affine_apply_polygon(to_local, &space->joint_strips.items[i]));
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

static void free_tile(TlTile *tile) { tl_polygon_list_free_all(&tile->shapes); }

static TlPolygon shape_from_tile_corner(TlPolygon clipped, TlRect rect)
{
    tl_polygon_clean(&clipped, 0.01);
    tl_polygon_translate(&clipped, tl_vec2(-rect.left, -rect.top));
    return clipped;
}

static bool crosses_joint(const LocalFloor *floor, TlRect rect)
{
    TlPolygon tile_outline = tl_polygon_from_rect(rect);
    bool crosses = false;
    for (size_t i = 0; i < floor->joint_strips.count && !crosses; i++)
        crosses = tl_rect_intersects(tl_polygon_bounds(&floor->joint_strips.items[i]), rect) &&
                  tl_polygons_overlap_area(&floor->joint_strips.items[i], &tile_outline) >= MIN_PIECE_AREA;
    tl_polygon_free(&tile_outline);
    return crosses;
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

static void set_piece_size(TlTile *tile, const TlTileSettings *settings, TlRect piece_bounds)
{
    tile->piece_width = fmin(tl_rect_width(piece_bounds), settings->tile_width);
    tile->piece_height = fmin(tl_rect_height(piece_bounds), settings->tile_height);
    tile->center = tl_rect_center(piece_bounds);
}

/* Плитка, через которую проходит шов: куски по обе стороны — разбиением
   плоскости (плитка ∩ пол − перегородки − швы). Номер — на самом большом
   куске, размеры подрезки — по нему же; узкая — если узкий хоть один кусок. */
static bool classify_tile_across_joint(const LocalFloor *floor, TlRect rect, const TlTileSettings *settings,
                                       bool fast, TlTile *tile)
{
    TlPolygonList cutters = {0};
    tl_polygon_list_push(&cutters, tl_polygon_from_rect(rect));
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
    if (area >= settings->tile_width * settings->tile_height * FULL_TILE_RATIO) {
        tile->kind = TL_TILE_WHOLE;
        tl_region_list_free_all(&pieces);
        return true;
    }
    set_piece_size(tile, settings, largest_bounds);
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

/* Заполняет tile, если плитка задевает пол; false — не задевает. */
static bool classify_tile(const LocalFloor *floor, TlRect rect, const TlTileSettings *settings,
                          bool fast, TlTile *tile)
{
    if (crosses_joint(floor, rect))
        return classify_tile_across_joint(floor, rect, settings, fast, tile);

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

    double tile_area = settings->tile_width * settings->tile_height;
    *tile = (TlTile){.rect = rect, .center = tl_rect_center(rect)};
    if (area >= tile_area * FULL_TILE_RATIO) {
        tile->kind = TL_TILE_WHOLE;
        tl_polygon_free(&clipped);
        tl_polygon_list_free_all(&hole_pieces);
        return true;
    }
    TlRect piece_bounds = tl_polygon_bounds(&clipped);
    set_piece_size(tile, settings, piece_bounds);
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

/* ---- СВП -------------------------------------------------------------------- */

/* СВП — в швах между двумя плитками: на нижней и правой стороне каждой
 * плитки (шов считается один раз). Точка ставится, только если по обе
 * стороны шва есть пол: у стен и перегородок СВП нет. */
static void place_clips(TlLayout *layout, const TlTileSettings *settings)
{
    bool width_is_long = settings->tile_width >= settings->tile_height;
    bool height_is_long = settings->tile_height >= settings->tile_width;
    int along_width = width_is_long ? settings->clips_per_long_side : settings->clips_per_short_side;
    int along_height = height_is_long ? settings->clips_per_long_side : settings->clips_per_short_side;
    double w = settings->tile_width, h = settings->tile_height, gap = settings->joint_width;

    for (size_t i = 0; i < layout->tiles.count; i++) {
        TlRect r = layout->tiles.items[i].rect;
        for (int k = 0; k < along_width; k++) {
            double x = r.left + w * (k + 0.5) / along_width;
            if (floor_contains(layout, tl_vec2(x, r.bottom - CLIP_PROBE)) &&
                floor_contains(layout, tl_vec2(x, r.bottom + gap + CLIP_PROBE)))
                tl_polygon_push(&layout->clip_points, tl_vec2(x, r.bottom + gap / 2));
        }
        for (int k = 0; k < along_height; k++) {
            double y = r.top + h * (k + 0.5) / along_height;
            if (floor_contains(layout, tl_vec2(r.right - CLIP_PROBE, y)) &&
                floor_contains(layout, tl_vec2(r.right + gap + CLIP_PROBE, y)))
                tl_polygon_push(&layout->clip_points, tl_vec2(r.right + gap / 2, y));
        }
    }
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

TlLayout *tl_layout_compute(TlSpace *space, const TlLayoutOptions *options)
{
    const TlLayoutOptions defaults = {0};
    if (!options)
        options = &defaults;
    const TlTileSettings *settings = tl_space_effective_tile(space);
    TlLayout *layout = TL_NEW(TlLayout);
    layout->local_to_world = local_frame(tl_space_frame(space), settings->rotation_degrees);
    layout->floor_area = tl_space_area(space);
    if (space->outline.count < 3)
        return layout;

    TlAffine to_local = tl_affine_inverted(&layout->local_to_world);
    LocalFloor room_floor = make_local_floor(space, &to_local, settings->wall_gap);
    layout->floor_outline = room_floor.outline;
    layout->floor_holes = room_floor.holes;
    layout->joint_strips = room_floor.joint_strips;
    TlRect bounds = tl_polygon_bounds(&room_floor.outline);
    layout->local_bounds = bounds;

    double w = settings->tile_width, h = settings->tile_height;
    double pitch_x = w + settings->joint_width, pitch_y = h + settings->joint_width;
    if (w <= 0 || h <= 0 || pitch_x <= 0 || pitch_y <= 0 || room_floor.outline.count < 3)
        return layout;
    if ((tl_rect_width(bounds) / pitch_x + 2) * (tl_rect_height(bounds) / pitch_y + 2) > TL_LAYOUT_MAX_TILES) {
        layout->too_many = true;
        return layout;
    }

    double offset_x = options->use_offset ? options->offset_x : settings->offset_x;
    double offset_y = options->use_offset ? options->offset_y : settings->offset_y;
    int first_row = (int)floor((bounds.top - offset_y - h) / pitch_y);
    int last_row = (int)ceil((bounds.bottom - offset_y) / pitch_y);
    for (int row = first_row; row <= last_row; row++) {
        double y = offset_y + row * pitch_y;
        if (y > bounds.bottom || y + h < bounds.top)
            continue;
        double row_shift = fmod(row * settings->row_shift, 1.0);
        if (row_shift < 0)
            row_shift += 1.0;
        double row_start = offset_x + row_shift * pitch_x;
        int first_column = (int)floor((bounds.left - row_start - w) / pitch_x);
        int last_column = (int)ceil((bounds.right - row_start) / pitch_x);
        for (int column = first_column; column <= last_column; column++) {
            TlRect rect = {row_start + column * pitch_x, y, row_start + column * pitch_x + w, y + h};
            TlTile tile;
            if (!tl_rect_intersects(rect, bounds) || !classify_tile(&room_floor, rect, settings, options->fast, &tile))
                continue;
            tile.cell = (TlCell){column, row};
            tile.number = (int)layout->tiles.count + 1; /* по рядам, слева направо */
            tl_tile_list_push(&layout->tiles, tile);
            count_tile(layout, tile.kind);
        }
    }
    if (!options->fast)
        place_clips(layout, settings);
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
        if (layout->tiles.items[i].cell.column == cell.column && layout->tiles.items[i].cell.row == cell.row)
            return &layout->tiles.items[i];
    return NULL;
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

void tl_layout_cache_free(struct TlLayoutCache *cache)
{
    if (!cache)
        return;
    tl_layout_free(cache->layout);
    tl_polygon_free(&cache->outline);
    tl_polygon_free(&cache->frame);
    tl_polygon_list_free_all(&cache->holes);
    tl_polygon_list_free_all(&cache->joint_strips);
    free(cache);
}

static bool cache_is_current(const struct TlLayoutCache *cache, TlSpace *space)
{
    return cache && polygons_equal(&cache->outline, &space->outline) &&
           polygon_lists_equal(&cache->holes, &space->holes) &&
           polygon_lists_equal(&cache->joint_strips, &space->joint_strips) &&
           polygons_equal(&cache->frame, tl_space_frame(space)) &&
           tl_tile_settings_equal(&cache->tile, tl_space_effective_tile(space));
}

TlLayout *tl_space_layout(TlSpace *space)
{
    if (cache_is_current(space->layout_cache, space))
        return space->layout_cache->layout;
    tl_layout_cache_free(space->layout_cache);
    struct TlLayoutCache *cache = TL_NEW(struct TlLayoutCache);
    cache->layout = tl_layout_compute(space, NULL);
    cache->outline = tl_polygon_copy(&space->outline);
    cache->frame = tl_polygon_copy(tl_space_frame(space));
    cache->holes = tl_polygon_list_deep_copy(&space->holes);
    cache->joint_strips = tl_polygon_list_deep_copy(&space->joint_strips);
    cache->tile = *tl_space_effective_tile(space);
    space->layout_cache = cache;
    return cache->layout;
}

/* ---- клей и СВП ----------------------------------------------------------- */

static int bags_for(double amount, double per_bag)
{
    return per_bag > 0 ? (int)ceil(amount / per_bag - 1e-9) : 0;
}

double tl_layout_glue_kg(TlSpace *space, const TlLayout *layout)
{
    const TlTileSettings *settings = tl_space_effective_tile(space);
    return layout->floor_area / 1e6 * settings->glue_kg_per_m2_mm * settings->glue_layer_mm;
}

int tl_layout_glue_bags(TlSpace *space, const TlLayout *layout)
{
    return bags_for(tl_layout_glue_kg(space, layout), tl_space_effective_tile(space)->glue_bag_kg);
}

int tl_layout_clip_packs(TlSpace *space, const TlLayout *layout)
{
    return bags_for((double)layout->clip_points.count, tl_space_effective_tile(space)->clips_per_pack);
}

/* ---- смещение сетки --------------------------------------------------------- */

void tl_layout_normalize_offsets(const TlTileSettings *tile, double *offset_x, double *offset_y)
{
    double pitch_x = tile->tile_width + tile->joint_width;
    double pitch_y = tile->tile_height + tile->joint_width;
    if (pitch_x <= 0 || pitch_y <= 0)
        return;
    double rows = floor(*offset_y / pitch_y);
    *offset_y -= rows * pitch_y;
    double shift = fmod(rows * tile->row_shift, 1.0);
    if (shift < 0)
        shift += 1.0;
    double x = fmod(*offset_x - shift * pitch_x, pitch_x);
    *offset_x = x < 0 ? x + pitch_x : x;
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
    double sign_x, sign_y;
    corner_signs(corner, &sign_x, &sign_y);
    TlVec2 vertex = tl_layout_local_corner(space, corner);
    *offset_x = vertex.x + sign_x * tile->wall_gap - (sign_x > 0 ? 0 : tile->tile_width);
    *offset_y = vertex.y + sign_y * tile->wall_gap - (sign_y > 0 ? 0 : tile->tile_height);
}

void tl_layout_center_offsets(TlSpace *space, bool on_joint, double *offset_x, double *offset_y)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    TlAffine to_local = world_to_local(space);
    TlPolygon local = tl_affine_apply_polygon(&to_local, tl_space_room_outline(space));
    TlVec2 center = tl_rect_center(tl_polygon_bounds(&local));
    tl_polygon_free(&local);
    if (on_joint) {
        *offset_x = center.x + tile->joint_width / 2;
        *offset_y = center.y + tile->joint_width / 2;
    } else {
        *offset_x = center.x - tile->tile_width / 2;
        *offset_y = center.y - tile->tile_height / 2;
    }
}

bool tl_layout_optimize_narrow(TlSpace *space, double *offset_x, double *offset_y)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    double pitch_x = tile->tile_width + tile->joint_width;
    double pitch_y = tile->tile_height + tile->joint_width;
    bool found = false;
    int best_narrow = 0, best_total = 0, best_whole = 0;
    for (int a = 0; a < OPTIMIZE_STEPS; a++)
        for (int b = 0; b < OPTIMIZE_STEPS; b++) {
            TlLayoutOptions options = {true, true, a * pitch_x / OPTIMIZE_STEPS, b * pitch_y / OPTIMIZE_STEPS};
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

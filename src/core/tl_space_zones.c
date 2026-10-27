/*
 * Зоны пола комнаты (tl_zone.h): их поверхности, раскладки и то, что они
 * вырезают из основного пола.
 *
 * Бордюр — полоса между контуром комнаты (без пола под дверьми) и тем же
 * контуром, сдвинутым внутрь на ширину бордюра. Полоса режется на куски по
 * стенам: у каждой стены своя раскладка, ось X — вдоль стены, ось Y — в
 * комнату, первый ряд — от стены; между кусками — диагональ угла («на ус»).
 * Ковёр — прямоугольник в осях раскладки комнаты.
 *
 * Зона занимает всю свою ширину (бордюр 300 — ровно плитка 300), шов между
 * зоной и основным полом — со стороны основного пола (его шов); между кусками
 * бордюра в углах — шов бордюра; от стены плитка бордюра отступает на свой wall_gap.
 */
#include <math.h>
#include <string.h>

#include "tl_layout.h"
#include "tl_regions.h"

#define ON_WALL_MM 1.0

#define MAX_ZONE_LAYOUTS 64 /* бордюр — по раскладке на стену */

TL_ARRAY_DEFINE(ZoneLayoutList, TlLayout *, zone_layout_list)

struct TlZoneCache {
    /* по чему построено — для проверки, не устарело ли */
    TlZone zones[TL_ZONE_COUNT];
    TlTileSettings room_tile;
    TlPolygon outline, room_outline, frame;
    TlPolygonList holes, joint_strips;
    TlCircleList pipes;
    /* что построено */
    TlTileSettings tiles[TL_ZONE_COUNT]; /* настройки плитки зон (tl_zone_tile) */
    ZoneLayoutList layouts;
    TlZoneKind kinds[MAX_ZONE_LAYOUTS];  /* чья раскладка — параллельно layouts */
};

/* ---- геометрия ------------------------------------------------------------------- */

/* Зона, как её кладут (с подгонкой под плитку). */
static TlZone zone_of(const TlSpace *space, TlZoneKind kind) { return tl_zone_effective(&space->zones[kind], kind); }

static bool zone_on(const TlSpace *space, TlZoneKind kind)
{
    const TlZone *zone = &space->zones[kind];
    return zone->enabled && zone->width > 0 && (kind == TL_ZONE_BORDER || zone->height > 0);
}

/* Контур, вдоль которого бордюр: комната без пола под дверьми, без лишних точек. */
static TlPolygon border_outline(const TlSpace *space)
{
    TlPolygon outline = tl_polygon_copy(tl_space_room_outline(space));
    tl_polygon_clean(&outline, 0.5);
    return outline;
}

/* Кусок бордюра: часть полосы у стены (стена — сторона wall контура outer). */
typedef struct BorderPiece {
    TlPolygon shape;
    size_t wall;
} BorderPiece;

TL_ARRAY_DEFINE(BorderPieceList, BorderPiece, border_piece_list)

static void free_pieces(BorderPieceList *pieces)
{
    for (size_t i = 0; i < pieces->count; i++)
        tl_polygon_free(&pieces->items[i].shape);
    border_piece_list_free(pieces);
}

static bool keep_first_only(const bool *inside, size_t count, void *context)
{
    (void)context;
    if (!inside[0])
        return false;
    for (size_t i = 1; i < count; i++)
        if (inside[i])
            return false;
    return true;
}

/* Части пола под дверьми — что есть в полу комнаты, но не в её контуре без проёмов. */
static TlRegionList doorway_floors(const TlSpace *space, const TlPolygon *outer)
{
    const TlPolygon polygons[] = {space->outline, *outer};
    return tl_regions_build(polygons, 2, keep_first_only, NULL);
}

/* Проходы через бордюр у дверей: вдоль стены — ширина пола под дверью (±margin), в
   комнату — глубже полосы. Пол под дверью и проход кладутся основной плиткой. */
static TlPolygonList door_passages(const TlSpace *space, const TlPolygon *outer, double depth, double margin)
{
    TlPolygonList passages = {0};
    TlRegionList doorways = doorway_floors(space, outer);
    for (size_t r = 0; r < doorways.count; r++) {
        const TlPolygon *floor = &doorways.items[r].outline;
        for (size_t e = 0; e < outer->count; e++) {
            TlVec2 a = outer->items[e], b = outer->items[(e + 1) % outer->count], direction, normal;
            double length = tl_distance(a, b);
            if (length < 1)
                continue;
            tl_polygon_edge_frame(outer, e, &direction, &normal);
            double from = INFINITY, to = -INFINITY;
            for (size_t k = 0; k < floor->count; k++) { /* точки пола под дверью на этой стене */
                double along;
                if (tl_point_segment_distance(floor->items[k], a, b, &along) < ON_WALL_MM) {
                    from = fmin(from, along * length);
                    to = fmax(to, along * length);
                }
            }
            if (to - from < 1)
                continue;
            TlVec2 start = tl_vec2_add(a, tl_vec2_scale(direction, from - margin));
            TlVec2 end = tl_vec2_add(a, tl_vec2_scale(direction, to + margin));
            TlVec2 out = tl_vec2_scale(normal, 1), in = tl_vec2_scale(normal, -2 * depth - 10);
            const TlVec2 rect[] = {tl_vec2_add(start, out), tl_vec2_add(end, out), tl_vec2_add(end, in),
                                   tl_vec2_add(start, in)};
            tl_polygon_list_push(&passages, tl_polygon_from_points(rect, 4));
        }
    }
    tl_region_list_free_all(&doorways);
    return passages;
}

/* Куски бордюра: полосы между outer и outer, сдвинутым внутрь на depth (по одной на сторону,
   углы — на ус), без проходов у дверей. margin — насколько проход шире пола под дверью. */
static BorderPieceList border_pieces(const TlSpace *space, const TlPolygon *outer, double depth, double margin)
{
    BorderPieceList pieces = {0};
    TlPolygon inner = tl_polygon_offset_uniform(outer, -depth);
    TlPolygonList passages = door_passages(space, outer, depth, margin);
    for (size_t i = 0; inner.count == outer->count && i < outer->count; i++) {
        size_t j = (i + 1) % outer->count;
        if (tl_distance(outer->items[i], outer->items[j]) < 1)
            continue;
        const TlVec2 quad[] = {outer->items[i], outer->items[j], inner.items[j], inner.items[i]};
        TlPolygonList cutters = {0};
        tl_polygon_list_push(&cutters, tl_polygon_from_points(quad, 4));
        for (size_t p = 0; p < passages.count; p++)
            tl_polygon_list_push(&cutters, tl_polygon_copy(&passages.items[p]));
        TlRegionList parts = tl_regions_build(cutters.items, cutters.count, keep_first_only, NULL);
        for (size_t k = 0; k < parts.count; k++) {
            TlPolygon shape = tl_polygon_copy(&parts.items[k].outline);
            tl_polygon_clean(&shape, 0.01);
            if (tl_polygon_area(&shape) > 1)
                border_piece_list_push(&pieces, (BorderPiece){shape, i});
            else
                tl_polygon_free(&shape);
        }
        tl_region_list_free_all(&parts);
        tl_polygon_list_free_all(&cutters);
    }
    tl_polygon_list_free_all(&passages);
    tl_polygon_free(&inner);
    return pieces;
}

/* Оси основной раскладки комнаты — в них задан ковёр. */
static TlAffine main_frame(TlSpace *space)
{
    TlRect bounds = tl_polygon_bounds(tl_space_frame(space));
    return tl_affine_rotation_translation(tl_vec2(bounds.left, bounds.top),
                                          tl_space_effective_tile(space)->rotation_degrees);
}

/* Ковёр (с полями margin со всех сторон) — в мире; *corner — его левый верхний угол в осях комнаты. */
static TlPolygon carpet_rect(TlSpace *space, double margin, TlVec2 *corner)
{
    TlZone zone_value = zone_of(space, TL_ZONE_CARPET);
    const TlZone *zone = &zone_value;
    TlAffine to_world = main_frame(space);
    TlAffine to_local = tl_affine_inverted(&to_world);
    TlPolygon local = tl_affine_apply_polygon(&to_local, tl_space_room_outline(space));
    TlVec2 center = tl_vec2_add(tl_rect_center(tl_polygon_bounds(&local)), tl_vec2(zone->dx, zone->dy));
    tl_polygon_free(&local);
    double half_w = zone->width / 2 + margin, half_h = zone->height / 2 + margin;
    const TlVec2 corners[] = {{center.x - half_w, center.y - half_h}, {center.x + half_w, center.y - half_h},
                              {center.x + half_w, center.y + half_h}, {center.x - half_w, center.y + half_h}};
    TlPolygon rect = {0};
    for (size_t i = 0; i < 4; i++)
        tl_polygon_push(&rect, tl_affine_apply(&to_world, corners[i]));
    if (corner)
        *corner = tl_affine_apply(&to_world, corners[0]);
    return rect;
}

TlPolygonList tl_space_zone_cutouts(TlSpace *space)
{
    TlPolygonList cutouts = {0};
    double gap = tl_space_effective_tile(space)->joint_width;
    if (zone_on(space, TL_ZONE_BORDER)) { /* проход у двери уже на шов: основная плитка от бордюра — на шов */
        TlPolygon outline = border_outline(space);
        BorderPieceList pieces = border_pieces(space, &outline, zone_of(space, TL_ZONE_BORDER).width + gap, -gap);
        for (size_t i = 0; i < pieces.count; i++)
            tl_polygon_list_push(&cutouts, pieces.items[i].shape);
        border_piece_list_free(&pieces);
        tl_polygon_free(&outline);
    }
    if (zone_on(space, TL_ZONE_CARPET))
        tl_polygon_list_push(&cutouts, carpet_rect(space, gap, NULL));
    return cutouts;
}

/* ---- раскладки зон ----------------------------------------------------------------- */

static bool on_wall(const TlSpace *space, TlVec2 a, TlVec2 b)
{
    return tl_polygon_on_boundary(&space->outline, tl_vec2_midpoint(a, b), ON_WALL_MM);
}

/* Поверхность зоны: перегородки, швы и трубы — комнаты. */
static TlSurface zone_surface(TlSpace *space, const TlPolygon *outline, const double *insets, TlVec2 origin,
                              double angle, const TlTileSettings *tile)
{
    return (TlSurface){.outline = outline,
                       .insets = insets,
                       .holes = &space->holes,
                       .joint_strips = &space->joint_strips,
                       .pipes = &space->pipes,
                       .origin = origin,
                       .angle = angle,
                       .tile = tile};
}

static void add_layout(struct TlZoneCache *cache, TlLayout *layout, TlZoneKind kind)
{
    if (cache->layouts.count >= MAX_ZONE_LAYOUTS) {
        tl_layout_free(layout);
        return;
    }
    cache->kinds[cache->layouts.count] = kind;
    zone_layout_list_push(&cache->layouts, layout);
}

/* Кусок бордюра у стены: ось X — вдоль стены, Y — в комнату; у стены — свой отступ, в углах —
   полшва бордюра. */
/* Отступ плитки бордюра от стороны куска: у стены — wall_gap, вдоль основного пола — 0 (шов
   даёт основной пол), в углу на ус и у прохода к двери — полшва. */
static double *piece_insets(const TlSpace *space, const BorderPiece *piece, const TlPolygon *outer, double depth,
                            const TlTileSettings *tile)
{
    TlVec2 a = outer->items[piece->wall], b = outer->items[(piece->wall + 1) % outer->count], direction, normal;
    tl_polygon_edge_frame(outer, piece->wall, &direction, &normal);
    double *insets = tl_malloc(piece->shape.count * sizeof *insets);
    for (size_t e = 0; e < piece->shape.count; e++) {
        TlVec2 p = tl_polygon_edge_start(&piece->shape, e), q = tl_polygon_edge_end(&piece->shape, e);
        TlVec2 middle = tl_vec2_midpoint(p, q);
        double across = -tl_vec2_dot(tl_vec2_sub(middle, a), normal); /* от стены в комнату */
        bool parallel = fabs(tl_vec2_cross(tl_vec2_sub(q, p), direction)) < 0.01 * tl_distance(p, q);
        if (parallel && fabs(across) < 0.5)
            insets[e] = on_wall(space, a, b) ? tile->wall_gap : tile->joint_width / 2;
        else if (parallel && fabs(across - depth) < 0.5)
            insets[e] = 0;
        else
            insets[e] = tile->joint_width / 2;
    }
    return insets;
}

/* Кусок бордюра у стены: ось X — вдоль стены, Y — в комнату, первый ряд — от стены. */
static void lay_border(TlSpace *space, struct TlZoneCache *cache)
{
    const TlTileSettings *tile = &cache->tiles[TL_ZONE_BORDER];
    double depth = zone_of(space, TL_ZONE_BORDER).width;
    TlPolygon outline = border_outline(space);
    BorderPieceList pieces = border_pieces(space, &outline, depth, 0);
    for (size_t i = 0; i < pieces.count; i++) {
        const BorderPiece *piece = &pieces.items[i];
        TlVec2 a = outline.items[piece->wall], b = outline.items[(piece->wall + 1) % outline.count], direction, normal;
        tl_polygon_edge_frame(&outline, piece->wall, &direction, &normal);
        double angle = atan2(b.y - a.y, b.x - a.x) * 180 / TL_PI;
        TlVec2 y_axis = {-sin(angle * TL_PI / 180), cos(angle * TL_PI / 180)};
        TlVec2 origin = a;
        if (tl_vec2_dot(y_axis, normal) > 0) { /* ось Y — в комнату: идём от другого конца стены */
            origin = b;
            angle += 180;
        }
        double *insets = piece_insets(space, piece, &outline, depth, tile);
        TlSurface surface = zone_surface(space, &piece->shape, insets, origin, angle, tile);
        add_layout(cache, tl_layout_compute_surface(&surface, NULL), TL_ZONE_BORDER);
        free(insets);
    }
    free_pieces(&pieces);
    tl_polygon_free(&outline);
}

static void lay_carpet(TlSpace *space, struct TlZoneCache *cache)
{
    const TlTileSettings *tile = &cache->tiles[TL_ZONE_CARPET];
    TlVec2 corner;
    TlPolygon rect = carpet_rect(space, 0, &corner);
    const double insets[4] = {0, 0, 0, 0};
    TlSurface surface =
        zone_surface(space, &rect, insets, corner, tl_space_effective_tile(space)->rotation_degrees, tile);
    add_layout(cache, tl_layout_compute_surface(&surface, NULL), TL_ZONE_CARPET);
    tl_polygon_free(&rect);
}

/* ---- кэш ---------------------------------------------------------------------------- */

static bool same_polygon(const TlPolygon *a, const TlPolygon *b)
{
    return a->count == b->count && (a->count == 0 || memcmp(a->items, b->items, a->count * sizeof(TlVec2)) == 0);
}

static bool same_polygons(const TlPolygonList *a, const TlPolygonList *b)
{
    if (a->count != b->count)
        return false;
    for (size_t i = 0; i < a->count; i++)
        if (!same_polygon(&a->items[i], &b->items[i]))
            return false;
    return true;
}

static bool cache_is_current(const struct TlZoneCache *cache, TlSpace *space)
{
    if (!cache || !tl_tile_settings_equal(&cache->room_tile, tl_space_effective_tile(space)))
        return false;
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++)
        if (!tl_zones_equal(&cache->zones[kind], &space->zones[kind]))
            return false;
    return same_polygon(&cache->outline, &space->outline) &&
           same_polygon(&cache->room_outline, tl_space_room_outline(space)) &&
           same_polygon(&cache->frame, tl_space_frame(space)) && same_polygons(&cache->holes, &space->holes) &&
           same_polygons(&cache->joint_strips, &space->joint_strips) && cache->pipes.count == space->pipes.count &&
           (cache->pipes.count == 0 ||
            memcmp(cache->pipes.items, space->pipes.items, cache->pipes.count * sizeof(TlCircle)) == 0);
}

void tl_zone_cache_free(struct TlZoneCache *cache)
{
    if (!cache)
        return;
    for (size_t i = 0; i < cache->layouts.count; i++)
        tl_layout_free(cache->layouts.items[i]);
    zone_layout_list_free(&cache->layouts);
    tl_polygon_free(&cache->outline);
    tl_polygon_free(&cache->room_outline);
    tl_polygon_free(&cache->frame);
    tl_polygon_list_free_all(&cache->holes);
    tl_polygon_list_free_all(&cache->joint_strips);
    tl_circle_list_free(&cache->pipes);
    free(cache);
}

static struct TlZoneCache *zone_cache(TlSpace *space)
{
    if (cache_is_current(space->zone_cache, space))
        return space->zone_cache;
    tl_zone_cache_free(space->zone_cache);
    struct TlZoneCache *cache = TL_NEW(struct TlZoneCache);
    const TlTileSettings *room_tile = tl_space_effective_tile(space);
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++) {
        cache->zones[kind] = space->zones[kind];
        cache->tiles[kind] = tl_zone_tile(&space->zones[kind], room_tile);
    }
    cache->room_tile = *room_tile;
    cache->outline = tl_polygon_copy(&space->outline);
    cache->room_outline = tl_polygon_copy(tl_space_room_outline(space));
    cache->frame = tl_polygon_copy(tl_space_frame(space));
    cache->holes = tl_polygon_list_deep_copy(&space->holes);
    cache->joint_strips = tl_polygon_list_deep_copy(&space->joint_strips);
    cache->pipes = tl_circle_list_copy(&space->pipes);
    if (room_tile->enabled && zone_on(space, TL_ZONE_BORDER))
        lay_border(space, cache);
    if (room_tile->enabled && zone_on(space, TL_ZONE_CARPET))
        lay_carpet(space, cache);
    space->zone_cache = cache;
    return cache;
}

/* ---- раскладки комнаты по номерам ------------------------------------------------------ */

size_t tl_space_layout_count(TlSpace *space) { return 1 + zone_cache(space)->layouts.count; }

TlLayout *tl_space_layout_at(TlSpace *space, size_t index)
{
    if (index == 0)
        return tl_space_layout(space);
    struct TlZoneCache *cache = zone_cache(space);
    return index - 1 < cache->layouts.count ? cache->layouts.items[index - 1] : NULL;
}

const TlTileSettings *tl_space_layout_tile(TlSpace *space, size_t index)
{
    if (index == 0)
        return tl_space_effective_tile(space);
    struct TlZoneCache *cache = zone_cache(space);
    return &cache->tiles[index - 1 < cache->layouts.count ? cache->kinds[index - 1] : 0];
}

TlZoneKind tl_space_layout_zone(TlSpace *space, size_t index)
{
    if (index == 0)
        return TL_ZONE_COUNT;
    struct TlZoneCache *cache = zone_cache(space);
    return index - 1 < cache->layouts.count ? cache->kinds[index - 1] : TL_ZONE_COUNT;
}

int tl_space_tile_total(TlSpace *space)
{
    int total = 0;
    for (size_t i = 0; i < tl_space_layout_count(space); i++)
        total += tl_layout_total(tl_space_layout_at(space, i));
    return total;
}

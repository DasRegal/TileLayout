/*
 * Вычисление комнат: пол каждого дома минус тела перегородок, но с полом
 * дверных проёмов в них и своей частью пола под проёмами основных стен.
 * Проём делит стык — щель шириной в шов двери (хоть тончайшая), — поэтому
 * комнаты через дверь не сливаются, а каждая получает свою часть проёма.
 * Новые области сопоставляются с прежними комнатами по площади перекрытия,
 * чтобы названия и настройки плитки сохранялись при правках.
 */
#include "tl_project.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tl_regions.h"

#define BODY_EXTENSION 0.05      /* удлинение тел, чтобы стыки надёжно смыкались */
#define CLEAN_TOLERANCE 0.2
#define THRESHOLD_GAP 0.1        /* мм — наименьшая ширина стыка в проёме: разделяет комнаты */
#define THRESHOLD_OVERRUN 1.0    /* мм — стык заходит за косяки, в тело перегородки */

/* Что вырезает пол комнат: тела перегородок, их проёмы (возвращают пол) и стыки
   (во всех проёмах с полом — и в основных стенах). */
typedef struct FloorCutters {
    TlPolygonList bodies, doorways, thresholds;
} FloorCutters;

typedef struct Component {
    TlPolygon outline;
    TlPolygon room_outline; /* без пола проёмов */
    TlPolygonList holes;
    TlOutline *house;
} Component;

typedef struct OverlapPair {
    double area;
    size_t component;
    size_t old_space;
} OverlapPair;

TL_ARRAY_DEFINE(ComponentList, Component, component_list)
TL_ARRAY_DEFINE(OverlapPairList, OverlapPair, overlap_pair_list)

/* Стык — полоска шириной в шов двери (не уже THRESHOLD_GAP) по линии стыка. */
static bool threshold_strip(TlProject *project, const TlOpening *opening, TlPolygon *strip)
{
    TlVec2 from, to;
    if (!tl_project_opening_threshold(project, opening, &from, &to))
        return false;
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(to, from));
    double width = fmax(THRESHOLD_GAP, opening->threshold_gap);
    TlVec2 half = tl_vec2_scale(tl_vec2_perpendicular(direction), width / 2);
    /* Бока полосы — вдоль стыка от косяка до косяка; за косяки она выступает на
       OVERRUN вдоль стены, а не вдоль стыка: стык наискосок иначе вылез бы в комнату. */
    TlVec2 along = direction;
    TlBand band;
    if (tl_project_opening_host(project, opening, &band))
        along = tl_vec2_scale(band.direction, tl_vec2_dot(band.direction, direction) < 0 ? -1 : 1);
    TlVec2 past = tl_vec2_scale(along, THRESHOLD_OVERRUN);
    const TlVec2 corners[] = {
        tl_vec2_add(from, half), tl_vec2_add(to, half), tl_vec2_add(tl_vec2_add(to, half), past),
        tl_vec2_add(tl_vec2_sub(to, half), past), tl_vec2_sub(to, half), tl_vec2_sub(from, half),
        tl_vec2_sub(tl_vec2_sub(from, half), past), tl_vec2_sub(tl_vec2_add(from, half), past),
    };
    *strip = tl_polygon_from_points(corners, 8);
    return true;
}

static FloorCutters floor_cutters(TlProject *project)
{
    FloorCutters cutters = {tl_project_partition_bodies(project, BODY_EXTENSION), {0}, {0}};
    for (size_t i = 0; i < project->openings.count; i++) {
        const TlOpening *opening = project->openings.items[i];
        TlPolygon doorway, strip;
        TlBand band;
        if (!tl_project_opening_doorway(project, opening, &doorway))
            continue;
        if (tl_project_opening_host(project, opening, &band) && band.host.kind == TL_HOST_PARTITION)
            tl_polygon_list_push(&cutters.doorways, doorway);
        else
            tl_polygon_free(&doorway); /* пол под проёмом стены — у каждого дома свой, см. wall_floors_of */
        if (threshold_strip(project, opening, &strip))
            tl_polygon_list_push(&cutters.thresholds, strip);
    }
    return cutters;
}

static void free_floor_cutters(FloorCutters *cutters)
{
    tl_polygon_list_free_all(&cutters->bodies);
    tl_polygon_list_free_all(&cutters->doorways);
    tl_polygon_list_free_all(&cutters->thresholds);
}

/* Многоугольники для разбиения: [0] — пол дома, затем тела перегородок, их
   проёмы, пол дома под проёмами основных стен, стыки. */
typedef struct FloorLayout {
    size_t body_count, doorway_count, wall_floor_count;
} FloorLayout;

static bool any_inside(const bool *inside, size_t from, size_t count)
{
    for (size_t i = from; i < from + count; i++)
        if (inside[i])
            return true;
    return false;
}

static bool keep_floor(const bool *inside, size_t polygon_count, void *context)
{
    const FloorLayout *layout = context;
    size_t doorways = 1 + layout->body_count, wall_floors = doorways + layout->doorway_count;
    size_t thresholds = wall_floors + layout->wall_floor_count;
    bool in_body = any_inside(inside, 1, layout->body_count);
    bool in_doorway = any_inside(inside, doorways, layout->doorway_count);
    bool in_floor = inside[0] || any_inside(inside, wall_floors, layout->wall_floor_count);
    bool on_threshold = any_inside(inside, thresholds, polygon_count - thresholds);
    return in_floor && (!in_body || in_doorway) && !on_threshold;
}

static void push_near(TlPolygonList *polygons, const TlPolygonList *candidates, TlRect bounds, size_t *count)
{
    *count = 0;
    for (size_t i = 0; i < candidates->count; i++)
        if (tl_rect_intersects(bounds, tl_polygon_bounds(&candidates->items[i]))) {
            tl_polygon_list_push(polygons, candidates->items[i]);
            (*count)++;
        }
}

/* Комната без пола проёмов — самая большая часть контура вне проёмов. */
static TlPolygon room_part(const TlPolygon *outline, const TlPolygonList *doorways)
{
    TlRegionList parts = tl_regions_difference(outline, doorways->items, doorways->count);
    size_t largest = 0;
    for (size_t i = 1; i < parts.count; i++)
        if (tl_region_area(&parts.items[i]) > tl_region_area(&parts.items[largest]))
            largest = i;
    TlPolygon room = parts.count ? tl_polygon_copy(&parts.items[largest].outline) : tl_polygon_copy(outline);
    tl_polygon_clean(&room, CLEAN_TOLERANCE);
    tl_region_list_free_all(&parts);
    return room;
}

/* Пол дома под проёмами основных стен — своих и соседнего дома. */
static TlPolygonList wall_floors_of(TlProject *project, const TlOutline *house)
{
    TlPolygonList floors = {0};
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon floor;
        if (tl_project_opening_wall_floor(project, project->openings.items[i], house, &floor))
            tl_polygon_list_push(&floors, floor);
    }
    return floors;
}

static void collect_components_of_house(TlProject *project, TlOutline *house, const FloorCutters *cutters,
                                        ComponentList *components)
{
    if (house->inner.count < 3 || tl_outline_area(house) < 1)
        return;
    TlRect house_bounds = tl_polygon_bounds(&house->inner);
    TlPolygonList wall_floors = wall_floors_of(project, house);
    TlPolygonList polygons = {0}; /* неглубокие копии: принадлежат cutters, дому и wall_floors */
    tl_polygon_list_push(&polygons, house->inner);
    FloorLayout layout = {.wall_floor_count = wall_floors.count};
    size_t threshold_count;
    push_near(&polygons, &cutters->bodies, house_bounds, &layout.body_count);
    push_near(&polygons, &cutters->doorways, house_bounds, &layout.doorway_count);
    for (size_t i = 0; i < wall_floors.count; i++)
        tl_polygon_list_push(&polygons, wall_floors.items[i]);
    push_near(&polygons, &cutters->thresholds, house_bounds, &threshold_count);

    TlRegionList regions = tl_regions_build_merged(polygons.items, polygons.count, keep_floor, &layout);
    tl_polygon_list_free(&polygons);
    TlPolygonList floor_extras = tl_polygon_list_copy(&cutters->doorways); /* пол проёмов — не в сетку */
    for (size_t i = 0; i < wall_floors.count; i++)
        tl_polygon_list_push(&floor_extras, wall_floors.items[i]);
    for (size_t r = 0; r < regions.count; r++) {
        TlRegion *region = &regions.items[r];
        tl_polygon_clean(&region->outline, CLEAN_TOLERANCE);
        if (region->outline.count >= 3 && tl_polygon_area(&region->outline) >= TL_MIN_SPACE_AREA) {
            for (size_t h = 0; h < region->holes.count; h++)
                tl_polygon_clean(&region->holes.items[h], CLEAN_TOLERANCE);
            Component component = {region->outline, room_part(&region->outline, &floor_extras),
                                    region->holes, house};
            component_list_push(components, component);
        } else {
            tl_region_free(region);
        }
    }
    tl_region_list_free(&regions); /* сами многоугольники перешли в components */
    tl_polygon_list_free(&floor_extras);
    tl_polygon_list_free_all(&wall_floors);
}

static int compare_pairs_by_area(const void *left, const void *right)
{
    const OverlapPair *a = left, *b = right;
    return a->area > b->area ? -1 : a->area < b->area;
}

static bool name_taken(const TlSpaceList *spaces, const char *name)
{
    for (size_t i = 0; i < spaces->count; i++)
        if (spaces->items[i] && strcmp(spaces->items[i]->name, name) == 0)
            return true;
    return false;
}

/* «Комната N» с первым свободным N, начиная с количества имеющихся + 1. */
static void make_room_name(const TlSpaceList *named, size_t names_in_use, char *name, size_t size)
{
    for (size_t k = names_in_use + 1;; k++) {
        snprintf(name, size, "Комната %zu", k);
        if (!name_taken(named, name))
            return;
    }
}

typedef struct OrderedSpace {
    long sort_key;
    TlSpace *space;
} OrderedSpace;

static int compare_ordered(const void *left, const void *right)
{
    const OrderedSpace *a = left, *b = right;
    return a->sort_key < b->sort_key ? -1 : a->sort_key > b->sort_key;
}

static void resolve_layout_sources(TlProject *project)
{
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        if (space->source_id == space->id || !tl_project_space_by_id(project, space->source_id))
            space->source_id = 0;
    }
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        TlSpace *source = space;
        /* по цепочке «как в», не больше числа комнат шагов (защита от циклов) */
        for (size_t step = 0; source->source_id && step < project->spaces.count; step++) {
            TlSpace *next = tl_project_space_by_id(project, source->source_id);
            if (next == space)
                break;
            source = next;
        }
        space->layout_source = source;
    }
}

/* Каждой комнате — полосы швов, которые задевают её пол. */
static void assign_joint_strips(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        tl_polygon_list_free_all(&space->joint_strips);
        for (size_t j = 0; j < project->expansion_joints.count; j++) {
            TlPolygon strip = tl_expansion_joint_strip(project->expansion_joints.items[j]);
            if (tl_polygons_overlap_area(&strip, &space->outline) > 0)
                tl_polygon_list_push(&space->joint_strips, strip);
            else
                tl_polygon_free(&strip);
        }
    }
}

static bool inside_first_only(const bool *inside, size_t count, void *context)
{
    (void)context;
    if (!inside[0])
        return false;
    for (size_t i = 1; i < count; i++)
        if (inside[i])
            return false;
    return true;
}

/* Каждой комнате — части пола между линиями (швами и линиями раздела) и её участки на них. */
static void assign_regions(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        tl_region_list_free_all(&space->regions);
        if (space->joint_strips.count > 0) {
            TlPolygonList polygons = {0};
            tl_polygon_list_push(&polygons, tl_polygon_copy(&space->outline));
            for (size_t i = 0; i < space->holes.count; i++)
                tl_polygon_list_push(&polygons, tl_polygon_copy(&space->holes.items[i]));
            for (size_t i = 0; i < space->joint_strips.count; i++)
                tl_polygon_list_push(&polygons, tl_polygon_copy(&space->joint_strips.items[i]));
            space->regions = tl_regions_build_merged(polygons.items, polygons.count, inside_first_only, NULL);
            tl_polygon_list_free_all(&polygons);
        }
        if (space->regions.count == 0) /* без линий — весь пол одна часть */
            tl_region_list_push(&space->regions,
                                (TlRegion){tl_polygon_copy(&space->outline), tl_polygon_list_deep_copy(&space->holes)});
        tl_space_match_areas(space);
    }
}

#define FIXTURE_ON_WALL_MM 30.0 /* спинка предмета ближе — стоит у стены */

/* Каждой комнате — предметы в ней: пол под ними без плитки и стены, которые они закрывают. */
static void assign_fixtures(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        tl_polygon_list_free_all(&space->fixture_floor);
        tl_fixture_list_clear(&space->stairs);
        TlPolygon walls = tl_space_wall_outline(space);
        for (size_t f = 0; f < project->fixtures.count; f++) {
            const TlFixture *fixture = &project->fixtures.items[f];
            if (!tl_space_contains(space, fixture->center))
                continue;
            TlPolygon outline = tl_fixture_outline(fixture);
            if (fixture->kind == TL_FIXTURE_STAIRS && fixture->steps > 0)
                tl_fixture_list_push(&space->stairs, *fixture);
            if (!fixture->under)
                tl_polygon_list_push(&space->fixture_floor, tl_polygon_copy(&outline));
            for (size_t w = 0; !fixture->behind && w < walls.count; w++) { /* закрытый кусок стены */
                TlVec2 a = tl_polygon_edge_start(&walls, w), b = tl_polygon_edge_end(&walls, w);
                double length = tl_distance(a, b), from = INFINITY, to = -INFINITY;
                for (size_t e = 0; length > 1 && e < outline.count; e++) { /* стороны предмета вдоль стены */
                    TlVec2 p = tl_polygon_edge_start(&outline, e), q = tl_polygon_edge_end(&outline, e);
                    double tp, tq;
                    if (tl_point_segment_distance(p, a, b, &tp) < FIXTURE_ON_WALL_MM &&
                        tl_point_segment_distance(q, a, b, &tq) < FIXTURE_ON_WALL_MM) {
                        from = fmin(from, fmin(tp, tq) * length);
                        to = fmax(to, fmax(tp, tq) * length);
                    }
                }
                if (to - from > 1)
                    tl_wall_opening_list_push(&space->wall_openings,
                                              (TlWallOpening){SIZE_MAX, f, w, from, to, 0, fixture->height, 0});
            }
            tl_polygon_free(&outline);
        }
        tl_polygon_free(&walls);
    }
}

/* Шов плитки на части пола region комнаты: участка — своей плитки, иначе — комнаты. */
static double part_joint(TlSpace *space, size_t region)
{
    size_t area = tl_space_area_of_region(space, region);
    double joint = area != SIZE_MAX && space->areas.items[area].tiled ? space->areas.items[area].zone.tile.joint_width
                                                                       : tl_space_effective_tile(space)->joint_width;
    return fmax(joint, 1);
}

/* Каждой комнате — трапы в ней: решётки без плитки, разрезы конверта от точечного трапа к углам его
   части пола; что режет плитку — швы и эти разрезы. */
static void assign_drains(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        tl_polygon_list_free_all(&space->slope_lines);
        tl_polygon_list_free_all(&space->cut_strips);
        space->cut_strips = tl_polygon_list_deep_copy(&space->joint_strips);
        for (size_t d = 0; d < project->drains.count; d++) {
            const TlDrain *drain = &project->drains.items[d];
            if (!tl_space_contains(space, drain->center))
                continue;
            tl_polygon_list_push(&space->fixture_floor, tl_drain_outline(drain));
            size_t region = tl_space_region_at(space, drain->center);
            if (drain->kind != TL_DRAIN_POINT || !drain->envelope || region == SIZE_MAX)
                continue;
            TlPolygon corners = tl_polygon_copy(&space->regions.items[region].outline);
            tl_polygon_clean(&corners, 1.0);
            double half = part_joint(space, region) / 2;
            for (size_t i = 0; i < corners.count; i++) {
                TlVec2 corner = corners.items[i];
                if (tl_distance(corner, drain->center) < drain->size)
                    continue;
                const TlVec2 line[2] = {drain->center, corner};
                tl_polygon_list_push(&space->slope_lines, tl_polygon_from_points(line, 2));
                TlVec2 along = tl_vec2_normalized(tl_vec2_sub(corner, drain->center));
                TlVec2 side = tl_vec2_scale(tl_vec2_perpendicular(along), half);
                TlVec2 end = tl_vec2_add(corner, tl_vec2_scale(along, 5)); /* за угол — наверняка до стены */
                const TlVec2 strip[4] = {tl_vec2_add(drain->center, side), tl_vec2_add(end, side),
                                         tl_vec2_sub(end, side), tl_vec2_sub(drain->center, side)};
                tl_polygon_list_push(&space->cut_strips, tl_polygon_from_points(strip, 4));
            }
            tl_polygon_free(&corners);
        }
    }
}

/* Каждой комнате — трубы, задевающие её пол. */
static void assign_pipes(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        tl_circle_list_clear(&space->pipes);
        TlRect bounds = tl_polygon_bounds(&space->outline);
        for (size_t p = 0; p < project->pipes.count; p++) {
            TlCircle circle = tl_pipe_circle(project->pipes.items[p]);
            if (tl_circle_touches_rect(circle, bounds) && tl_space_contains(space, circle.center))
                tl_circle_list_push(&space->pipes, circle);
        }
    }
}

/* Каждой комнате — проёмы на её стенах: где вырез проёма касается стороны контура комнаты. */
static void assign_wall_openings(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        tl_wall_opening_list_clear(&space->wall_openings);
        TlPolygon walls = tl_space_wall_outline(space);
        for (size_t i = 0; i < project->openings.count; i++) {
            const TlOpening *opening = project->openings.items[i];
            TlPolygon cut;
            if (!tl_project_opening_cut(project, opening, &cut))
                continue;
            for (size_t w = 0; w < walls.count; w++) {
                TlVec2 a = tl_polygon_edge_start(&walls, w), b = tl_polygon_edge_end(&walls, w);
                double length = tl_distance(a, b), from = INFINITY, to = -INFINITY;
                for (size_t k = 0; length > 1 && k < cut.count; k++) {
                    double along;
                    if (tl_point_segment_distance(cut.items[k], a, b, &along) < 2.0) { /* вырез — с запасом 1 мм */
                        from = fmin(from, along * length);
                        to = fmax(to, along * length);
                    }
                }
                if (to - from > 1) {
                    TlWallOpening hole = {i, SIZE_MAX, w, from, to, opening->sill, opening->sill + opening->height,
                                          opening->reveal};
                    tl_wall_opening_list_push(&space->wall_openings, hole);
                }
            }
            tl_polygon_free(&cut);
        }
        tl_polygon_free(&walls);
    }
}

/* Каждой комнате — стыки в проёмах у её пола: на этом краю плитка без отступа от стен. */
static void assign_threshold_strips(TlProject *project)
{
    for (size_t s = 0; s < project->spaces.count; s++)
        tl_polygon_list_free_all(&project->spaces.items[s]->threshold_strips);
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon doorway, strip;
        if (!tl_project_opening_doorway(project, project->openings.items[i], &doorway))
            continue;
        tl_polygon_free(&doorway);
        if (!threshold_strip(project, project->openings.items[i], &strip))
            continue;
        TlRect bounds = tl_polygon_bounds(&strip);
        bounds = (TlRect){bounds.left - 1, bounds.top - 1, bounds.right + 1, bounds.bottom + 1};
        for (size_t s = 0; s < project->spaces.count; s++) {
            TlSpace *space = project->spaces.items[s];
            if (tl_rect_intersects(bounds, tl_polygon_bounds(&space->outline)))
                tl_polygon_list_push(&space->threshold_strips, tl_polygon_copy(&strip));
        }
        tl_polygon_free(&strip);
    }
}

void tl_project_update_spaces(TlProject *project)
{
    FloorCutters cutters = floor_cutters(project);
    ComponentList components = {0};
    for (size_t o = 0; o < project->outlines.count; o++)
        collect_components_of_house(project, project->outlines.items[o], &cutters, &components);
    free_floor_cutters(&cutters);

    TlSpaceList old = project->spaces;
    OverlapPairList pairs = {0};
    for (size_t c = 0; c < components.count; c++)
        for (size_t s = 0; s < old.count; s++) {
            if (old.items[s]->outline.count < 3)
                continue;
            double area = tl_polygons_overlap_area(&components.items[c].outline, &old.items[s]->outline);
            if (area > 1) {
                OverlapPair pair = {area, c, s};
                overlap_pair_list_push(&pairs, pair);
            }
        }
    if (pairs.count > 1)
        qsort(pairs.items, pairs.count, sizeof(OverlapPair), compare_pairs_by_area);

    TlSpace **assigned = tl_calloc(components.count + 1, sizeof(TlSpace *));
    TlSpace **parent = tl_calloc(components.count + 1, sizeof(TlSpace *));
    bool *old_taken = tl_calloc(old.count + 1, sizeof(bool));
    for (size_t p = 0; p < pairs.count; p++) {
        const OverlapPair *pair = &pairs.items[p];
        if (!parent[pair->component])
            parent[pair->component] = old.items[pair->old_space];
        if (!assigned[pair->component] && !old_taken[pair->old_space]) {
            assigned[pair->component] = old.items[pair->old_space];
            old_taken[pair->old_space] = true;
        }
    }
    overlap_pair_list_free(&pairs);

    TlSpaceList named = tl_space_list_copy(&old); /* для проверки занятых имён */
    size_t names_in_use = old.count;
    OrderedSpace *ordered = tl_malloc((components.count + 1) * sizeof(OrderedSpace));
    for (size_t c = 0; c < components.count; c++) {
        TlSpace *space = assigned[c];
        long sort_key = 0;
        if (space) {
            long index;
            TL_ARRAY_INDEX_OF(&old, space, index);
            sort_key = index;
        } else {
            char name[64];
            make_room_name(&named, names_in_use, name, sizeof name);
            TlTileSettings tile = parent[c] ? parent[c]->tile : tl_tile_settings_for_new_room();
            space = tl_space_new(tl_project_take_space_id(project), name, tile);
            if (parent[c]) { /* часть разделённой комнаты — её высота и стены */
                space->height = parent[c]->height;
                space->walls = parent[c]->walls;
            }
            tl_space_list_push(&named, space);
            names_in_use++;
            sort_key = (long)old.count + space->id;
        }
        tl_polygon_free(&space->outline);
        tl_polygon_free(&space->room_outline);
        tl_polygon_list_free_all(&space->holes);
        space->outline = components.items[c].outline;
        space->room_outline = components.items[c].room_outline;
        space->holes = components.items[c].holes;
        space->house = components.items[c].house;
        ordered[c] = (OrderedSpace){sort_key, space};
    }
    for (size_t s = 0; s < old.count; s++)
        if (!old_taken[s])
            tl_space_free(old.items[s]);

    qsort(ordered, components.count, sizeof(OrderedSpace), compare_ordered);
    project->spaces = (TlSpaceList){0};
    for (size_t c = 0; c < components.count; c++)
        tl_space_list_push(&project->spaces, ordered[c].space);

    tl_space_list_free(&old);
    tl_space_list_free(&named);
    component_list_free(&components);
    free(ordered);
    free(assigned);
    free(parent);
    free(old_taken);
    resolve_layout_sources(project);
    assign_joint_strips(project);
    assign_threshold_strips(project);
    assign_pipes(project);
    assign_wall_openings(project);
    assign_regions(project);
    assign_fixtures(project);
    assign_drains(project);
}

/*
 * Вычисление комнат: пол каждого дома минус тела перегородок, но с полом
 * дверных проёмов в них. Проём делит стык — тончайшая щель, — поэтому
 * комнаты через дверь не сливаются, а каждая получает свою часть проёма.
 * Новые области сопоставляются с прежними комнатами по площади перекрытия,
 * чтобы названия и настройки плитки сохранялись при правках.
 */
#include "tl_project.h"

#include <stdio.h>
#include <string.h>

#include "tl_regions.h"

#define BODY_EXTENSION 0.05      /* удлинение тел, чтобы стыки надёжно смыкались */
#define CLEAN_TOLERANCE 0.2
#define THRESHOLD_GAP 0.1        /* мм — ширина стыка в проёме: разделяет комнаты */
#define THRESHOLD_OVERRUN 1.0    /* мм — стык заходит за косяки, в тело перегородки */

/* Что вырезает пол комнат: тела перегородок, их проёмы (возвращают пол) и стыки. */
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

/* Стык — полоска шириной THRESHOLD_GAP по линии стыка. */
static TlPolygon threshold_strip(TlVec2 from, TlVec2 to)
{
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(to, from));
    TlVec2 half = tl_vec2_scale(tl_vec2_perpendicular(direction), THRESHOLD_GAP / 2);
    TlVec2 a = tl_vec2_sub(from, tl_vec2_scale(direction, THRESHOLD_OVERRUN));
    TlVec2 b = tl_vec2_add(to, tl_vec2_scale(direction, THRESHOLD_OVERRUN));
    const TlVec2 corners[] = {tl_vec2_add(a, half), tl_vec2_add(b, half), tl_vec2_sub(b, half), tl_vec2_sub(a, half)};
    return tl_polygon_from_points(corners, 4);
}

static FloorCutters floor_cutters(TlProject *project)
{
    FloorCutters cutters = {tl_project_partition_bodies(project, BODY_EXTENSION), {0}, {0}};
    for (size_t i = 0; i < project->openings.count; i++) {
        const TlOpening *opening = project->openings.items[i];
        TlPolygon doorway;
        TlVec2 from, to;
        if (!tl_project_opening_doorway(project, opening, &doorway))
            continue;
        tl_polygon_list_push(&cutters.doorways, doorway);
        if (tl_project_opening_threshold(project, opening, &from, &to))
            tl_polygon_list_push(&cutters.thresholds, threshold_strip(from, to));
    }
    return cutters;
}

static void free_floor_cutters(FloorCutters *cutters)
{
    tl_polygon_list_free_all(&cutters->bodies);
    tl_polygon_list_free_all(&cutters->doorways);
    tl_polygon_list_free_all(&cutters->thresholds);
}

/* Многоугольники для разбиения: [0] — пол дома, затем тела, проёмы, стыки. */
typedef struct FloorLayout {
    size_t body_count, doorway_count;
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
    size_t doorways = 1 + layout->body_count, thresholds = doorways + layout->doorway_count;
    bool in_body = any_inside(inside, 1, layout->body_count);
    bool in_doorway = any_inside(inside, doorways, layout->doorway_count);
    bool on_threshold = any_inside(inside, thresholds, polygon_count - thresholds);
    return inside[0] && (!in_body || in_doorway) && !on_threshold;
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

static void collect_components_of_house(TlOutline *house, const FloorCutters *cutters, ComponentList *components)
{
    if (house->inner.count < 3 || tl_outline_area(house) < 1)
        return;
    TlRect house_bounds = tl_polygon_bounds(&house->inner);
    TlPolygonList polygons = {0}; /* неглубокие копии: принадлежат cutters и дому */
    tl_polygon_list_push(&polygons, house->inner);
    FloorLayout layout;
    size_t threshold_count;
    push_near(&polygons, &cutters->bodies, house_bounds, &layout.body_count);
    push_near(&polygons, &cutters->doorways, house_bounds, &layout.doorway_count);
    push_near(&polygons, &cutters->thresholds, house_bounds, &threshold_count);

    TlRegionList regions = tl_regions_build_merged(polygons.items, polygons.count, keep_floor, &layout);
    tl_polygon_list_free(&polygons);
    for (size_t r = 0; r < regions.count; r++) {
        TlRegion *region = &regions.items[r];
        tl_polygon_clean(&region->outline, CLEAN_TOLERANCE);
        if (region->outline.count >= 3 && tl_polygon_area(&region->outline) >= TL_MIN_SPACE_AREA) {
            for (size_t h = 0; h < region->holes.count; h++)
                tl_polygon_clean(&region->holes.items[h], CLEAN_TOLERANCE);
            Component component = {region->outline, room_part(&region->outline, &cutters->doorways),
                                    region->holes, house};
            component_list_push(components, component);
        } else {
            tl_region_free(region);
        }
    }
    tl_region_list_free(&regions); /* сами многоугольники перешли в components */
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

void tl_project_update_spaces(TlProject *project)
{
    FloorCutters cutters = floor_cutters(project);
    ComponentList components = {0};
    for (size_t o = 0; o < project->outlines.count; o++)
        collect_components_of_house(project->outlines.items[o], &cutters, &components);
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
}

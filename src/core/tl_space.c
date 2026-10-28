#include "tl_space.h"

#include <stdint.h>
#include <stdio.h>

#include "tl_layout.h"

TlSpace *tl_space_new(int id, const char *name, TlTileSettings tile)
{
    TlSpace *space = TL_NEW(TlSpace);
    space->id = id;
    space->name = tl_strdup(name);
    space->tile = tile;
    space->layout_source = space;
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++)
        space->zones[kind] = tl_zone_factory((TlZoneKind)kind);
    space->height = TL_ROOM_HEIGHT;
    space->walls = tl_wall_finish_factory();
    return space;
}

void tl_space_free(TlSpace *space)
{
    if (!space)
        return;
    free(space->name);
    tl_polygon_free(&space->outline);
    tl_polygon_free(&space->room_outline);
    tl_polygon_list_free_all(&space->holes);
    tl_polygon_list_free_all(&space->joint_strips);
    tl_polygon_list_free_all(&space->threshold_strips);
    tl_circle_list_free(&space->pipes);
    tl_layout_cache_free(space->layout_cache);
    tl_zone_cache_free(space->zone_cache);
    tl_wall_cache_free(space->wall_cache);
    tl_wall_opening_list_free(&space->wall_openings);
    tl_wall_pipe_list_free(&space->wall_pipes);
    tl_wall_ruler_list_free(&space->wall_rulers);
    tl_wall_shift_list_free(&space->wall_shifts);
    tl_panno_list_free(&space->pannos);
    tl_region_list_free_all(&space->regions);
    tl_polygon_list_free_all(&space->fixture_floor);
    tl_fixture_list_free(&space->stairs);
    tl_polygon_list_free_all(&space->slope_lines);
    tl_polygon_list_free_all(&space->cut_strips);
    for (size_t i = 0; i < space->areas.count; i++)
        tl_region_free(&space->areas.items[i].region);
    tl_floor_area_list_free(&space->areas);
    free(space);
}

void tl_space_set_name(TlSpace *space, const char *name)
{
    char *copy = tl_strdup(name);
    free(space->name);
    space->name = copy;
}

double tl_space_area(const TlSpace *space)
{
    double area = tl_polygon_area(&space->outline);
    for (size_t i = 0; i < space->holes.count; i++)
        area -= tl_polygon_area(&space->holes.items[i]);
    return area;
}

double tl_space_perimeter(const TlSpace *space) { return tl_polygon_perimeter(&space->outline); }

double tl_space_room_area(const TlSpace *space)
{
    double area = tl_polygon_area(tl_space_room_outline(space));
    for (size_t i = 0; i < space->holes.count; i++)
        area -= tl_polygon_area(&space->holes.items[i]);
    return area;
}

double tl_space_room_perimeter(const TlSpace *space) { return tl_polygon_perimeter(tl_space_room_outline(space)); }

bool tl_space_contains(const TlSpace *space, TlVec2 point)
{
    if (!tl_polygon_contains(&space->outline, point))
        return false;
    for (size_t i = 0; i < space->holes.count; i++)
        if (tl_polygon_contains(&space->holes.items[i], point))
            return false;
    return true;
}

TlVec2 tl_space_label_point(const TlSpace *space) { return tl_polygon_label_point(&space->outline); }

TlTileSettings *tl_space_effective_tile(TlSpace *space) { return &space->layout_source->tile; }

const TlPolygon *tl_space_room_outline(const TlSpace *space)
{
    return space->room_outline.count >= 3 ? &space->room_outline : &space->outline;
}

const TlPolygon *tl_space_frame(const TlSpace *space) { return tl_space_room_outline(space->layout_source); }

bool tl_space_has_shared_floor(const TlSpace *space) { return space->layout_source != space; }

TlPolygon tl_space_wall_outline(const TlSpace *space)
{
    TlPolygon outline = tl_polygon_copy(tl_space_room_outline(space));
    tl_polygon_clean(&outline, 0.5);
    return outline;
}

/* ---- участки пола ------------------------------------------------------------------ */

size_t tl_space_region_at(const TlSpace *space, TlVec2 point)
{
    for (size_t i = 0; i < space->regions.count; i++)
        if (tl_region_contains(&space->regions.items[i], point))
            return i;
    return SIZE_MAX;
}

size_t tl_space_area_of_region(const TlSpace *space, size_t region)
{
    if (region >= space->regions.count)
        return SIZE_MAX;
    for (size_t i = 0; i < space->areas.count; i++)
        if (tl_region_contains(&space->regions.items[region], space->areas.items[i].anchor))
            return i;
    return SIZE_MAX;
}

static TlRegion region_copy(const TlRegion *region)
{
    return (TlRegion){tl_polygon_copy(&region->outline), tl_polygon_list_deep_copy(&region->holes)};
}

size_t tl_space_add_area(TlSpace *space, size_t region, bool tiled)
{
    if (region >= space->regions.count)
        return SIZE_MAX;
    size_t existing = tl_space_area_of_region(space, region);
    if (existing != SIZE_MAX) {
        space->areas.items[existing].tiled = tiled;
        return existing;
    }
    TlFloorArea area = {.tiled = tiled, .zone = tl_zone_factory(TL_ZONE_CARPET)};
    area.zone.enabled = true;
    area.zone.tile = space->tile;
    area.anchor = tl_polygon_label_point(&space->regions.items[region].outline);
    snprintf(area.name, sizeof area.name, "Участок %zu", space->areas.count + 1);
    area.region = region_copy(&space->regions.items[region]);
    tl_floor_area_list_push(&space->areas, area);
    return space->areas.count - 1;
}

void tl_space_remove_area(TlSpace *space, size_t area)
{
    if (area >= space->areas.count)
        return;
    tl_region_free(&space->areas.items[area].region);
    tl_floor_area_list_remove_at(&space->areas, area);
}

void tl_space_match_areas(TlSpace *space)
{
    for (size_t i = 0; i < space->areas.count; i++) {
        TlFloorArea *area = &space->areas.items[i];
        tl_region_free(&area->region);
        area->region = (TlRegion){0};
        size_t region = tl_space_region_at(space, area->anchor);
        if (region == SIZE_MAX)
            continue; /* часть пропала (линии убрали) — участок ждёт, вдруг вернут */
        area->region = region_copy(&space->regions.items[region]);
        TlVec2 middle = tl_polygon_label_point(&area->region.outline); /* якорь — в середину: линии потом двигают */
        if (tl_region_contains(&area->region, middle))
            area->anchor = middle;
    }
}

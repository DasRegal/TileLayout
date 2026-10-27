#include "tl_space.h"

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

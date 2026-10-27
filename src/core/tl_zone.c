#include "tl_zone.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

TlZone tl_zone_factory(TlZoneKind kind)
{
    TlZone zone = {.enabled = false, .fit = true, .tile = tl_tile_settings_factory()};
    zone.tile.tile_width = zone.tile.tile_height = 300;
    if (kind == TL_ZONE_BORDER) {
        zone.width = 300;
        snprintf(zone.tile.color, sizeof zone.tile.color, "%s", "#6b5a4a");
    } else {
        zone.width = zone.height = 1200;
        snprintf(zone.tile.pattern, sizeof zone.tile.pattern, "%s", "checker");
        snprintf(zone.tile.color, sizeof zone.tile.color, "%s", "#e9e2d4");
        snprintf(zone.tile.color2, sizeof zone.tile.color2, "%s", "#6b5a4a");
    }
    return zone;
}

double tl_zone_fit_size(double size, double tile, double gap)
{
    double pitch = tile + gap;
    if (!(pitch > 0) || !(tile > 0))
        return size;
    double count = fmax(1, round((size + gap) / pitch));
    return count * pitch - gap;
}

TlZone tl_zone_effective(const TlZone *zone, TlZoneKind kind)
{
    TlZone result = *zone;
    const TlTileSettings *t = &zone->tile;
    bool grid = !t->pattern[0] || strcmp(t->pattern, "straight") == 0 || strcmp(t->pattern, "checker") == 0;
    double quarter = fmod(fabs(t->rotation_degrees), 180);
    bool square_turn = fabs(quarter) < 0.01 || fabs(quarter - 90) < 0.01;
    if (!zone->fit || !grid || !square_turn)
        return result;
    bool turned = fabs(quarter - 90) < 0.01;
    double along_x = turned ? t->tile_height : t->tile_width, along_y = turned ? t->tile_width : t->tile_height;
    if (kind == TL_ZONE_BORDER) {
        /* поперёк полосы — Y; у стены плитка отступает на wall_gap — он сверх целых рядов */
        result.width = tl_zone_fit_size(zone->width - t->wall_gap, along_y, t->joint_width) + t->wall_gap;
    } else {
        result.width = tl_zone_fit_size(zone->width, along_x, t->joint_width);
        result.height = tl_zone_fit_size(zone->height, along_y, t->joint_width);
    }
    return result;
}

const char *tl_zone_key(TlZoneKind kind) { return kind == TL_ZONE_BORDER ? "border" : "carpet"; }

const char *tl_zone_name(TlZoneKind kind) { return kind == TL_ZONE_BORDER ? "Бордюр" : "Ковёр"; }

/* Что относится к самой плитке зоны и её укладке; остальное (клей, СВП, затирка, грунт) — от комнаты. */
static const char *const OWN_FIELDS[] = {
    "w", "h", "gap", "off_x", "off_y", "angle", "shift", "pattern", "color", "color2", "texture", "pattern_angle",
    "insert", "min_cut", "reserve", "wall_gap", "price_tile", "price_work",
};

bool tl_zone_field_is_own(const char *key)
{
    for (size_t i = 0; i < sizeof OWN_FIELDS / sizeof OWN_FIELDS[0]; i++)
        if (strcmp(OWN_FIELDS[i], key) == 0)
            return true;
    return false;
}

TlTileSettings tl_zone_tile(const TlZone *zone, const TlTileSettings *room)
{
    TlTileSettings tile = *room;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++)
        if (tl_zone_field_is_own(fields[i].key))
            memcpy((char *)&tile + fields[i].offset, (const char *)&zone->tile + fields[i].offset, fields[i].size);
    tile.enabled = true;
    return tile;
}

bool tl_zones_equal(const TlZone *a, const TlZone *b)
{
    return a->enabled == b->enabled && a->fit == b->fit && a->width == b->width && a->height == b->height &&
           a->dx == b->dx && a->dy == b->dy && tl_tile_settings_equal(&a->tile, &b->tile);
}

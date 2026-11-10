#include "tl_wall.h"

#include <math.h>
#include <stdio.h>

TlWallFinish tl_wall_finish_factory(void)
{
    TlWallFinish finish = {.enabled = false, .height = 0};
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++) {
        TlZone *zone = &finish.bands[band];
        *zone = tl_zone_factory(TL_ZONE_BORDER);
        zone->tile.tile_width = 600;
        zone->tile.tile_height = 300;
        snprintf(zone->tile.color, sizeof zone->tile.color, "%s", "#e8e4dc");
    }
    finish.bands[TL_WALL_MAIN].enabled = true;
    finish.bands[TL_WALL_LOWER].width = 1200;
    snprintf(finish.bands[TL_WALL_LOWER].tile.color, TL_COLOR_LENGTH, "%s", "#7c8f99");
    finish.bands[TL_WALL_FRIEZE].width = 100;
    finish.bands[TL_WALL_FRIEZE].dy = 1200;
    finish.bands[TL_WALL_FRIEZE].tile.tile_width = 300;
    finish.bands[TL_WALL_FRIEZE].tile.tile_height = 100;
    snprintf(finish.bands[TL_WALL_FRIEZE].tile.color, TL_COLOR_LENGTH, "%s", "#3d4a52");
    return finish;
}

const char *tl_wall_band_key(TlWallBand band)
{
    static const char *const KEYS[] = {"lower", "frieze", "main"};
    return KEYS[band];
}

const char *tl_wall_band_name(TlWallBand band)
{
    static const char *const NAMES[] = {"низ", "фриз", "стены"};
    return NAMES[band];
}

bool tl_wall_finishes_equal(const TlWallFinish *a, const TlWallFinish *b)
{
    if (a->enabled != b->enabled || a->height != b->height)
        return false;
    for (int band = 0; band < TL_WALL_BAND_COUNT; band++)
        if (!tl_zones_equal(&a->bands[band], &b->bands[band]))
            return false;
    return true;
}

/* Высота полосы, как её кладут: по целой плитке — целыми рядами, у каждого ряда шов над ним. */
static double band_height(const TlZone *zone)
{
    double along_x, along_y, gap = zone->tile.joint_width;
    if (!tl_zone_fits_grid(zone, &along_x, &along_y) || !(along_y + gap > 0))
        return zone->width + gap;
    double pitch = along_y + gap;
    return fmax(1, round((zone->width + gap) / pitch)) * pitch;
}

/* Фриз — один ряд своей плитки: высота плитки (как лежит) и шов над ней. */
static double frieze_height(const TlZone *zone)
{
    const TlTileSettings *t = &zone->tile;
    bool turned = fabs(fmod(fabs(t->rotation_degrees), 180) - 90) < 0.01;
    return (turned ? tl_tile_laid_width(t) : tl_tile_laid_height(t)) + t->joint_width;
}

static size_t span(double spans[2][2], size_t count, double bottom, double top)
{
    if (top - bottom > 1) {
        spans[count][0] = bottom;
        spans[count][1] = top;
        count++;
    }
    return count;
}

size_t tl_wall_band_spans(const TlWallFinish *finish, TlWallBand band, double room_height, double spans[2][2])
{
    if (!finish->enabled)
        return 0;
    double limit = finish->height > 0 ? fmin(finish->height, room_height) : room_height;
    const TlZone *lower = &finish->bands[TL_WALL_LOWER], *frieze = &finish->bands[TL_WALL_FRIEZE];
    double lower_top = lower->enabled ? fmin(band_height(lower), limit) : 0;
    double frieze_bottom = lower->enabled ? lower_top : fmin(fmax(0, frieze->dy), limit);
    double frieze_top = frieze->enabled ? fmin(frieze_bottom + frieze_height(frieze), limit) : frieze_bottom;
    switch (band) {
    case TL_WALL_LOWER: return lower->enabled ? span(spans, 0, 0, lower_top) : 0;
    case TL_WALL_FRIEZE: return frieze->enabled ? span(spans, 0, frieze_bottom, frieze_top) : 0;
    default: {
        if (!frieze->enabled)
            return span(spans, 0, lower_top, limit);
        size_t count = span(spans, 0, lower_top, frieze_bottom); /* фриз без низа — плитка и под ним */
        return span(spans, count, frieze_top, limit);
    }
    }
}

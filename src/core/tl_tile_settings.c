#include "tl_tile_settings.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static TlTileSettings remembered_defaults;
static bool has_remembered_defaults;

TlTileSettings tl_tile_settings_factory(void)
{
    TlTileSettings settings = {
        .enabled = true,
        .tile_width = 600,
        .tile_height = 300,
        .joint_width = 2,
        .min_cut_width = 50,
        .reserve_percent = 10,
        .color = "#d9c9a8",
        .glue_kg_per_m2_mm = 1.5,
        .glue_layer_mm = 4,
        .glue_bag_kg = 25,
        .clips_per_long_side = 2,
        .clips_per_short_side = 1,
        .clips_per_pack = 100,
        .reuse_offcuts = true,
        .cut_allowance = 3,
        .wall_gap = 0,
    };
    return settings;
}

TlTileSettings tl_tile_settings_for_new_room(void)
{
    TlTileSettings settings = has_remembered_defaults ? remembered_defaults : tl_tile_settings_factory();
    settings.enabled = true;
    settings.offset_x = settings.offset_y = 0;
    return settings;
}

void tl_tile_settings_remember_as_default(const TlTileSettings *settings)
{
    remembered_defaults = *settings;
    has_remembered_defaults = true;
}

void tl_tile_settings_reset_defaults(void) { has_remembered_defaults = false; }

bool tl_tile_settings_equal(const TlTileSettings *a, const TlTileSettings *b)
{
    return a->enabled == b->enabled && a->tile_width == b->tile_width &&
           a->tile_height == b->tile_height && a->joint_width == b->joint_width &&
           a->offset_x == b->offset_x && a->offset_y == b->offset_y &&
           a->rotation_degrees == b->rotation_degrees && a->row_shift == b->row_shift &&
           a->min_cut_width == b->min_cut_width && a->reserve_percent == b->reserve_percent &&
           strcmp(a->color, b->color) == 0 && a->glue_kg_per_m2_mm == b->glue_kg_per_m2_mm &&
           a->glue_layer_mm == b->glue_layer_mm && a->glue_bag_kg == b->glue_bag_kg &&
           a->clips_per_long_side == b->clips_per_long_side &&
           a->clips_per_short_side == b->clips_per_short_side &&
           a->clips_per_pack == b->clips_per_pack && a->reuse_offcuts == b->reuse_offcuts &&
           a->cut_allowance == b->cut_allowance && a->wall_gap == b->wall_gap &&
           strcmp(a->texture, b->texture) == 0;
}

bool tl_tile_settings_is_square(const TlTileSettings *settings)
{
    return fabs(settings->tile_width - settings->tile_height) < 0.5;
}

#define FIELD(key, type, member, remembered) {key, type, offsetof(TlTileSettings, member), remembered}

static const TlTileField FIELDS[] = {
    FIELD("enabled", TL_FIELD_BOOL, enabled, false),
    FIELD("w", TL_FIELD_DOUBLE, tile_width, true),
    FIELD("h", TL_FIELD_DOUBLE, tile_height, true),
    FIELD("gap", TL_FIELD_DOUBLE, joint_width, true),
    FIELD("off_x", TL_FIELD_DOUBLE, offset_x, false),
    FIELD("off_y", TL_FIELD_DOUBLE, offset_y, false),
    FIELD("angle", TL_FIELD_DOUBLE, rotation_degrees, true),
    FIELD("shift", TL_FIELD_DOUBLE, row_shift, true),
    FIELD("min_cut", TL_FIELD_DOUBLE, min_cut_width, true),
    FIELD("reserve", TL_FIELD_DOUBLE, reserve_percent, true),
    FIELD("color", TL_FIELD_COLOR, color, true),
    FIELD("glue_rate", TL_FIELD_DOUBLE, glue_kg_per_m2_mm, true),
    FIELD("glue_mm", TL_FIELD_DOUBLE, glue_layer_mm, true),
    FIELD("glue_bag", TL_FIELD_DOUBLE, glue_bag_kg, true),
    FIELD("svp_long", TL_FIELD_INT, clips_per_long_side, true),
    FIELD("svp_short", TL_FIELD_INT, clips_per_short_side, true),
    FIELD("svp_pack", TL_FIELD_INT, clips_per_pack, true),
    FIELD("reuse", TL_FIELD_BOOL, reuse_offcuts, true),
    FIELD("cut_allow", TL_FIELD_DOUBLE, cut_allowance, true),
    FIELD("wall_gap", TL_FIELD_DOUBLE, wall_gap, true),
    FIELD("texture", TL_FIELD_TEXT, texture, true),
};

const TlTileField *tl_tile_settings_fields(size_t *count)
{
    *count = sizeof FIELDS / sizeof FIELDS[0];
    return FIELDS;
}

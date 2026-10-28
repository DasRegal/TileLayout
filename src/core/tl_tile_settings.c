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
        .color2 = "#5b5b5b",
        .pattern_angle = 45,
        .insert_size = 70,
        .tile_thickness = 9,
        .grout_density = 1.6,
        .grout_pack_kg = 2,
        .primer_l_per_m2 = 0.2,
        .primer_can_l = 10,
        .crosses_per_pack = 200,
        .waterproof_height = 200,
        .waterproof_kg_per_m2 = 0.8,
        .waterproof_layers = 2,
        .waterproof_pack_kg = 15,
        .profile_length = 2700,
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

bool tl_tile_settings_is_square(const TlTileSettings *settings)
{
    return fabs(settings->tile_width - settings->tile_height) < 0.5;
}

#define FIELD(key, type, member, remembered) \
    {key, type, offsetof(TlTileSettings, member), sizeof(((TlTileSettings *)0)->member), remembered}

static const TlTileField FIELDS[] = {
    FIELD("enabled", TL_FIELD_BOOL, enabled, false),
    FIELD("w", TL_FIELD_DOUBLE, tile_width, true),
    FIELD("h", TL_FIELD_DOUBLE, tile_height, true),
    FIELD("real_w", TL_FIELD_DOUBLE, real_width, true),
    FIELD("real_h", TL_FIELD_DOUBLE, real_height, true),
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
    FIELD("pattern", TL_FIELD_TEXT, pattern, true),
    FIELD("color2", TL_FIELD_COLOR, color2, true),
    FIELD("pattern_angle", TL_FIELD_DOUBLE, pattern_angle, true),
    FIELD("insert", TL_FIELD_DOUBLE, insert_size, true),
    FIELD("thickness", TL_FIELD_DOUBLE, tile_thickness, true),
    FIELD("grout_density", TL_FIELD_DOUBLE, grout_density, true),
    FIELD("grout_pack", TL_FIELD_DOUBLE, grout_pack_kg, true),
    FIELD("primer_rate", TL_FIELD_DOUBLE, primer_l_per_m2, true),
    FIELD("primer_can", TL_FIELD_DOUBLE, primer_can_l, true),
    FIELD("price_tile", TL_FIELD_DOUBLE, price_tile_m2, true),
    FIELD("price_glue", TL_FIELD_DOUBLE, price_glue_bag, true),
    FIELD("price_svp", TL_FIELD_DOUBLE, price_clip_pack, true),
    FIELD("price_grout", TL_FIELD_DOUBLE, price_grout_pack, true),
    FIELD("price_primer", TL_FIELD_DOUBLE, price_primer_can, true),
    FIELD("price_work", TL_FIELD_DOUBLE, price_work_m2, true),
    FIELD("price_tile_unit", TL_FIELD_INT, price_tile_unit, true),
    FIELD("tile_pack", TL_FIELD_INT, tile_pack, true),
    FIELD("crosses", TL_FIELD_BOOL, use_crosses, true),
    FIELD("crosses_pack", TL_FIELD_INT, crosses_per_pack, true),
    FIELD("waterproof", TL_FIELD_BOOL, waterproof, false),
    FIELD("waterproof_height", TL_FIELD_DOUBLE, waterproof_height, true),
    FIELD("waterproof_rate", TL_FIELD_DOUBLE, waterproof_kg_per_m2, true),
    FIELD("waterproof_layers", TL_FIELD_INT, waterproof_layers, true),
    FIELD("waterproof_pack", TL_FIELD_DOUBLE, waterproof_pack_kg, true),
    FIELD("threshold_profile", TL_FIELD_BOOL, threshold_profile, false),
    FIELD("corner_height", TL_FIELD_DOUBLE, corner_profile_height, false),
    FIELD("profile_length", TL_FIELD_DOUBLE, profile_length, true),
    FIELD("price_cut", TL_FIELD_DOUBLE, price_cut_m, true),
    FIELD("price_cross", TL_FIELD_DOUBLE, price_cross_pack, true),
    FIELD("price_waterproof", TL_FIELD_DOUBLE, price_waterproof_pack, true),
    FIELD("price_tape", TL_FIELD_DOUBLE, price_tape_m, true),
    FIELD("price_threshold", TL_FIELD_DOUBLE, price_threshold, true),
    FIELD("price_corner", TL_FIELD_DOUBLE, price_corner, true),
};

const TlTileField *tl_tile_settings_fields(size_t *count)
{
    *count = sizeof FIELDS / sizeof FIELDS[0];
    return FIELDS;
}

/* Поле за полем по таблице: новое поле сравнивается само. */
bool tl_tile_settings_equal(const TlTileSettings *a, const TlTileSettings *b)
{
    for (size_t i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; i++) {
        const TlTileField *field = &FIELDS[i];
        const char *x = (const char *)a + field->offset, *y = (const char *)b + field->offset;
        bool same;
        switch (field->type) {
        case TL_FIELD_BOOL: same = *(const bool *)x == *(const bool *)y; break;
        case TL_FIELD_DOUBLE: same = *(const double *)x == *(const double *)y; break;
        case TL_FIELD_INT: same = *(const int *)x == *(const int *)y; break;
        default: same = strncmp(x, y, field->size) == 0; break;
        }
        if (!same)
            return false;
    }
    return true;
}

double tl_tile_laid_width(const TlTileSettings *tile) { return tile->real_width > 0 ? tile->real_width : tile->tile_width; }

double tl_tile_laid_height(const TlTileSettings *tile)
{
    return tile->real_height > 0 ? tile->real_height : tile->tile_height;
}

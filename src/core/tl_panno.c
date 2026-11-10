#include "tl_panno.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

TlPanno tl_panno_default(size_t wall)
{
    TlPanno panno = {.wall = wall, .at = {600, 900}, .columns = 2, .rows = 2, .tile_width = 300,
                     .tile_height = 600, .joint_width = 2};
    if (wall == TL_PANNO_FLOOR)
        panno.at = tl_vec2(0, 0);
    snprintf(panno.color, sizeof panno.color, "%s", "#b9a07a");
    return panno;
}

double tl_panno_width(const TlPanno *panno)
{
    return panno->columns * panno->tile_width + (panno->columns - 1) * panno->joint_width;
}

double tl_panno_height(const TlPanno *panno)
{
    return panno->rows * panno->tile_height + (panno->rows - 1) * panno->joint_width;
}

void tl_panno_name(const TlPanno *panno, char *text, size_t size)
{
    snprintf(text, size, "Панно %.0f×%.0f мм (%d×%d плитки %.0f×%.0f)", tl_panno_width(panno),
             tl_panno_height(panno), panno->columns, panno->rows, panno->tile_width, panno->tile_height);
}

TlTileSettings tl_panno_tile(const TlPanno *panno, const TlTileSettings *around)
{
    TlTileSettings tile = *around;
    tile.enabled = true;
    tile.tile_width = panno->tile_width;
    tile.tile_height = panno->tile_height;
    tile.joint_width = panno->joint_width;
    tile.pattern[0] = '\0';
    tile.rotation_degrees = 0;
    tile.offset_x = tile.offset_y = 0;
    tile.row_shift = 0;
    tile.wall_gap = 0;
    tile.reserve_percent = 0; /* комплект — без запаса */
    snprintf(tile.color, sizeof tile.color, "%s", panno->color);
    snprintf(tile.texture, sizeof tile.texture, "%s", panno->texture);
    return tile;
}

void tl_panno_clamp(TlPanno *panno)
{
    panno->columns = panno->columns < 1 ? 1 : panno->columns > TL_PANNO_MAX_TILES ? TL_PANNO_MAX_TILES : panno->columns;
    panno->rows = panno->rows < 1 ? 1 : panno->rows > TL_PANNO_MAX_TILES ? TL_PANNO_MAX_TILES : panno->rows;
    panno->tile_width = fmax(panno->tile_width, 10);
    panno->tile_height = fmax(panno->tile_height, 10);
    panno->joint_width = fmax(panno->joint_width, 0);
    panno->price = fmax(panno->price, 0);
}

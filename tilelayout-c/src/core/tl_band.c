#include "tl_band.h"

TlBand tl_band_of_wall(TlOutline *outline, size_t wall)
{
    TlBand band = {.host = {TL_HOST_WALL, outline, wall, NULL}};
    tl_outline_wall_frame(outline, wall, &band.direction, &band.normal);
    band.origin = tl_outline_wall_start(outline, wall);
    band.length = tl_outline_inner_length(outline, wall);
    band.near = 0.0;
    band.far = outline->thickness.items[wall];
    return band;
}

TlBand tl_band_of_partition(TlPartition *partition)
{
    TlBand band = {.host = {TL_HOST_PARTITION, NULL, 0, partition}};
    band.direction = tl_partition_direction(partition);
    band.normal = tl_vec2_perpendicular(band.direction);
    band.origin = partition->start;
    band.length = tl_partition_length(partition);
    band.near = -partition->thickness / 2;
    band.far = partition->thickness / 2;
    return band;
}

void tl_band_local(const TlBand *band, TlVec2 point, double *along, double *across)
{
    TlVec2 offset = tl_vec2_sub(point, band->origin);
    if (along)
        *along = tl_vec2_dot(offset, band->direction);
    if (across)
        *across = tl_vec2_dot(offset, band->normal);
}

TlVec2 tl_band_point(const TlBand *band, double along, double across)
{
    return tl_vec2_add(band->origin, tl_vec2_add(tl_vec2_scale(band->direction, along),
                                                 tl_vec2_scale(band->normal, across)));
}

TlPolygon tl_band_rect(const TlBand *band, double along_from, double along_to, double across_from,
                       double across_to)
{
    const TlVec2 corners[] = {tl_band_point(band, along_from, across_from),
                              tl_band_point(band, along_to, across_from),
                              tl_band_point(band, along_to, across_to),
                              tl_band_point(band, along_from, across_to)};
    return tl_polygon_from_points(corners, 4);
}

double tl_band_thickness(const TlBand *band) { return band->far - band->near; }

double tl_band_middle(const TlBand *band) { return (band->near + band->far) / 2; }

bool tl_host_equal(TlHost a, TlHost b)
{
    if (a.kind != b.kind)
        return false;
    if (a.kind == TL_HOST_WALL)
        return a.outline == b.outline && a.wall == b.wall;
    return a.kind == TL_HOST_NONE || a.partition == b.partition;
}

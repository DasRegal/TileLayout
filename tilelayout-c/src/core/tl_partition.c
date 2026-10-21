#include "tl_partition.h"

#include <math.h>

TlPartition *tl_partition_new(TlVec2 start, TlVec2 end, double thickness)
{
    TlPartition *partition = TL_NEW(TlPartition);
    partition->start = start;
    partition->end = end;
    partition->thickness = thickness;
    return partition;
}

TlPartition *tl_partition_clone(const TlPartition *partition)
{
    TlPartition *copy = TL_NEW(TlPartition);
    *copy = *partition;
    return copy;
}

void tl_partition_free(TlPartition *partition) { free(partition); }

double tl_partition_length(const TlPartition *partition)
{
    return tl_distance(partition->start, partition->end);
}

TlVec2 tl_partition_direction(const TlPartition *partition)
{
    return tl_vec2_normalized(tl_vec2_sub(partition->end, partition->start));
}

double tl_partition_angle(const TlPartition *partition)
{
    TlVec2 d = tl_vec2_sub(partition->end, partition->start);
    return fmod(atan2(-d.y, d.x) * 180.0 / TL_PI + 360.0, 360.0);
}

void tl_partition_set_length(TlPartition *partition, double length)
{
    TlVec2 direction = tl_partition_direction(partition);
    if (!tl_vec2_is_zero(direction))
        partition->end = tl_vec2_add(partition->start, tl_vec2_scale(direction, length));
}

void tl_partition_translate(TlPartition *partition, TlVec2 offset)
{
    partition->start = tl_vec2_add(partition->start, offset);
    partition->end = tl_vec2_add(partition->end, offset);
}

TlVec2 *tl_partition_end_point(TlPartition *partition, TlPartitionEnd which)
{
    return which == TL_PARTITION_START ? &partition->start : &partition->end;
}

TlVec2 *tl_partition_other_end_point(TlPartition *partition, TlPartitionEnd which)
{
    return which == TL_PARTITION_START ? &partition->end : &partition->start;
}

TlPolygon tl_partition_body(const TlPartition *partition, double extension)
{
    TlVec2 direction = tl_partition_direction(partition);
    TlVec2 half_width = tl_vec2_scale(tl_vec2_perpendicular(direction), partition->thickness / 2);
    TlVec2 a = tl_vec2_sub(partition->start, tl_vec2_scale(direction, extension));
    TlVec2 b = tl_vec2_add(partition->end, tl_vec2_scale(direction, extension));
    const TlVec2 corners[] = {tl_vec2_add(a, half_width), tl_vec2_add(b, half_width),
                              tl_vec2_sub(b, half_width), tl_vec2_sub(a, half_width)};
    return tl_polygon_from_points(corners, 4);
}

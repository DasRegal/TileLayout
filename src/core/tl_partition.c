#include "tl_partition.h"

#include <math.h>

TlPartition *tl_partition_new(TlVec2 start, TlVec2 end, double thickness)
{
    TlPartition *partition = TL_NEW(TlPartition);
    partition->start = start;
    partition->end = end;
    tl_partition_set_thickness(partition, thickness);
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

void tl_partition_set_angle(TlPartition *partition, double degrees)
{
    double radians = degrees * TL_PI / 180.0, length = tl_partition_length(partition);
    partition->end = tl_vec2_add(partition->start, tl_vec2(cos(radians) * length, -sin(radians) * length));
}

TlPartitionEnd tl_partition_corner_end(TlPartitionCorner corner)
{
    return corner == TL_PARTITION_START_LEFT || corner == TL_PARTITION_START_RIGHT ? TL_PARTITION_START
                                                                                    : TL_PARTITION_END;
}

int tl_partition_corner_side(TlPartitionCorner corner)
{
    return corner == TL_PARTITION_START_LEFT || corner == TL_PARTITION_END_LEFT ? +1 : -1;
}

double tl_partition_thickness_at(const TlPartition *partition, TlPartitionEnd which)
{
    const double *offsets = partition->corner_offsets;
    return which == TL_PARTITION_START ? offsets[TL_PARTITION_START_LEFT] + offsets[TL_PARTITION_START_RIGHT]
                                       : offsets[TL_PARTITION_END_LEFT] + offsets[TL_PARTITION_END_RIGHT];
}

double tl_partition_max_thickness(const TlPartition *partition)
{
    return fmax(tl_partition_thickness_at(partition, TL_PARTITION_START),
                tl_partition_thickness_at(partition, TL_PARTITION_END));
}

bool tl_partition_is_uniform(const TlPartition *partition)
{
    for (int corner = 1; corner < TL_PARTITION_CORNER_COUNT; corner++)
        if (fabs(partition->corner_offsets[corner] - partition->corner_offsets[0]) > 1e-9)
            return false;
    return true;
}

void tl_partition_set_thickness(TlPartition *partition, double thickness)
{
    for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++)
        partition->corner_offsets[corner] = thickness / 2;
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
    TlVec2 normal = tl_vec2_perpendicular(direction);
    TlVec2 a = tl_vec2_sub(partition->start, tl_vec2_scale(direction, extension));
    TlVec2 b = tl_vec2_add(partition->end, tl_vec2_scale(direction, extension));
    TlVec2 corners[TL_PARTITION_CORNER_COUNT];
    for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++) {
        TlVec2 axis_point = tl_partition_corner_end((TlPartitionCorner)corner) == TL_PARTITION_START ? a : b;
        double offset = tl_partition_corner_side((TlPartitionCorner)corner) * partition->corner_offsets[corner];
        corners[corner] = tl_vec2_add(axis_point, tl_vec2_scale(normal, offset));
    }
    return tl_polygon_from_points(corners, TL_PARTITION_CORNER_COUNT);
}

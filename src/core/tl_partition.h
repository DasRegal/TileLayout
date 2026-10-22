/* Перегородка: осевой отрезок от start до end и толщина. */
#ifndef TL_PARTITION_H
#define TL_PARTITION_H

#include "tl_geometry.h"

typedef enum TlPartitionEnd { TL_PARTITION_START = 1, TL_PARTITION_END = 2 } TlPartitionEnd;

typedef struct TlPartition {
    TlVec2 start, end;
    double thickness;
    bool locked;
} TlPartition;

TlPartition *tl_partition_new(TlVec2 start, TlVec2 end, double thickness);
TlPartition *tl_partition_clone(const TlPartition *partition);
void tl_partition_free(TlPartition *partition);

double tl_partition_length(const TlPartition *partition);
TlVec2 tl_partition_direction(const TlPartition *partition);
double tl_partition_angle(const TlPartition *partition); /* 0…360° */
void tl_partition_set_length(TlPartition *partition, double length);
void tl_partition_translate(TlPartition *partition, TlVec2 offset);

TlVec2 *tl_partition_end_point(TlPartition *partition, TlPartitionEnd which);
TlVec2 *tl_partition_other_end_point(TlPartition *partition, TlPartitionEnd which);

/* Тело (прямоугольник); extension — удлинение с обоих концов. */
TlPolygon tl_partition_body(const TlPartition *partition, double extension);

#endif

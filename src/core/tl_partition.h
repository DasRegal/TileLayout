/*
 * Перегородка: осевой отрезок от start до end и тело вокруг него. Каждый из
 * четырёх углов тела задаётся своим отступом от оси — так тело может быть не
 * только прямоугольником, но и трапецией или скошенным с одной стороны
 * (точная форма для раскроя плитки). «Левая» сторона — по tl_vec2_perpendicular
 * от направления start → end.
 */
#ifndef TL_PARTITION_H
#define TL_PARTITION_H

#include "tl_geometry.h"

typedef enum TlPartitionEnd { TL_PARTITION_START = 1, TL_PARTITION_END = 2 } TlPartitionEnd;

/* Углы тела — в порядке обхода его вершин. */
typedef enum TlPartitionCorner {
    TL_PARTITION_START_LEFT,
    TL_PARTITION_END_LEFT,
    TL_PARTITION_END_RIGHT,
    TL_PARTITION_START_RIGHT,
    TL_PARTITION_CORNER_COUNT
} TlPartitionCorner;

typedef struct TlPartition {
    TlVec2 start, end;
    double corner_offsets[TL_PARTITION_CORNER_COUNT]; /* мм от оси до угла тела */
    bool locked;
} TlPartition;

TlPartition *tl_partition_new(TlVec2 start, TlVec2 end, double thickness);
TlPartition *tl_partition_clone(const TlPartition *partition);
void tl_partition_free(TlPartition *partition);

double tl_partition_length(const TlPartition *partition);
TlVec2 tl_partition_direction(const TlPartition *partition);
double tl_partition_angle(const TlPartition *partition); /* 0…360° */
void tl_partition_set_length(TlPartition *partition, double length);
/* Поворот вокруг начала: угол как у tl_partition_angle. */
void tl_partition_set_angle(TlPartition *partition, double degrees);

TlPartitionEnd tl_partition_corner_end(TlPartitionCorner corner);
int tl_partition_corner_side(TlPartitionCorner corner); /* +1 — левая сторона, −1 — правая */

double tl_partition_thickness_at(const TlPartition *partition, TlPartitionEnd which);
double tl_partition_max_thickness(const TlPartition *partition);
/* Прямоугольник с осью посередине. */
bool tl_partition_is_uniform(const TlPartition *partition);
/* Прямоугольник заданной толщины. */
void tl_partition_set_thickness(TlPartition *partition, double thickness);
void tl_partition_translate(TlPartition *partition, TlVec2 offset);

TlVec2 *tl_partition_end_point(TlPartition *partition, TlPartitionEnd which);
TlVec2 *tl_partition_other_end_point(TlPartition *partition, TlPartitionEnd which);

/* Тело с торцами поперёк оси; extension — удлинение с обоих концов. Вершины —
   по порядку TlPartitionCorner. Тело с торцами, срезанными по стенам, —
   tl_project_partition_body. */
TlPolygon tl_partition_body(const TlPartition *partition, double extension);

#endif

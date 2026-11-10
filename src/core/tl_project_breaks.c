/*
 * Разбивка грани стены или перегородки для размеров: где грань кончается или
 * её перекрывает примкнувший объект — другая перегородка или проём. Размер от
 * проёма идёт до ближайшей такой точки, а не до конца стены.
 *
 * У перегородки грань продолжается через узлы, где перегородка идёт прямо
 * (поделена примыканием другой): A, поделённая примыканием Б, меряется как одна.
 */
#include "tl_project.h"

#include <math.h>
#include <stdlib.h>

#define ON_FACE_TOLERANCE 1.5 /* мм: угол тела другого объекта лежит на грани */
#define STRAIGHT_SINE 0.02    /* концы узла на одной прямой */
#define SAME_BREAK 0.5        /* мм: совпадающие точки разбивки */

/* ---- перегородка и её прямое продолжение через узлы ---------------------- */

typedef struct Chain {
    TlPartition *items[64];
    size_t count;
} Chain;

static bool in_chain(const Chain *chain, const TlPartition *partition)
{
    for (size_t i = 0; i < chain->count; i++)
        if (chain->items[i] == partition)
            return true;
    return false;
}

static TlVec2 away_from_node(const TlNodeEnd *end)
{
    TlVec2 direction = tl_partition_direction(end->partition);
    return end->end == TL_PARTITION_START ? direction : tl_vec2_scale(direction, -1);
}

/* Перегородки на одной прямой с partition, сходящиеся с ней (и друг с другом) в узлах. */
static void collect_chain(TlProject *project, TlPartition *partition, Chain *chain)
{
    if (in_chain(chain, partition) || chain->count >= sizeof chain->items / sizeof chain->items[0])
        return;
    chain->items[chain->count++] = partition;
    const TlPartitionEnd ENDS[2] = {TL_PARTITION_START, TL_PARTITION_END};
    for (int e = 0; e < 2; e++) {
        TlNode node;
        if (!tl_project_node_of(project, partition, ENDS[e], &node))
            continue;
        TlNodeEnd self = {partition, ENDS[e]};
        TlVec2 own = away_from_node(&self);
        for (size_t i = 0; i < node.count; i++) {
            TlVec2 other = away_from_node(&node.ends[i]);
            if (node.ends[i].partition != partition && tl_vec2_dot(own, other) < 0 &&
                fabs(tl_vec2_cross(own, other)) < STRAIGHT_SINE)
                collect_chain(project, node.ends[i].partition, chain);
        }
    }
}

/* ---- концы грани ------------------------------------------------------------ */

/* Где вдоль band кончается бок side цепочки перегородок: по углам их тел на этом
   боку (в Г-узле внешний угол дальше конца оси на полтолщины, внутренний — ближе). */
static void chain_face_ends(TlProject *project, const Chain *chain, const TlBand *band, int side, double *low,
                            double *high)
{
    *low = INFINITY;
    *high = -INFINITY;
    for (size_t i = 0; i < chain->count; i++) {
        TlPartition *partition = chain->items[i];
        bool same_direction = tl_vec2_dot(tl_partition_direction(partition), band->direction) > 0;
        bool left = (side > 0) == same_direction; /* бок side — левый у этой перегородки? */
        TlPolygon body = tl_project_partition_body(project, partition, 0);
        const TlPartitionCorner corners[2] = {left ? TL_PARTITION_START_LEFT : TL_PARTITION_START_RIGHT,
                                              left ? TL_PARTITION_END_LEFT : TL_PARTITION_END_RIGHT};
        for (int c = 0; c < 2; c++) {
            double along;
            tl_band_local(band, body.items[corners[c]], &along, NULL);
            *low = fmin(*low, along);
            *high = fmax(*high, along);
        }
        tl_polygon_free(&body);
    }
}

bool tl_project_opening_span(TlProject *project, const TlOpening *opening, double *start, double *end)
{
    TlBand band;
    if (!tl_project_opening_host(project, opening, &band))
        return false;
    if (band.host.kind != TL_HOST_PARTITION) {
        *start = 0;
        *end = band.length;
        return true;
    }
    Chain chain = {0};
    collect_chain(project, band.host.partition, &chain);
    double low[2], high[2];
    chain_face_ends(project, &chain, &band, +1, &low[0], &high[0]);
    chain_face_ends(project, &chain, &band, -1, &low[1], &high[1]);
    *start = fmin(low[0], low[1]);
    *end = fmax(high[0], high[1]);
    return true;
}

/* ---- точки разбивки --------------------------------------------------------- */

static int compare_doubles(const void *left, const void *right)
{
    double a = *(const double *)left, b = *(const double *)right;
    return a < b ? -1 : a > b;
}

static void add_break(TlDoubleArray *breaks, double along) { tl_double_array_push(breaks, along); }

/* Углы тел других перегородок, лежащие на грани (от point по direction). */
static void add_touching_partitions(TlProject *project, const Chain *chain, const TlBand *band, TlVec2 point,
                                    TlVec2 direction, TlDoubleArray *breaks)
{
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *other = project->partitions.items[i];
        if (in_chain(chain, other) || (band->host.kind == TL_HOST_PARTITION && other == band->host.partition))
            continue;
        TlPolygon body = tl_project_partition_body(project, other, 0);
        for (size_t k = 0; k < body.count; k++) {
            TlVec2 offset = tl_vec2_sub(body.items[k], point);
            if (fabs(tl_vec2_cross(direction, offset)) < ON_FACE_TOLERANCE) {
                double along;
                tl_band_local(band, body.items[k], &along, NULL);
                add_break(breaks, along);
            }
        }
        tl_polygon_free(&body);
    }
}

/* Края других проёмов на той же стене (или на перегородках той же прямой). */
static void add_openings(TlProject *project, const Chain *chain, const TlBand *band, const TlOpening *exclude,
                         TlDoubleArray *breaks)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        const TlOpening *opening = project->openings.items[i];
        TlBand host;
        if (opening == exclude || !tl_project_opening_host(project, opening, &host))
            continue;
        bool same = band->host.kind == TL_HOST_WALL ? tl_host_equal(host.host, band->host)
                                                    : host.host.kind == TL_HOST_PARTITION &&
                                                          in_chain(chain, host.host.partition);
        if (!same)
            continue;
        double along;
        tl_band_local(band, opening->center, &along, NULL);
        add_break(breaks, along - opening->width / 2);
        add_break(breaks, along + opening->width / 2);
    }
}

/* Грань: у стены — внутренняя, у перегородки — бок side (+1 левый, −1 правый)
   по телу перегородки, на которой стоит band. */
static void face_line(TlProject *project, const TlBand *band, int side, TlVec2 *point, TlVec2 *direction)
{
    if (band->host.kind == TL_HOST_WALL || !tl_project_face_line(project, band->host, side, point, direction)) {
        *point = band->origin;
        *direction = band->direction;
    }
}

TlDoubleArray tl_project_face_breaks(TlProject *project, const TlBand *band, int side, const TlOpening *exclude)
{
    TlDoubleArray breaks = {0};
    Chain chain = {0};
    if (band->host.kind == TL_HOST_PARTITION) {
        collect_chain(project, band->host.partition, &chain); /* узлы внутри прямой грань не прерывают */
        double low, high;
        chain_face_ends(project, &chain, band, side, &low, &high);
        add_break(&breaks, low);
        add_break(&breaks, high);
    } else {
        add_break(&breaks, 0);
        add_break(&breaks, band->length);
    }
    TlVec2 point, direction;
    face_line(project, band, side, &point, &direction);
    add_touching_partitions(project, &chain, band, point, direction, &breaks);
    add_openings(project, &chain, band, exclude, &breaks);

    if (breaks.count > 1)
        qsort(breaks.items, breaks.count, sizeof(double), compare_doubles);
    size_t kept = 0;
    for (size_t i = 0; i < breaks.count; i++)
        if (kept == 0 || breaks.items[i] - breaks.items[kept - 1] > SAME_BREAK)
            breaks.items[kept++] = breaks.items[i];
    breaks.count = kept;
    return breaks;
}

bool tl_project_opening_gaps(TlProject *project, const TlBand *band, int side, const TlOpening *exclude,
                             double from, double to, double *before, double *after)
{
    TlDoubleArray breaks = tl_project_face_breaks(project, band, side, exclude);
    bool found_before = false, found_after = false;
    for (size_t i = 0; i < breaks.count; i++) {
        double along = breaks.items[i];
        if (along <= from + SAME_BREAK && (!found_before || along > *before)) {
            *before = along;
            found_before = true;
        }
        if (along >= to - SAME_BREAK && (!found_after || along < *after)) {
            *after = along;
            found_after = true;
        }
    }
    tl_double_array_free(&breaks);
    return found_before && found_after;
}

/*
 * Примыкания и узлы, которые из них получаются.
 *
 * Примыкание (Т): конец перегородки branch упирается в бок перегородки host
 * посередине; host остаётся целой. Точку примыкания тянут вдоль host — конец
 * branch скользит по её боку; уводят за грань host — host делится в этой точке
 * и получается узел из трёх (tl_project_move_tee).
 *
 * Обратно: узел, где две перегородки снова на одной прямой и тело не ломается,
 * при фиксации правки опять становится одной перегородкой
 * (tl_project_merge_straight_nodes). Узел можно и убрать явно
 * (tl_project_dissolve_node): самая прямая пара выпрямляется в одну
 * перегородку, остальные упираются в её бок.
 */
#include "tl_project.h"

#include <math.h>

#define TEE_TOLERANCE 1.0       /* мм: конец лежит на боку */
#define TEE_MIN_ANGLE_SINE 0.05 /* почти вдоль перегородки — не примыкание */
#define STRAIGHT_SINE 0.002     /* ≈ 0,1°: две перегородки на одной прямой */
#define SAME_SHAPE 0.5          /* мм: углы тела у узла лежат на боках слитой перегородки */

/* ---- форма перегородки вдоль оси -------------------------------------------- */

/* Отступ бока side (+1 левый, −1 правый) от оси на доле share длины. */
static double side_offset(const TlPartition *partition, int side, double share)
{
    TlPartitionCorner at_start = side > 0 ? TL_PARTITION_START_LEFT : TL_PARTITION_START_RIGHT;
    TlPartitionCorner at_end = side > 0 ? TL_PARTITION_END_LEFT : TL_PARTITION_END_RIGHT;
    return partition->corner_offsets[at_start] +
           (partition->corner_offsets[at_end] - partition->corner_offsets[at_start]) * share;
}

/* Точка на боку side на расстоянии along от начала по оси. */
static TlVec2 side_point(const TlPartition *partition, int side, double along)
{
    TlVec2 axis = tl_partition_direction(partition);
    double share = fmax(0, fmin(1, along / tl_partition_length(partition)));
    TlVec2 on_axis = tl_vec2_add(partition->start, tl_vec2_scale(axis, along));
    return tl_vec2_add(on_axis, tl_vec2_scale(tl_vec2_perpendicular(axis), side * side_offset(partition, side, share)));
}

/* Делит перегородку в точке оси along: сама становится частью до неё, новая —
   после; форма тела сохраняется. */
static void split_partition(TlProject *project, TlPartition *partition, double along)
{
    long index;
    TL_ARRAY_INDEX_OF(&project->partitions, partition, index);
    if (index < 0)
        return;
    double share = along / tl_partition_length(partition);
    double left = side_offset(partition, +1, share), right = side_offset(partition, -1, share);
    TlVec2 split = tl_vec2_add(partition->start, tl_vec2_scale(tl_partition_direction(partition), along));
    TlPartition *rest = tl_partition_clone(partition);
    rest->start = split;
    rest->corner_offsets[TL_PARTITION_START_LEFT] = left;
    rest->corner_offsets[TL_PARTITION_START_RIGHT] = right;
    partition->end = split;
    partition->corner_offsets[TL_PARTITION_END_LEFT] = left;
    partition->corner_offsets[TL_PARTITION_END_RIGHT] = right;
    tl_partition_list_insert_at(&project->partitions, (size_t)index + 1, rest);
}

/* ---- примыкания ---------------------------------------------------------- */

static bool tee_on(TlProject *project, TlPartition *branch, TlPartitionEnd end, TlPartition *host, TlTee *tee)
{
    double length = tl_partition_length(host);
    if (host == branch || length <= 0 || tl_partition_length(branch) <= 0)
        return false;
    TlVec2 axis = tl_partition_direction(host);
    if (fabs(tl_vec2_cross(axis, tl_partition_direction(branch))) < TEE_MIN_ANGLE_SINE)
        return false;
    TlVec2 point = *tl_partition_end_point(branch, end);
    TlVec2 offset = tl_vec2_sub(point, host->start);
    double along = tl_vec2_dot(offset, axis);
    if (along <= TEE_TOLERANCE || along >= length - TEE_TOLERANCE)
        return false;
    int side = tl_vec2_cross(axis, offset) >= 0 ? +1 : -1;
    if (tl_distance(point, side_point(host, side, along)) > TEE_TOLERANCE)
        return false;
    TlNode node;
    if (tl_project_node_of(project, branch, end, &node))
        return false;
    *tee = (TlTee){branch, end, host, side, point};
    return true;
}

TlTeeList tl_project_tees(TlProject *project)
{
    TlTeeList tees = {0};
    const TlPartitionEnd ENDS[2] = {TL_PARTITION_START, TL_PARTITION_END};
    for (size_t b = 0; b < project->partitions.count; b++)
        for (int e = 0; e < 2; e++)
            for (size_t h = 0; h < project->partitions.count; h++) {
                TlTee tee;
                if (tee_on(project, project->partitions.items[b], ENDS[e], project->partitions.items[h], &tee)) {
                    tl_tee_list_push(&tees, tee);
                    break;
                }
            }
    return tees;
}

bool tl_project_tee_of(TlProject *project, const TlPartition *branch, TlPartitionEnd end, TlTee *tee)
{
    for (size_t h = 0; h < project->partitions.count; h++)
        if (tee_on(project, (TlPartition *)branch, end, project->partitions.items[h], tee))
            return true;
    return false;
}

bool tl_project_move_tee(TlProject *project, const TlTee *tee, TlVec2 target)
{
    TlPartition *host = tee->host;
    double length = tl_partition_length(host);
    TlVec2 axis = tl_partition_direction(host);
    TlVec2 offset = tl_vec2_sub(target, host->start);
    double along = fmax(TEE_TOLERANCE * 2, fmin(length - TEE_TOLERANCE * 2, tl_vec2_dot(offset, axis)));
    double across = tl_vec2_cross(axis, offset) * tee->side; /* + — наружу от бока примыкания */
    double share = along / length;
    bool inside = across <= side_offset(host, tee->side, share) &&
                  -across <= side_offset(host, -tee->side, share); /* не дальше граней host */
    if (inside) {
        *tl_partition_end_point(tee->branch, tee->end) = side_point(host, tee->side, along);
        return false;
    }
    split_partition(project, host, along);
    TlNode node;
    TlVec2 split = tl_vec2_add(host->start, tl_vec2_scale(axis, tl_partition_length(host)));
    *tl_partition_end_point(tee->branch, tee->end) = split;
    if (tl_project_node_of(project, tee->branch, tee->end, &node))
        tl_project_move_node(project, &node, target);
    return true;
}

/* ---- узел обратно в прямую перегородку ------------------------------------------ */

static TlVec2 away(const TlNodeEnd *end)
{
    TlVec2 direction = tl_partition_direction(end->partition);
    return end->end == TL_PARTITION_START ? direction : tl_vec2_scale(direction, -1);
}

static TlPartitionEnd far_end(const TlNodeEnd *end)
{
    return end->end == TL_PARTITION_START ? TL_PARTITION_END : TL_PARTITION_START;
}

/* Отступ бока у дальнего конца: side — по направлению «от first к second». */
static double far_offset(const TlNodeEnd *end, int side, bool toward_node)
{
    /* идём к узлу по оси перегородки или против неё */
    bool along_axis = (end->end == TL_PARTITION_END) == toward_node;
    int body_side = along_axis ? side : -side;
    TlPartitionCorner corner = far_end(end) == TL_PARTITION_START
                                   ? (body_side > 0 ? TL_PARTITION_START_LEFT : TL_PARTITION_START_RIGHT)
                                   : (body_side > 0 ? TL_PARTITION_END_LEFT : TL_PARTITION_END_RIGHT);
    return end->partition->corner_offsets[corner];
}

/* Прямая перегородка от дальнего конца first до дальнего конца second (через узел). */
static TlPartition merged(const TlNodeEnd *first, const TlNodeEnd *second)
{
    TlPartition straight = *first->partition;
    straight.start = *tl_partition_end_point(first->partition, far_end(first));
    straight.end = *tl_partition_end_point(second->partition, far_end(second));
    straight.corner_offsets[TL_PARTITION_START_LEFT] = far_offset(first, +1, true);
    straight.corner_offsets[TL_PARTITION_START_RIGHT] = far_offset(first, -1, true);
    straight.corner_offsets[TL_PARTITION_END_LEFT] = far_offset(second, +1, false);
    straight.corner_offsets[TL_PARTITION_END_RIGHT] = far_offset(second, -1, false);
    straight.locked = first->partition->locked || second->partition->locked;
    return straight;
}

static bool point_near_line(TlVec2 point, TlVec2 from, TlVec2 to)
{
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(to, from));
    return fabs(tl_vec2_cross(direction, tl_vec2_sub(point, from))) < SAME_SHAPE;
}

/* Слитая перегородка повторяет тело обеих: углы их тел у узла — на её боках. */
static bool same_shape(const TlNodeEnd *first, const TlNodeEnd *second, const TlPartition *straight)
{
    TlPolygon body = tl_partition_body(straight, 0);
    TlVec2 left_from = body.items[TL_PARTITION_START_LEFT], left_to = body.items[TL_PARTITION_END_LEFT];
    TlVec2 right_from = body.items[TL_PARTITION_START_RIGHT], right_to = body.items[TL_PARTITION_END_RIGHT];
    tl_polygon_free(&body);
    const TlNodeEnd *ends[2] = {first, second};
    for (int i = 0; i < 2; i++) {
        TlPolygon own = tl_partition_body(ends[i]->partition, 0);
        bool at_start = ends[i]->end == TL_PARTITION_START;
        TlVec2 a = own.items[at_start ? TL_PARTITION_START_LEFT : TL_PARTITION_END_LEFT];
        TlVec2 b = own.items[at_start ? TL_PARTITION_START_RIGHT : TL_PARTITION_END_RIGHT];
        tl_polygon_free(&own);
        bool fits = (point_near_line(a, left_from, left_to) && point_near_line(b, right_from, right_to)) ||
                    (point_near_line(a, right_from, right_to) && point_near_line(b, left_from, left_to));
        if (!fits)
            return false;
    }
    return true;
}

/* Остальные концы узла — на бок новой прямой перегородки (вдоль своей оси). */
static void attach_rest(const TlNode *node, size_t first, size_t second, const TlPartition *straight)
{
    TlPolygon body = tl_partition_body(straight, 0);
    TlVec2 axis = tl_partition_direction(straight);
    for (size_t i = 0; i < node->count; i++) {
        if (i == first || i == second)
            continue;
        const TlNodeEnd *end = &node->ends[i];
        TlVec2 far = *tl_partition_end_point(end->partition, far_end(end));
        bool left = tl_vec2_cross(axis, tl_vec2_sub(far, straight->start)) >= 0;
        TlVec2 from = body.items[left ? TL_PARTITION_START_LEFT : TL_PARTITION_START_RIGHT];
        TlVec2 to = body.items[left ? TL_PARTITION_END_LEFT : TL_PARTITION_END_RIGHT];
        TlVec2 hit;
        if (tl_line_intersection(far, tl_vec2_sub(node->point, far), from, tl_vec2_sub(to, from), &hit))
            *tl_partition_end_point(end->partition, end->end) = hit;
    }
    tl_polygon_free(&body);
}

/* Пара first, second узла — снова одна перегородка (first), вторая удаляется. */
static void merge_pair(TlProject *project, const TlNode *node, size_t first, size_t second)
{
    TlPartition straight = merged(&node->ends[first], &node->ends[second]);
    attach_rest(node, first, second, &straight);
    TlPartition *keep = node->ends[first].partition, *gone = node->ends[second].partition;
    *keep = straight;
    tl_project_delete_partition(project, gone);
}

static bool straight_pair(const TlNode *node, size_t *first, size_t *second)
{
    for (size_t i = 0; i < node->count; i++)
        for (size_t j = i + 1; j < node->count; j++) {
            TlVec2 a = away(&node->ends[i]), b = away(&node->ends[j]);
            if (node->ends[i].partition != node->ends[j].partition && tl_vec2_dot(a, b) < 0 &&
                fabs(tl_vec2_cross(a, b)) < STRAIGHT_SINE) {
                *first = i;
                *second = j;
                return true;
            }
        }
    return false;
}

static bool merge_one(TlProject *project)
{
    TlNodeList nodes = tl_project_nodes(project);
    bool merged_any = false;
    for (size_t n = 0; n < nodes.count && !merged_any; n++) {
        size_t first, second;
        if (!straight_pair(&nodes.items[n], &first, &second))
            continue;
        TlPartition straight = merged(&nodes.items[n].ends[first], &nodes.items[n].ends[second]);
        if (!same_shape(&nodes.items[n].ends[first], &nodes.items[n].ends[second], &straight))
            continue;
        merge_pair(project, &nodes.items[n], first, second);
        merged_any = true;
    }
    tl_node_list_free(&nodes);
    return merged_any;
}

void tl_project_merge_straight_nodes(TlProject *project)
{
    for (size_t pass = 0; pass <= project->partitions.count && merge_one(project); pass++) {
    }
}

bool tl_project_dissolve_node(TlProject *project, const TlNode *node)
{
    if (node->count < 2)
        return false;
    size_t first = 0, second = 1;
    double straightest = 2;
    for (size_t i = 0; i < node->count; i++)
        for (size_t j = i + 1; j < node->count; j++) {
            double cosine = tl_vec2_dot(away(&node->ends[i]), away(&node->ends[j]));
            if (node->ends[i].partition != node->ends[j].partition && cosine < straightest) {
                straightest = cosine;
                first = i;
                second = j;
            }
        }
    if (straightest > 1)
        return false;
    merge_pair(project, node, first, second);
    return true;
}

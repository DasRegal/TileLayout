/*
 * Узлы перегородок: совпадающие концы двух и более перегородок.
 *
 * Концы узла упорядочены против часовой стрелки по направлению «от узла вдоль
 * перегородки». Между соседними концами k и k + 1 — угол узла: там левый (по
 * этому направлению) бок перегородки k встречает правый бок перегородки k + 1.
 * Тело каждой перегородки у узла кончается на своих двух углах; середину узла
 * из трёх и больше перегородок закрывает отдельный многоугольник по углам.
 *
 * Примыкание к боку другой перегородки и превращение его в узел —
 * tl_project_tees.c.
 */
#include "tl_project.h"

#include <math.h>

#define NODE_TOLERANCE 1.0     /* мм: концы совпадают */
#define CORNER_LIMIT_FACTOR 3.0 /* угол дальше этого от узла (в толщинах) — бока не сводим */
#define PARALLEL_SINE 1e-6

/* ---- поиск узлов --------------------------------------------------------- */

static TlVec2 outgoing(const TlNodeEnd *end)
{
    TlVec2 direction = tl_partition_direction(end->partition);
    return end->end == TL_PARTITION_START ? direction : tl_vec2_scale(direction, -1);
}

static double outgoing_angle(const TlNodeEnd *end)
{
    TlVec2 d = outgoing(end);
    return atan2(d.y, d.x);
}

static int compare_by_angle(const void *left, const void *right)
{
    double a = outgoing_angle(left), b = outgoing_angle(right);
    return a < b ? -1 : a > b;
}

static bool node_has(const TlNode *node, const TlPartition *partition, TlPartitionEnd end)
{
    for (size_t i = 0; i < node->count; i++)
        if (node->ends[i].partition == partition && node->ends[i].end == end)
            return true;
    return false;
}

TlNodeList tl_project_nodes(TlProject *project)
{
    TlNodeList nodes = {0};
    const TlPartitionEnd ENDS[2] = {TL_PARTITION_START, TL_PARTITION_END};
    for (size_t i = 0; i < project->partitions.count; i++)
        for (int e = 0; e < 2; e++) {
            TlPartition *partition = project->partitions.items[i];
            if (tl_partition_length(partition) <= 0)
                continue;
            TlVec2 point = *tl_partition_end_point(partition, ENDS[e]);
            bool taken = false;
            for (size_t n = 0; n < nodes.count && !taken; n++)
                taken = node_has(&nodes.items[n], partition, ENDS[e]);
            if (taken)
                continue;
            TlNode node = {.point = point};
            for (size_t j = 0; j < project->partitions.count && node.count < TL_NODE_MAX; j++)
                for (int f = 0; f < 2 && node.count < TL_NODE_MAX; f++) {
                    TlPartition *other = project->partitions.items[j];
                    if (tl_partition_length(other) > 0 &&
                        tl_distance(*tl_partition_end_point(other, ENDS[f]), point) < NODE_TOLERANCE)
                        node.ends[node.count++] = (TlNodeEnd){other, ENDS[f]};
                }
            if (node.count < 2)
                continue;
            qsort(node.ends, node.count, sizeof node.ends[0], compare_by_angle);
            tl_node_list_push(&nodes, node);
        }
    return nodes;
}

bool tl_project_node_of(TlProject *project, const TlPartition *partition, TlPartitionEnd end, TlNode *node)
{
    TlNodeList nodes = tl_project_nodes(project);
    bool found = false;
    for (size_t i = 0; i < nodes.count && !found; i++)
        if (node_has(&nodes.items[i], partition, end)) {
            *node = nodes.items[i];
            found = true;
        }
    tl_node_list_free(&nodes);
    return found;
}

/* ---- углы узла ------------------------------------------------------------ */

/* Бок конца: угол тела у узла и у дальнего конца. left — левый по направлению от узла. */
typedef struct Side {
    TlPartitionCorner near, far;
} Side;

static Side side_of(const TlNodeEnd *end, bool left)
{
    bool at_start = end->end == TL_PARTITION_START;
    bool body_left = left == at_start; /* от конца к началу «лево» и «право» меняются местами */
    if (body_left)
        return at_start ? (Side){TL_PARTITION_START_LEFT, TL_PARTITION_END_LEFT}
                        : (Side){TL_PARTITION_END_LEFT, TL_PARTITION_START_LEFT};
    return at_start ? (Side){TL_PARTITION_START_RIGHT, TL_PARTITION_END_RIGHT}
                    : (Side){TL_PARTITION_END_RIGHT, TL_PARTITION_START_RIGHT};
}

static void side_points(const TlNodeEnd *end, bool left, TlVec2 *near, TlVec2 *far)
{
    Side side = side_of(end, left);
    TlPolygon body = tl_partition_body(end->partition, 0);
    *near = body.items[side.near];
    *far = body.items[side.far];
    tl_polygon_free(&body);
}

size_t tl_node_corners(const TlNode *node, TlNodeCorner *corners)
{
    for (size_t k = 0; k < node->count; k++) {
        const TlNodeEnd *first = &node->ends[k], *second = &node->ends[(k + 1) % node->count];
        TlVec2 first_near, first_far, second_near, second_far;
        side_points(first, true, &first_near, &first_far);
        side_points(second, false, &second_near, &second_far);
        TlNodeCorner *corner = &corners[k];
        corner->first = first_near;
        corner->second = second_near;
        corner->joined = false;
        TlVec2 a = tl_vec2_sub(first_near, first_far), b = tl_vec2_sub(second_near, second_far);
        double limit = CORNER_LIMIT_FACTOR * fmax(tl_partition_max_thickness(first->partition),
                                                  tl_partition_max_thickness(second->partition));
        TlVec2 meeting;
        if (fabs(tl_vec2_cross(tl_vec2_normalized(a), tl_vec2_normalized(b))) > PARALLEL_SINE &&
            tl_line_intersection(first_far, a, second_far, b, &meeting) && tl_distance(meeting, node->point) < limit) {
            corner->first = corner->second = meeting;
            corner->joined = true;
        } else if (tl_distance(first_near, second_near) < 1e-6) {
            corner->joined = true; /* бока продолжают друг друга */
        }
    }
    return node->count;
}

TlVec2 tl_node_center(const TlNode *node)
{
    TlNodeCorner corners[TL_NODE_MAX];
    size_t count = tl_node_corners(node, corners);
    TlVec2 sum = tl_vec2(0, 0);
    size_t points = 0;
    for (size_t k = 0; k < count; k++) {
        sum = tl_vec2_add(sum, corners[k].first);
        points++;
        if (!corners[k].joined) {
            sum = tl_vec2_add(sum, corners[k].second);
            points++;
        }
    }
    return tl_vec2_scale(sum, 1.0 / (double)points);
}

/* Углы тела у конца в узле: левый и правый по направлению от узла. */
void tl_node_end_corners(const TlNode *node, size_t index, TlVec2 *left, TlVec2 *right)
{
    TlNodeCorner corners[TL_NODE_MAX];
    size_t count = tl_node_corners(node, corners);
    *left = corners[index].first;
    *right = corners[(index + count - 1) % count].second;
}

bool tl_node_core(const TlNode *node, TlPolygon *core)
{
    TlNodeCorner corners[TL_NODE_MAX];
    size_t count = tl_node_corners(node, corners);
    *core = (TlPolygon){0};
    for (size_t k = 0; k < count; k++) {
        tl_polygon_push(core, corners[k].first);
        if (!corners[k].joined)
            tl_polygon_push(core, corners[k].second);
    }
    tl_polygon_clean(core, 1e-6);
    if (core->count >= 3 && fabs(tl_polygon_signed_area(core)) > 1e-6) {
        if (tl_polygon_signed_area(core) < 0) { /* как у тел — против часовой */
            for (size_t i = 0, j = core->count - 1; i < j; i++, j--) {
                TlVec2 swap = core->items[i];
                core->items[i] = core->items[j];
                core->items[j] = swap;
            }
        }
        return true;
    }
    tl_polygon_free(core);
    return false;
}

bool tl_node_corner_patch(const TlNode *node, size_t corner, TlPolygon *patch)
{
    TlNodeCorner corners[TL_NODE_MAX];
    size_t count = tl_node_corners(node, corners);
    *patch = (TlPolygon){0};
    if (corner >= count || !corners[corner].joined)
        return false;
    const TlNodeEnd *first = &node->ends[corner], *second = &node->ends[(corner + 1) % count];
    TlVec2 meeting = corners[corner].first, unused, first_far, second_far;
    side_points(first, true, &unused, &first_far);
    side_points(second, false, &unused, &second_far);
    TlVec2 a = tl_vec2_sub(first_far, meeting), b = tl_vec2_sub(second_far, meeting);
    if (fabs(tl_vec2_cross(tl_vec2_normalized(a), tl_vec2_normalized(b))) < PARALLEL_SINE)
        return false;
    /* По бокам — на полтолщины, но не дальше половины бока. */
    double reach = 0.5 * fmin(tl_partition_max_thickness(first->partition),
                              tl_partition_max_thickness(second->partition));
    double first_reach = fmin(reach, 0.5 * tl_vec2_length(a)), second_reach = fmin(reach, 0.5 * tl_vec2_length(b));
    if (first_reach <= 0 || second_reach <= 0)
        return false;
    tl_polygon_push(patch, tl_vec2_add(meeting, tl_vec2_scale(tl_vec2_normalized(a), first_reach)));
    tl_polygon_push(patch, meeting);
    tl_polygon_push(patch, tl_vec2_add(meeting, tl_vec2_scale(tl_vec2_normalized(b), second_reach)));
    tl_polygon_push(patch, node->point);
    if (fabs(tl_polygon_signed_area(patch)) < 1e-6) {
        tl_polygon_free(patch);
        return false;
    }
    return true;
}

bool tl_node_locked(const TlNode *node)
{
    for (size_t i = 0; i < node->count; i++)
        if (node->ends[i].partition->locked)
            return true;
    return false;
}

/* ---- правка узла ---------------------------------------------------------- */

void tl_project_move_node(TlProject *project, const TlNode *node, TlVec2 point)
{
    (void)project;
    for (size_t i = 0; i < node->count; i++)
        *tl_partition_end_point(node->ends[i].partition, node->ends[i].end) = point;
}

/* Отступ угла у узла, при котором бок проходит через точку: бок — прямая от
   дальнего угла через point, её пересечение с поперечником у узла. */
static void aim_side(const TlNodeEnd *end, bool left, TlVec2 point)
{
    TlPartition *partition = end->partition;
    Side side = side_of(end, left);
    TlPolygon body = tl_partition_body(partition, 0);
    TlVec2 far = body.items[side.far];
    tl_polygon_free(&body);
    TlVec2 node_point = *tl_partition_end_point(partition, end->end);
    /* наружу от оси — в сторону этого бока: точка за осью даёт не толщину с другим
       знаком, а ноль — бок ложится на ось */
    TlVec2 outward = tl_vec2_scale(tl_vec2_perpendicular(tl_partition_direction(partition)),
                                   tl_partition_corner_side(side.near));
    TlVec2 hit;
    TlVec2 toward = tl_vec2_sub(point, far);
    if (tl_vec2_is_zero(toward) || !tl_line_intersection(far, toward, node_point, outward, &hit))
        hit = point;
    partition->corner_offsets[side.near] = fmax(0, tl_vec2_dot(tl_vec2_sub(hit, node_point), outward));
}

void tl_project_set_node_corner(TlProject *project, const TlNode *node, size_t corner, TlVec2 point)
{
    (void)project;
    aim_side(&node->ends[corner], true, point);
    aim_side(&node->ends[(corner + 1) % node->count], false, point);
}

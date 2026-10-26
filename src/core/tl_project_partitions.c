/*
 * Перегородки: тела (торцы — по углам узлов или по граням, в которые упираются)
 * и грани, к которым примыкают другие перегородки. Узлы — tl_project_nodes.c.
 */
#include "tl_project.h"

#include <math.h>

#define JOINT_TOLERANCE 1.0
#define FACE_CONTACT_TOLERANCE 1.0 /* мм: конец перегородки лежит на грани */
#define MIN_FACE_ANGLE_SINE 0.05   /* грань почти вдоль оси — конец в неё не упирается */

static void orient_counterclockwise(TlPolygon *polygon)
{
    if (tl_polygon_signed_area(polygon) >= 0)
        return;
    for (size_t i = 0, j = polygon->count - 1; i < j; i++, j--) {
        TlVec2 swap = polygon->items[i];
        polygon->items[i] = polygon->items[j];
        polygon->items[j] = swap;
    }
}

/* ---- торцы по граням ------------------------------------------------------- */

typedef struct EndFace {
    bool found;
    double distance;
    TlVec2 point, direction;
} EndFace;

static void consider_face(EndFace *face, TlVec2 end_point, TlVec2 axis, TlVec2 from, TlVec2 to)
{
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(to, from));
    if (fabs(tl_vec2_cross(axis, direction)) < MIN_FACE_ANGLE_SINE)
        return;
    double distance = tl_point_segment_distance(end_point, from, to, NULL);
    if (distance < FACE_CONTACT_TOLERANCE && (!face->found || distance < face->distance))
        *face = (EndFace){true, distance, from, direction};
}

static bool ends_meet(const TlPartition *other, TlVec2 point)
{
    return tl_distance(other->start, point) < JOINT_TOLERANCE || tl_distance(other->end, point) < JOINT_TOLERANCE;
}

/* Грань, в которую упирается конец: внутренняя сторона стены или бок другой
   перегородки (не той, с которой конец образует узел — там угол «на ус»). */
static EndFace end_face(TlProject *project, const TlPartition *partition, TlPartitionEnd end)
{
    EndFace face = {0};
    TlVec2 end_point = end == TL_PARTITION_START ? partition->start : partition->end;
    TlVec2 axis = tl_partition_direction(partition);
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        for (size_t wall = 0; wall < tl_outline_wall_count(outline); wall++)
            consider_face(&face, end_point, axis, tl_outline_wall_start(outline, wall),
                          tl_outline_wall_end(outline, wall));
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *other = project->partitions.items[i];
        if (other == partition || ends_meet(other, end_point) || tl_partition_length(other) <= 0)
            continue;
        TlPolygon body = tl_partition_body(other, 0);
        consider_face(&face, end_point, axis, body.items[TL_PARTITION_START_LEFT], body.items[TL_PARTITION_END_LEFT]);
        consider_face(&face, end_point, axis, body.items[TL_PARTITION_START_RIGHT], body.items[TL_PARTITION_END_RIGHT]);
        tl_polygon_free(&body);
    }
    return face;
}

/* Угол на той же стороне у другого конца — бок идёт от него к этому углу. */
static TlPartitionCorner opposite_corner(TlPartitionCorner corner)
{
    static const TlPartitionCorner OPPOSITE[TL_PARTITION_CORNER_COUNT] = {
        TL_PARTITION_END_LEFT, TL_PARTITION_START_LEFT, TL_PARTITION_START_RIGHT, TL_PARTITION_END_RIGHT};
    return OPPOSITE[corner];
}

/* Углы тела у конца в узле — углы узла. */
static bool node_corners_of(TlProject *project, const TlPartition *partition, TlPartitionEnd end,
                            TlVec2 *left, TlVec2 *right)
{
    TlNode node;
    if (!tl_project_node_of(project, partition, end, &node))
        return false;
    for (size_t i = 0; i < node.count; i++)
        if (node.ends[i].partition == partition && node.ends[i].end == end) {
            TlVec2 outgoing_left, outgoing_right; /* по направлению от узла */
            tl_node_end_corners(&node, i, &outgoing_left, &outgoing_right);
            bool at_start = end == TL_PARTITION_START; /* у конца «лево» тела — справа по ходу от узла */
            *left = at_start ? outgoing_left : outgoing_right;
            *right = at_start ? outgoing_right : outgoing_left;
            return true;
        }
    return false;
}

TlPolygon tl_project_partition_body(TlProject *project, const TlPartition *partition, double extension)
{
    TlPolygon body = tl_partition_body(partition, 0);
    if (tl_partition_length(partition) <= 0)
        return body;
    const EndFace faces[2] = {end_face(project, partition, TL_PARTITION_START),
                              end_face(project, partition, TL_PARTITION_END)};
    TlVec2 corners[TL_PARTITION_CORNER_COUNT];
    for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++) {
        const EndFace *face = &faces[tl_partition_corner_end((TlPartitionCorner)corner) == TL_PARTITION_START ? 0 : 1];
        TlVec2 square_corner = body.items[corner];
        TlVec2 outward = tl_vec2_normalized(
            tl_vec2_sub(square_corner, body.items[opposite_corner((TlPartitionCorner)corner)]));
        TlVec2 on_face;
        if (!face->found || !tl_line_intersection(square_corner, outward, face->point, face->direction, &on_face))
            on_face = square_corner;
        corners[corner] = tl_vec2_add(on_face, tl_vec2_scale(outward, extension));
    }
    const TlPartitionEnd ENDS[2] = {TL_PARTITION_START, TL_PARTITION_END};
    for (int e = 0; e < 2; e++) {
        TlVec2 left, right;
        if (!node_corners_of(project, partition, ENDS[e], &left, &right))
            continue;
        bool at_start = ENDS[e] == TL_PARTITION_START;
        corners[at_start ? TL_PARTITION_START_LEFT : TL_PARTITION_END_LEFT] = left;
        corners[at_start ? TL_PARTITION_START_RIGHT : TL_PARTITION_END_RIGHT] = right;
    }
    tl_polygon_free(&body);
    return tl_polygon_from_points(corners, TL_PARTITION_CORNER_COUNT);
}

TlPolygonList tl_project_partition_bodies(TlProject *project, double extension)
{
    TlPolygonList bodies = {0};
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        if (tl_partition_length(partition) <= 0 || tl_partition_max_thickness(partition) <= 0)
            continue;
        TlPolygon body = tl_project_partition_body(project, partition, extension);
        orient_counterclockwise(&body); /* одно направление обхода — заливка без дыр */
        tl_polygon_list_push(&bodies, body);
    }
    TlNodeList nodes = tl_project_nodes(project);
    for (size_t i = 0; i < nodes.count; i++) {
        TlPolygon core;
        if (tl_node_core(&nodes.items[i], &core))
            tl_polygon_list_push(&bodies, core);
    }
    tl_node_list_free(&nodes);
    return bodies;
}

/* ---- грани ---------------------------------------------------------------- */

TlFaceList tl_project_faces(TlProject *project)
{
    TlFaceList faces = {0};
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        for (size_t wall = 0; wall < tl_outline_wall_count(outline); wall++) {
            TlFace face = {{TL_HOST_WALL, outline, wall, NULL}, 0,
                           tl_outline_wall_start(outline, wall), tl_outline_wall_end(outline, wall)};
            tl_face_list_push(&faces, face);
        }
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        if (tl_partition_length(partition) <= 0)
            continue;
        TlPolygon body = tl_project_partition_body(project, partition, 0);
        TlHost host = {TL_HOST_PARTITION, NULL, 0, partition};
        TlFace left = {host, +1, body.items[TL_PARTITION_START_LEFT], body.items[TL_PARTITION_END_LEFT]};
        TlFace right = {host, -1, body.items[TL_PARTITION_START_RIGHT], body.items[TL_PARTITION_END_RIGHT]};
        tl_face_list_push(&faces, left);
        tl_face_list_push(&faces, right);
        tl_polygon_free(&body);
    }
    return faces;
}

bool tl_project_face_line(TlProject *project, TlHost host, int side, TlVec2 *point,
                          TlVec2 *direction)
{
    TlBand band;
    if (!tl_project_band_of(project, host, &band))
        return false;
    if (host.kind == TL_HOST_WALL) {
        *point = band.origin;
        *direction = band.direction;
        return true;
    }
    if (tl_partition_length(host.partition) <= 0)
        return false;
    /* бок по телу: у трапеции он не параллелен оси */
    TlPolygon body = tl_partition_body(host.partition, 0);
    TlVec2 from = body.items[side > 0 ? TL_PARTITION_START_LEFT : TL_PARTITION_START_RIGHT];
    TlVec2 to = body.items[side > 0 ? TL_PARTITION_END_LEFT : TL_PARTITION_END_RIGHT];
    tl_polygon_free(&body);
    *point = from;
    *direction = tl_vec2_normalized(tl_vec2_sub(to, from));
    return true;
}

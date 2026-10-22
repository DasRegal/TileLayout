/*
 * Перегородки: узлы (совпадающие концы), заполнение углов «на ус», тела и
 * грани, к которым примыкают другие перегородки.
 */
#include "tl_project.h"

#include <math.h>

#define JOINT_TOLERANCE 1.0
#define CAP_LIMIT_FACTOR 3.0
#define FACE_CONTACT_TOLERANCE 1.0 /* мм: конец перегородки лежит на грани */
#define MIN_FACE_ANGLE_SINE 0.05   /* грань почти вдоль оси — конец в неё не упирается */

TlJointList tl_project_joints(TlProject *project)
{
    TlJointList joints = {0};
    TlPartitionList *partitions = &project->partitions;
    const TlPartitionEnd ends[2] = {TL_PARTITION_START, TL_PARTITION_END};
    for (size_t a = 0; a < partitions->count; a++)
        for (size_t b = a + 1; b < partitions->count; b++)
            for (int ea = 0; ea < 2; ea++)
                for (int eb = 0; eb < 2; eb++) {
                    TlPartition *first = partitions->items[a], *second = partitions->items[b];
                    if (tl_distance(*tl_partition_end_point(first, ends[ea]),
                                    *tl_partition_end_point(second, ends[eb])) < JOINT_TOLERANCE) {
                        TlJoint joint = {first, ends[ea], second, ends[eb]};
                        tl_joint_list_push(&joints, joint);
                    }
                }
    return joints;
}

/* ---- угол «на ус» ------------------------------------------------------------ */

/* Бок перегородки у узла: угол тела и направление бока от этого угла вдоль тела. */
typedef struct SideAtJoint {
    TlVec2 corner;
    TlVec2 direction;
} SideAtJoint;

/* Бок на стороне travel_side (+1 — слева по ходу travel) у конца end. */
static SideAtJoint side_at_joint(const TlPartition *partition, TlPartitionEnd end, TlVec2 travel, int travel_side)
{
    bool along_axis = tl_vec2_dot(travel, tl_partition_direction(partition)) > 0;
    bool left = (travel_side > 0) == along_axis; /* «лево» по ходу — лево тела, если идём по оси */
    bool at_start = end == TL_PARTITION_START;
    TlPartitionCorner near = left ? (at_start ? TL_PARTITION_START_LEFT : TL_PARTITION_END_LEFT)
                                  : (at_start ? TL_PARTITION_START_RIGHT : TL_PARTITION_END_RIGHT);
    TlPartitionCorner far = left ? (at_start ? TL_PARTITION_END_LEFT : TL_PARTITION_START_LEFT)
                                 : (at_start ? TL_PARTITION_END_RIGHT : TL_PARTITION_START_RIGHT);
    TlPolygon body = tl_partition_body(partition, 0);
    SideAtJoint side = {body.items[near], tl_vec2_normalized(tl_vec2_sub(body.items[far], body.items[near]))};
    tl_polygon_free(&body);
    return side;
}

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

/*
 * Идём по первой перегородке к узлу и дальше по второй. С внутренней стороны
 * поворота тела перекрываются, с наружной между торцами остаётся клин. Уголок
 * закрывает его: узел, наружные углы обоих тел и точка, где сходятся наружные
 * бока. Если бока сходятся слишком далеко (очень острый угол) или позади
 * торцов — клин срезается прямо между наружными углами.
 */
bool tl_project_joint_cap(const TlJoint *joint, TlPolygon *cap)
{
    TlVec2 joint_point = *tl_partition_end_point(joint->first, joint->first_end);
    TlVec2 first_far_end = *tl_partition_other_end_point(joint->first, joint->first_end);
    TlVec2 second_far_end = *tl_partition_other_end_point(joint->second, joint->second_end);
    TlVec2 into = tl_vec2_normalized(tl_vec2_sub(joint_point, first_far_end));
    TlVec2 out = tl_vec2_normalized(tl_vec2_sub(second_far_end, joint_point));
    double turn = tl_vec2_cross(into, out);
    if (tl_vec2_is_zero(into) || tl_vec2_is_zero(out) || fabs(turn) < 1e-9)
        return false; /* прямое продолжение — клина нет */
    int outer = turn > 0 ? -1 : +1;
    SideAtJoint first = side_at_joint(joint->first, joint->first_end, into, outer);
    SideAtJoint second = side_at_joint(joint->second, joint->second_end, out, outer);

    double limit = CAP_LIMIT_FACTOR * fmax(tl_partition_thickness_at(joint->first, joint->first_end),
                                           tl_partition_thickness_at(joint->second, joint->second_end));
    TlVec2 meeting;
    bool mitred = tl_line_intersection(first.corner, first.direction, second.corner, second.direction, &meeting) &&
                  tl_vec2_dot(tl_vec2_sub(meeting, first.corner), into) >= -1e-9 &&
                  tl_vec2_dot(tl_vec2_sub(meeting, second.corner), out) <= 1e-9 &&
                  tl_distance(meeting, joint_point) < limit;
    *cap = (TlPolygon){0};
    tl_polygon_push(cap, joint_point);
    tl_polygon_push(cap, first.corner);
    if (mitred)
        tl_polygon_push(cap, meeting);
    tl_polygon_push(cap, second.corner);
    orient_counterclockwise(cap);
    if (fabs(tl_polygon_signed_area(cap)) > 1e-9)
        return true;
    tl_polygon_free(cap);
    return false;
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
    TlJointList joints = tl_project_joints(project);
    for (size_t i = 0; i < joints.count; i++) {
        TlPolygon cap;
        if (tl_project_joint_cap(&joints.items[i], &cap))
            tl_polygon_list_push(&bodies, cap);
    }
    tl_joint_list_free(&joints);
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

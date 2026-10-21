/*
 * Перегородки: узлы (совпадающие концы), заполнение углов «на ус», тела и
 * грани, к которым примыкают другие перегородки.
 */
#include "tl_project.h"

#include <math.h>

#define JOINT_TOLERANCE 1.0
#define CAP_LIMIT_FACTOR 3.0

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

/* Боковые линии перегородки у узла: точки на боках и направление от узла. */
static TlVec2 side_lines(TlPartition *partition, TlPartitionEnd end, TlVec2 sides[2])
{
    TlVec2 joint_point = *tl_partition_end_point(partition, end);
    TlVec2 away = tl_vec2_normalized(tl_vec2_sub(*tl_partition_other_end_point(partition, end), joint_point));
    TlVec2 half = tl_vec2_scale(tl_vec2_perpendicular(away), partition->thickness / 2);
    sides[0] = tl_vec2_add(joint_point, half);
    sides[1] = tl_vec2_sub(joint_point, half);
    return away;
}

bool tl_project_joint_cap(const TlJoint *joint, TlPolygon *cap)
{
    TlVec2 joint_point = *tl_partition_end_point(joint->first, joint->first_end);
    TlVec2 first_sides[2], second_sides[2];
    TlVec2 first_away = side_lines(joint->first, joint->first_end, first_sides);
    TlVec2 second_away = side_lines(joint->second, joint->second_end, second_sides);
    if (tl_vec2_is_zero(first_away) || tl_vec2_is_zero(second_away))
        return false;

    TlVec2 points[8];
    size_t count = 0;
    points[count++] = first_sides[0];
    points[count++] = first_sides[1];
    points[count++] = second_sides[0];
    points[count++] = second_sides[1];
    double limit = CAP_LIMIT_FACTOR * fmax(joint->first->thickness, joint->second->thickness);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            TlVec2 corner;
            if (tl_line_intersection(first_sides[i], first_away, second_sides[j], second_away, &corner) &&
                tl_distance(corner, joint_point) < limit)
                points[count++] = corner;
        }
    *cap = tl_convex_hull(points, count);
    if (cap->count >= 3)
        return true;
    tl_polygon_free(cap);
    return false;
}

TlPolygonList tl_project_partition_bodies(TlProject *project, double extension)
{
    TlPolygonList bodies = {0};
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        if (tl_partition_length(partition) > 0 && partition->thickness > 0)
            tl_polygon_list_push(&bodies, tl_partition_body(partition, extension));
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
        TlPolygon body = tl_partition_body(partition, 0);
        TlHost host = {TL_HOST_PARTITION, NULL, 0, partition};
        TlFace left = {host, +1, body.items[0], body.items[1]};
        TlFace right = {host, -1, body.items[3], body.items[2]};
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
    *point = tl_band_point(&band, 0, side > 0 ? band.far : band.near);
    *direction = band.direction;
    return true;
}

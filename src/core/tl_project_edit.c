/*
 * Правка с привязками (транзакция).
 *
 * begin: запоминаем, какие концы перегородок лежат на каких гранях, какие
 * концы соединены в узлы, на какой стене и где вдоль неё стоит каждый проём,
 * и (при переносе дома) какие перегородки лежат внутри него.
 * apply: переносим перегородки дома, узлы — за ведущей перегородкой, концы на
 * гранях — на новое положение граней (сохраняя направление перегородки),
 * проёмы — на свои стены, концы линеек и швов — на то, к чему примагничены
 * (tl_measure_anchor.h); затем пересчитываем комнаты.
 * end: то же и в конце — tl_project_merge_straight_nodes (узел на прямой —
 * снова одна перегородка).
 */
#include "tl_project.h"

#include "tl_measure_anchor.h"

#include <math.h>

#define ATTACH_TOLERANCE 1.0
#define FOLLOW_PASSES 3 /* перегородки могут опираться друг на друга */
#define STRAIGHT_SINE 0.02 /* два конца узла — одна прямая перегородка, поделённая в узле */

typedef struct Attachment {
    TlPartition *partition;
    TlPartitionEnd end;
    TlHost host;
    int side;
} Attachment;

typedef struct OpeningAnchor {
    TlOpening *opening;
    TlHost host;
    double along;
} OpeningAnchor;

typedef struct CarriedPartition {
    TlPartition *partition;
    TlVec2 start, end;
} CarriedPartition;

typedef struct CarriedJoint { /* деформационный шов внутри переносимого дома */
    TlExpansionJoint *joint;
    TlVec2 start, end;
} CarriedJoint;

typedef struct CarriedPipe {
    TlPipe *pipe;
    TlVec2 center;
} CarriedPipe;

typedef struct PartitionDirection {
    TlPartition *partition;
    TlVec2 direction;
} PartitionDirection;

TL_ARRAY_DEFINE(AttachmentList, Attachment, attachment_list)
TL_ARRAY_DEFINE(OpeningAnchorList, OpeningAnchor, opening_anchor_list)
TL_ARRAY_DEFINE(CarriedPartitionList, CarriedPartition, carried_partition_list)
TL_ARRAY_DEFINE(CarriedJointList, CarriedJoint, carried_joint_list)
TL_ARRAY_DEFINE(CarriedPipeList, CarriedPipe, carried_pipe_list)
TL_ARRAY_DEFINE(PartitionDirectionList, PartitionDirection, partition_direction_list)

struct TlEditToken {
    AttachmentList attachments;
    TlNodeList nodes;
    OpeningAnchorList anchors;
    CarriedPartitionList carried;
    CarriedJointList carried_joints;
    CarriedPipeList carried_pipes;
    TlMeasureAnchors *measures;          /* концы линеек и швов на стенах — едут с ними */
    PartitionDirectionList directions;
};

static bool face_belongs_to(const TlFace *face, const TlPartition *partition)
{
    return face->host.kind == TL_HOST_PARTITION && face->host.partition == partition;
}

static void remember_attachments(TlProject *project, TlEditToken *token)
{
    TlFaceList faces = tl_project_faces(project);
    const TlPartitionEnd ends[2] = {TL_PARTITION_START, TL_PARTITION_END};
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        for (int e = 0; e < 2; e++) {
            TlVec2 point = *tl_partition_end_point(partition, ends[e]);
            for (size_t f = 0; f < faces.count; f++) {
                const TlFace *face = &faces.items[f];
                if (face_belongs_to(face, partition))
                    continue;
                if (tl_point_segment_distance(point, face->start, face->end, NULL) < ATTACH_TOLERANCE) {
                    Attachment attachment = {partition, ends[e], face->host, face->side};
                    attachment_list_push(&token->attachments, attachment);
                    break;
                }
            }
        }
    }
    tl_face_list_free(&faces);
}

static void remember_openings(TlProject *project, TlEditToken *token)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        TlOpening *opening = project->openings.items[i];
        TlBand band;
        if (!tl_project_opening_host(project, opening, &band))
            continue;
        OpeningAnchor anchor = {opening, band.host, 0};
        tl_band_local(&band, opening->center, &anchor.along, NULL);
        opening_anchor_list_push(&token->anchors, anchor);
    }
}

static void remember_carried(TlProject *project, TlOutline *moving_outline, TlEditToken *token)
{
    if (!moving_outline)
        return;
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        if (tl_outline_contains_or_touches(moving_outline, partition->start) &&
            tl_outline_contains_or_touches(moving_outline, partition->end)) {
            CarriedPartition carried = {partition, partition->start, partition->end};
            carried_partition_list_push(&token->carried, carried);
        }
    }
    for (size_t i = 0; i < project->expansion_joints.count; i++) { /* швы режут плитку дома — едут с ним */
        TlExpansionJoint *joint = project->expansion_joints.items[i];
        if (tl_outline_contains_or_touches(moving_outline, joint->start) &&
            tl_outline_contains_or_touches(moving_outline, joint->end)) {
            CarriedJoint carried = {joint, joint->start, joint->end};
            carried_joint_list_push(&token->carried_joints, carried);
        }
    }
    for (size_t i = 0; i < project->pipes.count; i++) { /* трубы — тоже */
        TlPipe *pipe = project->pipes.items[i];
        if (tl_outline_contains_or_touches(moving_outline, pipe->center))
            carried_pipe_list_push(&token->carried_pipes, (CarriedPipe){pipe, pipe->center});
    }
}

TlEditToken *tl_project_begin_edit(TlProject *project, TlOutline *moving_outline)
{
    TlEditToken *token = TL_NEW(TlEditToken);
    remember_attachments(project, token);
    token->nodes = tl_project_nodes(project);
    remember_openings(project, token);
    remember_carried(project, moving_outline, token);
    token->measures = tl_measure_anchors_remember(project);
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        PartitionDirection entry = {partition, tl_partition_direction(partition)};
        partition_direction_list_push(&token->directions, entry);
    }
    return token;
}

void tl_edit_token_free(TlEditToken *token)
{
    if (!token)
        return;
    attachment_list_free(&token->attachments);
    tl_node_list_free(&token->nodes);
    opening_anchor_list_free(&token->anchors);
    carried_partition_list_free(&token->carried);
    carried_joint_list_free(&token->carried_joints);
    carried_pipe_list_free(&token->carried_pipes);
    tl_measure_anchors_free(token->measures);
    partition_direction_list_free(&token->directions);
    free(token);
}

/* ---- завершение правки ------------------------------------------------------ */

static bool partition_exists(TlProject *project, const TlPartition *partition)
{
    long index;
    TL_ARRAY_INDEX_OF(&project->partitions, partition, index);
    return index >= 0;
}

static bool was_carried(const TlEditToken *token, const TlPartition *partition)
{
    for (size_t i = 0; i < token->carried.count; i++)
        if (token->carried.items[i].partition == partition)
            return true;
    return false;
}

static TlVec2 original_direction(const TlEditToken *token, const TlPartition *partition)
{
    for (size_t i = 0; i < token->directions.count; i++)
        if (token->directions.items[i].partition == partition)
            return token->directions.items[i].direction;
    return tl_vec2(0, 0);
}

static void carry_partitions(TlEditToken *token, TlVec2 offset)
{
    for (size_t i = 0; i < token->carried.count; i++) {
        CarriedPartition *carried = &token->carried.items[i];
        carried->partition->start = tl_vec2_add(carried->start, offset);
        carried->partition->end = tl_vec2_add(carried->end, offset);
    }
    for (size_t i = 0; i < token->carried_joints.count; i++) {
        CarriedJoint *carried = &token->carried_joints.items[i];
        carried->joint->start = tl_vec2_add(carried->start, offset);
        carried->joint->end = tl_vec2_add(carried->end, offset);
    }
    for (size_t i = 0; i < token->carried_pipes.count; i++)
        token->carried_pipes.items[i].pipe->center = tl_vec2_add(token->carried_pipes.items[i].center, offset);
}

static TlVec2 away(const TlNodeEnd *end, const TlEditToken *token)
{
    TlVec2 direction = original_direction(token, end->partition);
    return end->end == TL_PARTITION_START ? direction : tl_vec2_scale(direction, -1);
}

/* Два других конца узла на одной прямой — перегородка, поделённая в узле
   примыканием: её дальние концы задают прямую, по которой узел скользит. */
static bool straight_pair(const TlNode *node, const TlEditToken *token, const TlPartition *master,
                          TlVec2 *point, TlVec2 *direction)
{
    for (size_t i = 0; i < node->count; i++)
        for (size_t j = i + 1; j < node->count; j++) {
            const TlNodeEnd *a = &node->ends[i], *b = &node->ends[j];
            if (a->partition == master || b->partition == master)
                continue;
            TlVec2 da = away(a, token), db = away(b, token);
            if (tl_vec2_dot(da, db) < 0 && fabs(tl_vec2_cross(da, db)) < STRAIGHT_SINE) {
                *point = *tl_partition_other_end_point(a->partition, a->end);
                *direction = tl_vec2_sub(*tl_partition_other_end_point(b->partition, b->end), *point);
                return !tl_vec2_is_zero(*direction);
            }
        }
    return false;
}

/* Узел следует за концом ведущей перегородки. Если узел на прямой из двух
   других — он скользит по ней туда, где её пересекает ось ведущей. */
static void follow_master(const TlEditToken *token, const TlEditOptions *options)
{
    if (!options->master)
        return;
    for (size_t n = 0; n < token->nodes.count; n++) {
        const TlNode *node = &token->nodes.items[n];
        const TlNodeEnd *lead = NULL;
        for (size_t i = 0; i < node->count && !lead; i++)
            if (node->ends[i].partition == options->master)
                lead = &node->ends[i];
        if (!lead)
            continue;
        if (options->has_translate) {
            bool carried = false;
            for (size_t i = 0; i < node->count; i++)
                carried = carried || was_carried(token, node->ends[i].partition);
            if (carried)
                continue;
        }
        TlVec2 target = *tl_partition_end_point(lead->partition, lead->end);
        TlVec2 line_point, line_direction, hit;
        TlPartition *master = options->master;
        if (straight_pair(node, token, master, &line_point, &line_direction) &&
            tl_line_intersection(*tl_partition_other_end_point(master, lead->end),
                                 tl_vec2_sub(target, *tl_partition_other_end_point(master, lead->end)), line_point,
                                 line_direction, &hit))
            target = hit;
        for (size_t i = 0; i < node->count; i++) {
            const TlNodeEnd *end = &node->ends[i];
            if (end->partition == master && master != options->frozen)
                *tl_partition_end_point(end->partition, end->end) = target;
            else if (end->partition != master && !end->partition->locked)
                *tl_partition_end_point(end->partition, end->end) = target;
        }
    }
}

/* Конец перегородки возвращается на свою грань по прежнему направлению. */
static void follow_faces(TlProject *project, const TlEditToken *token, const TlEditOptions *options,
                         bool translating)
{
    for (size_t i = 0; i < token->attachments.count; i++) {
        const Attachment *attachment = &token->attachments.items[i];
        TlPartition *partition = attachment->partition;
        TlNode node;
        if (partition == options->frozen || partition->locked || !partition_exists(project, partition) ||
            (translating && was_carried(token, partition)))
            continue;
        if (tl_project_node_of(project, partition, attachment->end, &node))
            continue; /* конец в узле (например, перегородку поделили) — его держит узел */
        TlVec2 face_point, face_direction;
        if (!tl_project_face_line(project, attachment->host, attachment->side, &face_point, &face_direction))
            continue;
        TlVec2 *own = tl_partition_end_point(partition, attachment->end);
        TlVec2 other = *tl_partition_other_end_point(partition, attachment->end);
        TlVec2 direction = original_direction(token, partition);
        TlVec2 hit;
        if (tl_vec2_is_zero(direction) ||
            !tl_line_intersection(other, direction, face_point, face_direction, &hit)) {
            double t = tl_vec2_dot(tl_vec2_sub(*own, face_point), face_direction);
            hit = tl_vec2_add(face_point, tl_vec2_scale(face_direction, t));
        }
        *own = hit;
    }
}

static void restore_openings(TlProject *project, const TlEditToken *token)
{
    for (size_t i = 0; i < token->anchors.count; i++) {
        const OpeningAnchor *anchor = &token->anchors.items[i];
        TlBand band;
        if (!tl_project_band_of(project, anchor->host, &band))
            continue;
        double half = anchor->opening->width / 2, along = anchor->along;
        if (band.length >= anchor->opening->width)
            along = fmax(half, fmin(along, band.length - half));
        anchor->opening->center = tl_band_point(&band, along, tl_band_middle(&band));
    }
}

void tl_project_apply_edit(TlProject *project, TlEditToken *token, const TlEditOptions *options)
{
    const TlEditOptions no_options = {0};
    if (!options)
        options = &no_options;
    if (options->has_translate)
        carry_partitions(token, options->translate);
    for (int pass = 0; pass < FOLLOW_PASSES; pass++) {
        follow_master(token, options);
        if (!options->keep_ends)
            follow_faces(project, token, options, options->has_translate);
    }
    restore_openings(project, token);
    tl_measure_anchors_apply(project, token->measures);
    if (!options->skip_space_update)
        tl_project_update_spaces(project);
}

void tl_project_end_edit(TlProject *project, TlEditToken *token, const TlEditOptions *options)
{
    tl_project_apply_edit(project, token, options);
    tl_edit_token_free(token);
    tl_project_merge_straight_nodes(project);
    if (!options || !options->skip_space_update)
        tl_project_update_spaces(project);
}

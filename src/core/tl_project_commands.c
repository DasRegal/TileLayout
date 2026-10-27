/*
 * Добавление на план — общее для программы и сайта: контур (на пустом месте —
 * дом, внутри дома — комната из перегородок по его сторонам), перегородка,
 * проём. Каждая функция оставляет проект согласованным (комнаты пересчитаны).
 */
#include "tl_project.h"

#include <math.h>
#include <stdlib.h>

#include "tl_measure_anchor.h"
#include "tl_memory.h"

#define MIN_LENGTH 1.0 /* мм: короче — не перегородка */

static TlOutline *outline_containing(TlProject *project, const TlVec2 *corners, size_t count)
{
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        bool all_inside = true;
        for (size_t i = 0; i < count && all_inside; i++)
            all_inside = tl_outline_contains_or_touches(outline, corners[i]);
        if (all_inside)
            return outline;
    }
    return NULL;
}

static bool edge_lies_on_face(const TlFaceList *faces, TlVec2 a, TlVec2 b)
{
    for (size_t i = 0; i < faces->count; i++)
        if (tl_point_segment_distance(a, faces->items[i].start, faces->items[i].end, NULL) < 1 &&
            tl_point_segment_distance(b, faces->items[i].start, faces->items[i].end, NULL) < 1)
            return true;
    return false;
}

/* Перегородки по сторонам нарисованной комнаты: её внутренние размеры равны
 * нарисованным; стороны, легшие на существующие стены, пропускаются, у
 * соседних с ними перегородок конец — на этой стене. */
static void add_partitions_around(TlProject *project, const TlVec2 *corners, size_t n, double thickness)
{
    TlPolygon contour = tl_polygon_from_points(corners, n);
    TlFaceList faces = tl_project_faces(project);
    bool *on_wall = tl_calloc(n, sizeof *on_wall);
    TlVec2 *line_points = tl_calloc(n, sizeof *line_points);
    TlVec2 *directions = tl_calloc(n, sizeof *directions), *normals = tl_calloc(n, sizeof *normals);
    for (size_t i = 0; i < n; i++) {
        on_wall[i] = edge_lies_on_face(&faces, corners[i], corners[(i + 1) % n]);
        tl_polygon_edge_frame(&contour, i, &directions[i], &normals[i]);
        line_points[i] = tl_vec2_add(corners[i], tl_vec2_scale(normals[i], thickness / 2));
    }
    for (size_t i = 0; i < n; i++) {
        if (on_wall[i])
            continue;
        TlVec2 ends[2];
        for (int side = 0; side < 2; side++) {
            size_t neighbour = side == 0 ? (i + n - 1) % n : (i + 1) % n;
            double sign = side == 0 ? -1 : 1;
            TlVec2 hit;
            bool found;
            if (on_wall[neighbour]) {
                found = tl_line_intersection(line_points[i], directions[i], corners[neighbour],
                                             directions[neighbour], &hit);
            } else {
                found = tl_line_intersection(line_points[i], directions[i], line_points[neighbour],
                                             directions[neighbour], &hit);
                if (found) /* заходим на соседнюю перегородку, чтобы угол замкнулся */
                    hit = tl_vec2_add(hit, tl_vec2_scale(directions[i], sign * thickness / 2));
            }
            if (!found) {
                TlVec2 corner = side == 0 ? corners[i] : corners[(i + 1) % n];
                hit = tl_vec2_add(corner, tl_vec2_scale(normals[i], thickness / 2));
            }
            ends[side] = hit;
        }
        tl_project_add_partition(project, tl_partition_new(ends[0], ends[1], thickness));
    }
    free(on_wall);
    free(line_points);
    free(directions);
    free(normals);
    tl_face_list_free(&faces);
    tl_polygon_free(&contour);
}

TlContourResult tl_project_add_contour(TlProject *project, const TlVec2 *corners, size_t count,
                                       double house_thickness, double partition_thickness)
{
    if (outline_containing(project, corners, count)) {
        size_t rooms_before = project->spaces.count;
        add_partitions_around(project, corners, count, partition_thickness);
        tl_project_update_spaces(project);
        return project->spaces.count > rooms_before ? TL_CONTOUR_ROOM : TL_CONTOUR_PARTITIONS;
    }
    tl_project_add_outline(project, tl_outline_new(corners, count, house_thickness));
    tl_project_update_spaces(project);
    return TL_CONTOUR_HOUSE;
}

TlPartition *tl_project_draw_partition(TlProject *project, TlVec2 start, TlVec2 end, double thickness)
{
    if (tl_distance(start, end) < MIN_LENGTH)
        return NULL;
    TlPartition *partition = tl_partition_new(start, end, thickness);
    tl_project_add_partition(project, partition);
    tl_project_merge_straight_nodes(project); /* продолжила другую по прямой — одна перегородка */
    tl_project_update_spaces(project);
    /* Слившись с соседней, перегородка могла уйти (освобождена) — тогда это та, что
       легла на её место. */
    TlVec2 middle = tl_vec2_midpoint(start, end);
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *candidate = project->partitions.items[i];
        if (tl_point_segment_distance(middle, candidate->start, candidate->end, NULL) < MIN_LENGTH)
            return candidate;
    }
    return NULL;
}

TlOpening *tl_project_place_opening(TlProject *project, const TlBand *band, double along, double width)
{
    if (band->length < width)
        return NULL;
    TlOpening *opening = tl_opening_new(tl_band_point(band, along, tl_band_middle(band)), width);
    tl_project_add_opening(project, opening);
    tl_opening_set_threshold(opening, tl_project_default_threshold(project, opening));
    tl_project_merge_straight_nodes(project);
    tl_project_update_spaces(project);
    return opening;
}

/* ---- правка по числам ------------------------------------------------------------ */

bool tl_project_set_vertex(TlProject *project, TlOutline *outline, size_t index, TlVec2 point)
{
    size_t count = outline->inner.count;
    if (index >= count || outline->locked.items[index] || outline->locked.items[(index + count - 1) % count])
        return false;
    TlEditToken *token = tl_project_begin_edit(project, NULL);
    outline->inner.items[index] = point;
    tl_project_end_edit(project, token, NULL);
    return true;
}

#define NODE_CENTER_STEPS 8 /* середина узла сдвигается чуть иначе, чем сам узел, — уточняем */

bool tl_project_set_node_point(TlProject *project, const TlNode *node, int index, TlVec2 point)
{
    if (tl_node_locked(node))
        return false;
    TlPartition *partition = node->ends[0].partition;
    TlPartitionEnd end = node->ends[0].end;
    TlEditToken *token = tl_project_begin_edit(project, NULL);
    TlNode current = *node;
    if (index == 0) {
        for (int step = 0; step < NODE_CENTER_STEPS && tl_project_node_of(project, partition, end, &current); step++) {
            TlVec2 shift = tl_vec2_sub(point, tl_node_center(&current));
            tl_project_move_node(project, &current, tl_vec2_add(current.point, shift));
        }
    } else {
        tl_project_set_node_corner(project, &current, (size_t)index - 1, point);
    }
    tl_project_end_edit(project, token, NULL);
    return true;
}

bool tl_project_set_tee_point(TlProject *project, TlPartition *branch, TlPartitionEnd end, TlVec2 point,
                              bool *became_node)
{
    TlTee tee;
    if (!tl_project_tee_of(project, branch, end, &tee) || tee.branch->locked || tee.host->locked)
        return false;
    TlEditToken *token = tl_project_begin_edit(project, NULL);
    bool node = tl_project_move_tee(project, &tee, point);
    TlEditOptions options = {.frozen = tee.branch, .keep_ends = true};
    tl_project_end_edit(project, token, &options);
    if (became_node)
        *became_node = node;
    return true;
}

typedef struct LinkedShift {
    TlProject *project;
    bool linked;
} LinkedShift;

static void shift_with_links(TlOutline *outline, size_t wall, TlVec2 offset, void *context)
{
    LinkedShift *shift = context;
    tl_project_shift_wall(shift->project, outline, wall, offset, shift->linked);
}

bool tl_project_set_wall_length(TlProject *project, TlOutline *outline, size_t wall, double inner_length,
                                bool keep_angles, bool linked)
{
    if (wall >= tl_outline_wall_count(outline) || inner_length <= 1 || outline->locked.items[wall])
        return false;
    TlEditToken *token = tl_project_begin_edit(project, NULL);
    LinkedShift shift = {project, linked};
    tl_outline_set_inner_length(outline, wall, inner_length, keep_angles, shift_with_links, &shift);
    tl_project_end_edit(project, token, NULL);
    return true;
}

bool tl_project_place_opening_at(TlProject *project, TlOpening *opening, double distance, bool from_end)
{
    TlBand band;
    double start, end;
    if (opening->locked || !tl_project_opening_host(project, opening, &band) ||
        !tl_project_opening_span(project, opening, &start, &end))
        return false;
    double along = from_end ? end - distance - opening->width / 2 : start + distance + opening->width / 2;
    opening->center = tl_band_point(&band, along, tl_band_middle(&band));
    tl_project_merge_straight_nodes(project);
    tl_project_update_spaces(project);
    return true;
}

bool tl_project_set_threshold(TlProject *project, TlOpening *opening, double middle, double skew)
{
    if (opening->locked)
        return false;
    skew = fmax(-1, fmin(1, skew));
    middle = fmax(fabs(skew) / 2, fmin(1 - fabs(skew) / 2, middle));
    opening->threshold = middle - skew / 2;
    opening->threshold_end = middle + skew / 2;
    tl_project_merge_straight_nodes(project);
    tl_project_update_spaces(project);
    return true;
}

void tl_project_set_opening_width(TlProject *project, TlOpening *opening, double width)
{
    TlMeasureAnchors *measures = tl_measure_anchors_remember(project);
    opening->width = width;
    tl_measure_anchors_apply(project, measures);
    tl_measure_anchors_free(measures);
}

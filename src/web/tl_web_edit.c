/*
 * Правка плана из браузера: притягивание курсора, контур (дом или комната),
 * перегородка, проём, выбор предмета под курсором, его свойства и удаление.
 * Геометрия — в ядре (tl_project_commands.c, tl_snap.c), как у программы.
 */
#include <math.h>
#include <string.h>

#include "tl_drag.h"
#include "tl_snap.h"
#include "tl_web.h"
#include "tl_web_internal.h"

static char *answer; /* последний ответ строкой */

static const char *keep(TlJson *json) { return tl_web_keep(&answer, json); }

static TlJson *point_json(TlVec2 p)
{
    TlJson *array = tl_json_array();
    tl_json_array_push(array, tl_web_number(p.x));
    tl_json_array_push(array, tl_web_number(p.y));
    return array;
}

/* [x0, y0, x1, y1, …] из JSON — точки. */
static TlPolygon parse_points(const char *text)
{
    TlPolygon points = {0};
    TlJson *json = text ? tl_json_parse(text, NULL, 0) : NULL;
    for (size_t i = 0; i + 1 < tl_json_length(json); i += 2)
        tl_polygon_push(&points, tl_vec2(tl_json_number_or(tl_json_at(json, i), 0),
                                         tl_json_number_or(tl_json_at(json, i + 1), 0)));
    tl_json_free(json);
    return points;
}

/* ---- притягивание ------------------------------------------------------------- */

const char *tl_web_snap(double x, double y, bool has_start, double start_x, double start_y, const char *refs,
                        double tolerance, double grid, bool free)
{
    TlPolygon extra = parse_points(refs);
    TlSnapContext context = {.project = tl_web_project(), .tolerance = tolerance, .grid_step = grid,
                             .skip_vertex = -1, .align = true, .extra_refs = extra.items,
                             .extra_ref_count = extra.count};
    TlVec2 start = tl_vec2(start_x, start_y);
    TlSnapResult snap = tl_snap_wall_point(&context, tl_vec2(x, y), has_start ? &start : NULL, free);
    tl_polygon_free(&extra);
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "point", point_json(snap.point));
    tl_json_object_set(json, "on_face", tl_json_bool(snap.on_face));
    TlJson *face = tl_json_null();
    if (snap.on_face) {
        TlPolygon line = {0};
        tl_polygon_push(&line, snap.face_start);
        tl_polygon_push(&line, snap.face_end);
        face = tl_web_polygon(&line);
        tl_polygon_free(&line);
    }
    tl_json_object_set(json, "face", face);
    TlJson *guides = tl_json_array();
    for (int i = 0; i < snap.guides.count; i++) {
        TlJson *guide = point_json(snap.guides.items[i].from);
        tl_json_array_push(guide, tl_json_bool(snap.guides.items[i].vertical));
        tl_json_array_push(guides, guide);
    }
    tl_json_object_set(json, "guides", guides);
    return keep(json);
}

/* ---- добавление ------------------------------------------------------------------- */

const char *tl_web_add_contour(const char *points, double house_thickness, double partition_thickness,
                               double now_seconds)
{
    TlProject *project = tl_web_project();
    TlPolygon contour = parse_points(points);
    TlJson *json = tl_json_object();
    if (contour.count < 3 || tl_polygon_area(&contour) < 1) {
        tl_json_object_set(json, "result", tl_json_string("invalid"));
        tl_json_object_set(json, "space", tl_json_number(0));
        tl_polygon_free(&contour);
        return keep(json);
    }
    tl_web_checkpoint("", now_seconds);
    TlContourResult result = tl_project_add_contour(project, contour.items, contour.count, house_thickness,
                                                    partition_thickness);
    static const char *const NAMES[] = {"house", "room", "partitions"};
    TlSpace *space = tl_project_space_at(project, tl_polygon_label_point(&contour));
    tl_json_object_set(json, "result", tl_json_string(NAMES[result]));
    tl_json_object_set(json, "space", tl_json_number(space ? space->id : 0));
    tl_polygon_free(&contour);
    return keep(json);
}

static int index_of_partition(const TlProject *project, const TlPartition *partition)
{
    for (size_t i = 0; i < project->partitions.count; i++)
        if (project->partitions.items[i] == partition)
            return (int)i;
    return -1;
}

static int index_of_opening(const TlProject *project, const TlOpening *opening)
{
    for (size_t i = 0; i < project->openings.count; i++)
        if (project->openings.items[i] == opening)
            return (int)i;
    return -1;
}

int tl_web_add_partition(double x0, double y0, double x1, double y1, double thickness, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (tl_distance(tl_vec2(x0, y0), tl_vec2(x1, y1)) < 1 || thickness <= 0)
        return -1;
    tl_web_checkpoint("", now_seconds);
    TlPartition *partition = tl_project_draw_partition(project, tl_vec2(x0, y0), tl_vec2(x1, y1), thickness);
    return partition ? index_of_partition(project, partition) : -1;
}

/* Полоса стены у точки и центр проёма на ней (край — по шагу сетки, в пределах стены). */
static bool opening_place(TlVec2 point, double width, double tolerance, double grid, TlBand *band, double *along)
{
    if (!tl_project_find_band(tl_web_project(), point, tolerance, band) || band->length < width)
        return false;
    tl_band_local(band, point, along, NULL);
    *along = tl_snap_along(*along, width, band->length, grid);
    return true;
}

const char *tl_web_opening_preview(double x, double y, double width, double tolerance, double grid)
{
    TlBand band;
    double along;
    if (!opening_place(tl_vec2(x, y), width, tolerance, grid, &band, &along))
        return "null";
    TlPolygon cut = tl_project_opening_shape(tl_web_project(), &band, along, width);
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "cut", tl_web_polygon(&cut));
    tl_json_object_set(json, "host", tl_json_string(band.host.kind == TL_HOST_PARTITION ? "partition" : "wall"));
    tl_polygon_free(&cut);
    return keep(json);
}

int tl_web_add_opening(double x, double y, double width, double tolerance, double grid, double now_seconds)
{
    TlBand band;
    double along;
    if (!opening_place(tl_vec2(x, y), width, tolerance, grid, &band, &along))
        return -1;
    tl_web_checkpoint("", now_seconds);
    TlProject *project = tl_web_project();
    TlOpening *opening = tl_project_place_opening(project, &band, along, width);
    return opening ? index_of_opening(project, opening) : -1;
}

int tl_web_add_ruler(double x0, double y0, double x1, double y1, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (tl_distance(tl_vec2(x0, y0), tl_vec2(x1, y1)) < 1)
        return -1;
    tl_web_checkpoint("", now_seconds);
    tl_project_add_ruler(project, tl_ruler_new(tl_vec2(x0, y0), tl_vec2(x1, y1)));
    return (int)project->rulers.count - 1;
}

int tl_web_add_joint(double x0, double y0, double x1, double y1, double gap, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (tl_distance(tl_vec2(x0, y0), tl_vec2(x1, y1)) < 1 || !(gap > 0 && gap <= 100))
        return -1;
    tl_web_checkpoint("", now_seconds);
    tl_project_add_expansion_joint(project, tl_expansion_joint_new(tl_vec2(x0, y0), tl_vec2(x1, y1), gap));
    tl_project_update_spaces(project);
    return (int)project->expansion_joints.count - 1;
}

const char *tl_web_measure_point(double x, double y, bool has_anchor, double anchor_x, double anchor_y,
                                 const char *targets, double tolerance, bool free)
{
    TlVec2 anchor = tl_vec2(anchor_x, anchor_y), world = tl_vec2(x, y);
    TlSnapPoint snap = {world, TL_SNAP_NONE, {0, 0}};
    TlGuides guides = {0};
    TlVec2 point = free ? world
                        : tl_measure_point(tl_web_project(), world, has_anchor ? &anchor : NULL, tolerance,
                                           strcmp(targets, "walls") == 0 ? TL_SNAP_TO_WALL_SURFACES : TL_SNAP_TO_ALL,
                                           true, true, &snap, &guides);
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "point", point_json(point));
    tl_json_object_set(json, "snapped", tl_json_bool(snap.kind != TL_SNAP_NONE));
    TlJson *list = tl_json_array();
    for (int i = 0; i < guides.count; i++) {
        TlJson *guide = point_json(guides.items[i].from);
        tl_json_array_push(guide, tl_json_bool(guides.items[i].vertical));
        tl_json_array_push(list, guide);
    }
    tl_json_object_set(json, "guides", list);
    return keep(json);
}

/* ---- выбор -------------------------------------------------------------------- */

#define HANDLE_FACTOR 1.5 /* маркеры (углы, концы, узлы) ловятся чуть дальше тел */

/* Предмет плана — как в ответах для JavaScript. */
typedef struct Item {
    const char *kind;
    int index, part, end, space;
} Item;

static const char *item_json(Item item)
{
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "kind", tl_json_string(item.kind));
    tl_json_object_set(json, "index", tl_json_number(item.index));
    tl_json_object_set(json, "part", tl_json_number(item.part));
    tl_json_object_set(json, "end", tl_json_number(item.end));
    tl_json_object_set(json, "space", tl_json_number(item.space));
    return keep(json);
}

static TlJson *item_object(Item item)
{
    TlJson *json = tl_json_parse(item_json(item), NULL, 0);
    return json;
}

static bool on_opening(TlProject *project, const TlOpening *opening, TlVec2 point, double tolerance)
{
    TlBand band;
    if (!tl_project_opening_host(project, opening, &band))
        return false;
    double center, along, across;
    tl_band_local(&band, opening->center, &center, NULL);
    tl_band_local(&band, point, &along, &across);
    return fabs(along - center) <= opening->width / 2 && across >= fmin(band.near, band.far) - tolerance &&
           across <= fmax(band.near, band.far) + tolerance;
}

static bool end_in_node(TlProject *project, TlPartition *partition, TlPartitionEnd end)
{
    TlNode node;
    return tl_project_node_of(project, partition, end, &node);
}

/* Стена дома под точкой: в теле стен — ближайшая к точке внутренняя грань. */
static int wall_at(const TlOutline *outline, TlVec2 point)
{
    TlPolygon outer = tl_outline_outer(outline);
    bool inside = tl_polygon_contains(&outer, point) && !tl_polygon_contains(&outline->inner, point);
    tl_polygon_free(&outer);
    if (!inside)
        return -1;
    int best = -1;
    double best_distance = INFINITY;
    for (size_t i = 0; i < tl_outline_wall_count(outline); i++) {
        double distance = tl_point_segment_distance(point, tl_outline_wall_start(outline, i),
                                                    tl_outline_wall_end(outline, i), NULL);
        if (distance < best_distance) {
            best_distance = distance;
            best = (int)i;
        }
    }
    return best;
}

static bool pick_handle(TlProject *project, TlVec2 point, double tolerance, Item *item)
{
    double reach = tolerance * HANDLE_FACTOR;
    for (size_t o = 0; o < project->outlines.count; o++) {
        const TlPolygon *inner = &project->outlines.items[o]->inner;
        for (size_t i = 0; i < inner->count; i++)
            if (tl_distance(point, inner->items[i]) < reach) {
                *item = (Item){"vertex", (int)o, (int)i, 0, 0};
                return true;
            }
    }
    bool found = false;
    double best = reach;
    TlNodeList nodes = tl_project_nodes(project);
    for (size_t n = 0; n < nodes.count; n++) {
        double distance = tl_distance(point, tl_node_center(&nodes.items[n]));
        if (distance < best) {
            best = distance;
            const TlNodeEnd *end = &nodes.items[n].ends[0];
            *item = (Item){"node", index_of_partition(project, end->partition), 0, (int)end->end, 0};
            found = true;
        }
    }
    tl_node_list_free(&nodes);
    TlTeeList tees = tl_project_tees(project);
    for (size_t t = 0; t < tees.count; t++) {
        double distance = tl_distance(point, tees.items[t].point);
        if (distance < best) {
            best = distance;
            *item = (Item){"tee", index_of_partition(project, tees.items[t].branch), 0, (int)tees.items[t].end, 0};
            found = true;
        }
    }
    tl_tee_list_free(&tees);
    if (found)
        return true;
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        const TlVec2 ends[2] = {partition->start, partition->end};
        for (int e = 0; e < 2; e++)
            if (tl_distance(point, ends[e]) < reach && !end_in_node(project, partition, (TlPartitionEnd)e)) {
                TlTee tee;
                if (tl_project_tee_of(project, partition, (TlPartitionEnd)e, &tee))
                    continue; /* конец на боку другой — его тянут как примыкание */
                *item = (Item){"partition", (int)i, e + 1, 0, 0};
                return true;
            }
    }
    return false;
}

/* Часть отрезка под точкой: 1 — начало, 2 — конец, 0 — сам отрезок, −1 — мимо. */
static int segment_part(TlVec2 start, TlVec2 end, TlVec2 point, double end_tolerance, double line_tolerance)
{
    if (tl_distance(point, start) < end_tolerance * HANDLE_FACTOR)
        return 1;
    if (tl_distance(point, end) < end_tolerance * HANDLE_FACTOR)
        return 2;
    return tl_point_segment_distance(point, start, end, NULL) < line_tolerance ? 0 : -1;
}

/* Стык полов в проёме под точкой: 2/3 — конец у косяка, 1 — линия, 0 — мимо. */
static int threshold_part(TlProject *project, const TlOpening *opening, TlVec2 point, double tolerance)
{
    TlVec2 from, to;
    if (!tl_project_opening_threshold(project, opening, &from, &to))
        return 0;
    if (tl_distance(point, from) < tolerance)
        return TL_DRAG_THRESHOLD_FROM;
    if (tl_distance(point, to) < tolerance)
        return TL_DRAG_THRESHOLD_TO;
    return tl_point_segment_distance(point, from, to, NULL) < tolerance * 0.6 ? TL_DRAG_THRESHOLD_LINE : 0;
}

const char *tl_web_pick(double x, double y, double tolerance)
{
    TlProject *project = tl_web_project();
    TlVec2 point = tl_vec2(x, y);
    Item item = {"none", 0, 0, 0, 0};
    for (size_t i = project->rulers.count; i-- > 0;) {
        int part = segment_part(project->rulers.items[i]->start, project->rulers.items[i]->end, point, tolerance,
                                tolerance);
        if (part >= 0)
            return item_json((Item){"ruler", (int)i, part, 0, 0});
    }
    for (size_t i = project->expansion_joints.count; i-- > 0;) {
        const TlExpansionJoint *joint = project->expansion_joints.items[i];
        int part = segment_part(joint->start, joint->end, point, tolerance, fmax(tolerance, joint->gap / 2));
        if (part >= 0)
            return item_json((Item){"joint", (int)i, part, 0, 0});
    }
    if (pick_handle(project, point, tolerance, &item))
        return item_json(item);
    for (size_t i = 0; i < project->openings.count; i++) {
        int part = threshold_part(project, project->openings.items[i], point, tolerance);
        if (part > 0)
            return item_json((Item){"threshold", (int)i, part, 0, 0});
    }
    for (size_t i = 0; i < project->openings.count; i++)
        if (on_opening(project, project->openings.items[i], point, tolerance))
            return item_json((Item){"opening", (int)i, 0, 0, 0});
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        if (tl_point_segment_distance(point, partition->start, partition->end, NULL) <=
            tl_partition_max_thickness(partition) / 2 + tolerance)
            return item_json((Item){"partition", (int)i, 0, 0, 0});
    }
    for (size_t i = 0; i < project->outlines.count; i++) {
        int wall = wall_at(project->outlines.items[i], point);
        if (wall >= 0)
            return item_json((Item){"wall", (int)i, wall, 0, 0});
    }
    TlSpace *space = tl_project_space_at(project, point);
    return item_json(space ? (Item){"space", 0, 0, 0, space->id} : item);
}

static TlPartition *partition_at(int index)
{
    TlProject *project = tl_web_project();
    return index >= 0 && (size_t)index < project->partitions.count ? project->partitions.items[index] : NULL;
}

static TlOpening *opening_at(int index)
{
    TlProject *project = tl_web_project();
    return index >= 0 && (size_t)index < project->openings.count ? project->openings.items[index] : NULL;
}

static TlOutline *house_at(int index)
{
    TlProject *project = tl_web_project();
    return index >= 0 && (size_t)index < project->outlines.count ? project->outlines.items[index] : NULL;
}

static bool is(const char *kind, const char *expected) { return strcmp(kind, expected) == 0; }

static void add_point(TlJson *array, TlVec2 p) { tl_json_array_push(array, point_json(p)); }

static void add_outline(TlJson *outlines, const TlPolygon *polygon) { tl_json_array_push(outlines, tl_web_polygon(polygon)); }

static bool node_locked(const TlNode *node)
{
    for (size_t i = 0; i < node->count; i++)
        if (node->ends[i].partition->locked)
            return true;
    return false;
}

/* Свойства дома: подсветка — обе стороны стен, маркеры — углы. */
static void house_properties(TlJson *json, TlOutline *house, TlJson *outlines, TlJson *handles)
{
    TlPolygon outer = tl_outline_outer(house);
    add_outline(outlines, &outer);
    add_outline(outlines, &house->inner);
    for (size_t i = 0; i < house->inner.count; i++)
        add_point(handles, house->inner.items[i]);
    tl_json_object_set(json, "walls", tl_json_number((double)tl_outline_wall_count(house)));
    tl_json_object_set(json, "locked", tl_json_bool(tl_outline_any_locked(house)));
    tl_polygon_free(&outer);
}

const char *tl_web_object(const char *kind, int index, int part, int end)
{
    TlProject *project = tl_web_project();
    TlJson *json = item_object((Item){kind, index, part, end, 0});
    TlJson *outlines = tl_json_array(), *handles = tl_json_array();
    TlPartition *partition = partition_at(index);
    TlOpening *opening = opening_at(index);
    TlOutline *house = house_at(index);
    bool found = true;
    if (is(kind, "partition") && partition) {
        TlPolygon body = tl_project_partition_body(project, partition, 0);
        add_outline(outlines, &body);
        tl_polygon_free(&body);
        if (!end_in_node(project, partition, TL_PARTITION_START))
            add_point(handles, partition->start);
        if (!end_in_node(project, partition, TL_PARTITION_END))
            add_point(handles, partition->end);
        tl_json_object_set(json, "thickness", tl_json_number(tl_partition_max_thickness(partition)));
        tl_json_object_set(json, "length", tl_json_number(round(tl_partition_length(partition))));
        tl_json_object_set(json, "locked", tl_json_bool(partition->locked));
    } else if (is(kind, "opening") && opening) {
        TlBand band;
        bool hosted = tl_project_opening_host(project, opening, &band);
        TlPolygon cut = {0};
        if (tl_project_opening_cut(project, opening, &cut))
            add_outline(outlines, &cut);
        tl_polygon_free(&cut);
        tl_json_object_set(json, "width", tl_json_number(opening->width));
        tl_json_object_set(json, "host", tl_json_string(!hosted ? "none"
                                                        : band.host.kind == TL_HOST_PARTITION ? "partition"
                                                                                              : "wall"));
        tl_json_object_set(json, "locked", tl_json_bool(opening->locked));
    } else if ((is(kind, "wall") || is(kind, "vertex") || is(kind, "house")) && house &&
               (is(kind, "house") || (part >= 0 && (size_t)part < house->inner.count))) {
        house_properties(json, house, outlines, handles);
        if (is(kind, "wall")) {
            tl_json_object_set(json, "thickness", tl_json_number(house->thickness.items[part]));
            tl_json_object_set(json, "length", tl_json_number(round(tl_distance(
                                                   tl_outline_wall_start(house, (size_t)part),
                                                   tl_outline_wall_end(house, (size_t)part)))));
            tl_json_object_set(json, "locked", tl_json_bool(house->locked.items[part]));
        } else if (is(kind, "vertex")) {
            tl_json_object_set(json, "locked", tl_json_bool(tl_outline_vertex_locked(house, (size_t)part)));
        }
    } else if (is(kind, "ruler") && index >= 0 && (size_t)index < project->rulers.count) {
        const TlRuler *ruler = project->rulers.items[index];
        TlPolygon line = {0};
        tl_polygon_push(&line, ruler->start);
        tl_polygon_push(&line, ruler->end);
        add_outline(outlines, &line);
        tl_polygon_free(&line);
        add_point(handles, ruler->start);
        add_point(handles, ruler->end);
        tl_json_object_set(json, "length", tl_json_number(round(tl_ruler_length(ruler))));
        tl_json_object_set(json, "locked", tl_json_bool(false));
    } else if (is(kind, "joint") && index >= 0 && (size_t)index < project->expansion_joints.count) {
        const TlExpansionJoint *joint = project->expansion_joints.items[index];
        TlPolygon strip = tl_expansion_joint_strip(joint);
        add_outline(outlines, &strip);
        tl_polygon_free(&strip);
        add_point(handles, joint->start);
        add_point(handles, joint->end);
        tl_json_object_set(json, "gap", tl_json_number(joint->gap));
        tl_json_object_set(json, "length", tl_json_number(round(tl_expansion_joint_length(joint))));
        tl_json_object_set(json, "locked", tl_json_bool(false));
    } else if (is(kind, "threshold") && opening) {
        TlVec2 from, to;
        if (!tl_project_opening_threshold(project, opening, &from, &to)) {
            found = false;
        } else {
            TlPolygon line = {0};
            tl_polygon_push(&line, from);
            tl_polygon_push(&line, to);
            add_outline(outlines, &line);
            tl_polygon_free(&line);
            add_point(handles, from);
            add_point(handles, to);
            tl_json_object_set(json, "locked", tl_json_bool(opening->locked));
        }
    } else if ((is(kind, "node") || is(kind, "tee")) && partition) {
        TlNode node;
        TlTee tee;
        if (is(kind, "node") && tl_project_node_of(project, partition, (TlPartitionEnd)end, &node)) {
            for (size_t i = 0; i < node.count; i++) {
                TlPolygon body = tl_project_partition_body(project, node.ends[i].partition, 0);
                add_outline(outlines, &body);
                tl_polygon_free(&body);
            }
            add_point(handles, tl_node_center(&node));
            tl_json_object_set(json, "count", tl_json_number((double)node.count));
            tl_json_object_set(json, "locked", tl_json_bool(node_locked(&node)));
        } else if (is(kind, "tee") && tl_project_tee_of(project, partition, (TlPartitionEnd)end, &tee)) {
            TlPolygon body = tl_project_partition_body(project, tee.branch, 0);
            add_outline(outlines, &body);
            tl_polygon_free(&body);
            add_point(handles, tee.point);
            tl_json_object_set(json, "locked", tl_json_bool(tee.branch->locked || tee.host->locked));
        } else {
            found = false;
        }
    } else {
        found = false;
    }
    if (!found) {
        tl_json_free(outlines);
        tl_json_free(handles);
        tl_json_free(json);
        return "null";
    }
    tl_json_object_set(json, "outline", outlines);
    tl_json_object_set(json, "handles", handles);
    return keep(json);
}

/* ---- изменение и удаление -------------------------------------------------------- */

bool tl_web_set_partition_thickness(int index, double thickness, double now_seconds)
{
    TlPartition *partition = partition_at(index);
    if (!partition || partition->locked || !(thickness >= 10 && thickness <= 1000))
        return false;
    TlProject *project = tl_web_project();
    tl_web_checkpoint("pthick", now_seconds);
    TlEditToken *token = tl_project_begin_edit(project, NULL);
    tl_partition_set_thickness(partition, thickness);
    TlEditOptions options = {.master = partition};
    tl_project_end_edit(project, token, &options);
    return true;
}

bool tl_web_set_opening_width(int index, double width, double now_seconds)
{
    TlOpening *opening = opening_at(index);
    if (!opening || opening->locked || !(width >= 100 && width <= 10000))
        return false;
    tl_web_checkpoint("owidth", now_seconds);
    opening->width = width;
    TlProject *project = tl_web_project();
    tl_project_merge_straight_nodes(project);
    tl_project_update_spaces(project);
    return true;
}

bool tl_web_set_wall_thickness(int house_index, int wall, double thickness, double now_seconds)
{
    TlOutline *house = house_at(house_index);
    if (!house || wall < 0 || (size_t)wall >= tl_outline_wall_count(house) || house->locked.items[wall] ||
        !(thickness >= 10 && thickness <= 2000))
        return false;
    tl_web_checkpoint("wthick", now_seconds);
    tl_project_set_thickness(tl_web_project(), house, (size_t)wall, thickness, true);
    return true;
}

bool tl_web_set_joint_gap(int index, double gap, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (index < 0 || (size_t)index >= project->expansion_joints.count || !(gap > 0 && gap <= 100))
        return false;
    tl_web_checkpoint("jgap", now_seconds);
    project->expansion_joints.items[index]->gap = gap;
    tl_project_update_spaces(project);
    return true;
}

bool tl_web_delete(const char *kind, int index, int part, int end, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlPartition *partition = partition_at(index);
    TlOpening *opening = opening_at(index);
    TlOutline *house = house_at(index);
    if (is(kind, "partition") && partition && !partition->locked) {
        tl_web_checkpoint("", now_seconds);
        tl_project_delete_partition(project, partition);
        tl_project_remove_orphan_openings(project);
    } else if (is(kind, "opening") && opening && !opening->locked) {
        tl_web_checkpoint("", now_seconds);
        tl_project_delete_opening(project, opening);
    } else if ((is(kind, "house") || is(kind, "wall")) && house && !tl_outline_any_locked(house)) {
        tl_web_checkpoint("", now_seconds);
        tl_project_delete_outline(project, house);
    } else if (is(kind, "vertex") && house && part >= 0 && (size_t)part < house->inner.count &&
               tl_outline_wall_count(house) > 3 && !tl_outline_vertex_locked(house, (size_t)part)) {
        tl_web_checkpoint("", now_seconds);
        TlEditToken *token = tl_project_begin_edit(project, NULL);
        tl_outline_remove_vertex(house, (size_t)part);
        tl_project_end_edit(project, token, NULL);
        tl_project_remove_orphan_openings(project);
    } else if (is(kind, "ruler") && index >= 0 && (size_t)index < project->rulers.count) {
        tl_web_checkpoint("", now_seconds);
        tl_project_delete_ruler(project, project->rulers.items[index]);
    } else if (is(kind, "joint") && index >= 0 && (size_t)index < project->expansion_joints.count) {
        tl_web_checkpoint("", now_seconds);
        tl_project_delete_expansion_joint(project, project->expansion_joints.items[index]);
    } else if (is(kind, "node") && partition) {
        TlNode node;
        if (!tl_project_node_of(project, partition, (TlPartitionEnd)end, &node) || node_locked(&node))
            return false;
        tl_web_checkpoint("", now_seconds);
        tl_project_dissolve_node(project, &node);
    } else {
        return false;
    }
    tl_project_update_spaces(project);
    return true;
}

/* ---- перетаскивание ------------------------------------------------------------- */

static struct {
    TlDrag drag;
    bool active, moved;
    int space; /* дом и сетка плитки — id комнаты, за которую взяли */
} dragging;

bool tl_web_drag_begin(const char *kind, int index, int part, int end, double x, double y)
{
    TlProject *project = tl_web_project();
    if (dragging.active)
        tl_drag_free(&dragging.drag);
    dragging.active = dragging.moved = false;
    TlDragTarget target = {.index = part, .end = (TlPartitionEnd)end, .link_walls = true};
    if (is(kind, "vertex") || is(kind, "wall")) {
        target.kind = is(kind, "vertex") ? TL_DRAG_VERTEX : TL_DRAG_WALL;
        target.outline = house_at(index);
        if (!target.outline)
            return false;
    } else if (is(kind, "partition") || is(kind, "node") || is(kind, "tee")) {
        target.kind = is(kind, "partition") ? TL_DRAG_PARTITION : is(kind, "node") ? TL_DRAG_NODE : TL_DRAG_TEE;
        target.partition = partition_at(index);
        if (!target.partition)
            return false;
        if (target.kind == TL_DRAG_NODE)
            target.index = 0; /* за середину — узел целиком */
    } else if (is(kind, "opening") || is(kind, "threshold")) {
        target.kind = is(kind, "opening") ? TL_DRAG_OPENING : TL_DRAG_THRESHOLD;
        target.opening = opening_at(index);
        if (!target.opening)
            return false;
    } else if (is(kind, "house") || is(kind, "tiles")) {
        TlSpace *space = tl_project_space_at(project, tl_vec2(x, y));
        if (!space || (is(kind, "house") && !space->house))
            return false;
        target.kind = is(kind, "house") ? TL_DRAG_HOUSE : TL_DRAG_TILES;
        target.outline = space->house;
        target.space = space;
        dragging.space = space->id;
    } else if (is(kind, "ruler") && index >= 0 && (size_t)index < project->rulers.count) {
        target.kind = TL_DRAG_RULER;
        target.ruler = project->rulers.items[index];
    } else if (is(kind, "joint") && index >= 0 && (size_t)index < project->expansion_joints.count) {
        target.kind = TL_DRAG_JOINT;
        target.joint = project->expansion_joints.items[index];
    } else {
        return false;
    }
    dragging.active = tl_drag_begin(&dragging.drag, project, target, tl_vec2(x, y));
    return dragging.active;
}

/* Предмет, который тянут сейчас, — номерами для JavaScript. */
static Item dragged_item(TlProject *project)
{
    const TlDragTarget *t = &dragging.drag.target;
    switch (t->kind) {
    case TL_DRAG_VERTEX:
    case TL_DRAG_WALL: {
        int house = -1;
        for (size_t i = 0; i < project->outlines.count; i++)
            if (project->outlines.items[i] == t->outline)
                house = (int)i;
        return (Item){t->kind == TL_DRAG_VERTEX ? "vertex" : "wall", house, t->index, 0, 0};
    }
    case TL_DRAG_PARTITION: return (Item){"partition", index_of_partition(project, t->partition), t->index, 0, 0};
    case TL_DRAG_NODE: return (Item){"node", index_of_partition(project, t->partition), 0, (int)t->end, 0};
    case TL_DRAG_TEE: return (Item){"tee", index_of_partition(project, t->partition), 0, (int)t->end, 0};
    case TL_DRAG_OPENING: return (Item){"opening", index_of_opening(project, t->opening), 0, 0, 0};
    case TL_DRAG_THRESHOLD: return (Item){"threshold", index_of_opening(project, t->opening), t->index, 0, 0};
    case TL_DRAG_HOUSE:
    case TL_DRAG_TILES: return (Item){"space", 0, 0, 0, dragging.space};
    case TL_DRAG_RULER:
    case TL_DRAG_JOINT: {
        int index = -1;
        if (t->kind == TL_DRAG_RULER) {
            for (size_t i = 0; i < project->rulers.count; i++)
                if (project->rulers.items[i] == t->ruler)
                    index = (int)i;
        } else {
            for (size_t i = 0; i < project->expansion_joints.count; i++)
                if (project->expansion_joints.items[i] == t->joint)
                    index = (int)i;
        }
        return (Item){t->kind == TL_DRAG_RULER ? "ruler" : "joint", index, t->index, 0, 0};
    }
    default: return (Item){"none", 0, 0, 0, 0};
    }
}

const char *tl_web_drag_move(double x, double y, double tolerance, double grid, bool free, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (!dragging.active)
        return "null";
    if (!dragging.moved) {
        tl_web_checkpoint("", now_seconds);
        dragging.moved = true;
    }
    TlSnapContext snap = {.project = project, .tolerance = tolerance, .grid_step = grid, .skip_vertex = -1,
                          .align = true, .magnet = true};
    TlGuides guides;
    tl_drag_move(&dragging.drag, project, tl_vec2(x, y), &snap, free, &guides);
    TlJson *json = tl_json_object();
    TlJson *guide_list = tl_json_array();
    for (int i = 0; i < guides.count; i++) {
        TlJson *guide = point_json(guides.items[i].from);
        tl_json_array_push(guide, tl_json_bool(guides.items[i].vertical));
        tl_json_array_push(guide_list, guide);
    }
    tl_json_object_set(json, "guides", guide_list);
    tl_json_object_set(json, "selection", item_object(dragged_item(project)));
    return keep(json);
}

const char *tl_web_drag_end(void)
{
    TlProject *project = tl_web_project();
    if (!dragging.active)
        return "null";
    bool moved = dragging.moved;
    /* перегородка, которую тянули, могла слиться с соседней — запомнить её середину */
    TlPartition *partition = dragging.drag.target.partition;
    bool partition_kind = dragging.drag.target.kind == TL_DRAG_PARTITION;
    TlVec2 middle = partition_kind ? tl_vec2_midpoint(partition->start, partition->end) : tl_vec2(0, 0);
    Item item = dragged_item(project);
    if (moved)
        tl_drag_end(&dragging.drag, project);
    else
        tl_drag_free(&dragging.drag);
    dragging.active = dragging.moved = false;
    if (partition_kind) {
        item.index = -1;
        for (size_t i = 0; i < project->partitions.count; i++) {
            const TlPartition *candidate = project->partitions.items[i];
            if (tl_point_segment_distance(middle, candidate->start, candidate->end, NULL) < 1)
                item.index = (int)i;
        }
        if (item.index < 0)
            item.kind = "none";
    }
    return item_json(item);
}

/*
 * Правка плана из браузера: притягивание курсора, контур (дом или комната),
 * перегородка, проём, выбор предмета под курсором, его свойства и удаление.
 * Геометрия — в ядре (tl_project_commands.c, tl_snap.c), как у программы.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tl_drag.h"
#include "tl_layout.h"
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

int tl_web_add_divider(double x0, double y0, double x1, double y1, double gap, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (tl_distance(tl_vec2(x0, y0), tl_vec2(x1, y1)) < 1 || !(gap >= 0 && gap <= 50))
        return -1;
    tl_web_checkpoint("", now_seconds);
    TlExpansionJoint *line = tl_expansion_joint_new(tl_vec2(x0, y0), tl_vec2(x1, y1), gap);
    line->kind = TL_LINE_DIVIDER;
    tl_project_add_expansion_joint(project, line);
    tl_project_update_spaces(project);
    return (int)project->expansion_joints.count - 1;
}

#define FIXTURE_WALL_REACH_MM 400.0

int tl_web_add_drain(const char *kind, double x, double y, bool free, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlDrain drain = tl_drain_default(tl_drain_kind_from_key(kind), tl_vec2(x, y));
    TlSpace *space = tl_project_space_at(project, drain.center);
    if (space && drain.kind == TL_DRAIN_POINT) /* решётка — по плитке комнаты */
        drain.angle = tl_space_effective_tile(space)->rotation_degrees;
    if (space && drain.kind == TL_DRAIN_LINEAR && !free) {
        TlPolygon walls = tl_space_wall_outline(space);
        tl_drain_snap_to_walls(&drain, &walls, FIXTURE_WALL_REACH_MM);
        tl_polygon_free(&walls);
    }
    tl_web_checkpoint("", now_seconds);
    tl_drain_list_push(&project->drains, drain);
    tl_project_update_spaces(project);
    return (int)project->drains.count - 1;
}

bool tl_web_set_drain(int index, const char *fields, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    bool ok = index >= 0 && (size_t)index < project->drains.count && parsed && tl_json_type(parsed) == TL_JSON_OBJECT;
    if (ok) {
        TlDrain drain = project->drains.items[index];
        const TlJson *kind = tl_json_get(parsed, "kind");
        if (kind && tl_drain_kind_from_key(tl_json_string_or(kind, "")) != drain.kind) {
            double angle = drain.angle;
            drain = tl_drain_default(tl_drain_kind_from_key(tl_json_string_or(kind, "")), drain.center);
            drain.angle = angle;
        }
        static const char *const NUMBERS[] = {"size", "length", "angle", "slope", "x", "y"};
        double *const targets[] = {&drain.size, &drain.length, &drain.angle, &drain.slope, &drain.center.x, &drain.center.y};
        for (size_t i = 0; i < 6; i++) {
            const TlJson *value = tl_json_get(parsed, NUMBERS[i]);
            if (value)
                ok = ok && isfinite(*targets[i] = tl_json_number_or(value, NAN));
        }
        const TlJson *envelope = tl_json_get(parsed, "envelope");
        if (envelope)
            drain.envelope = tl_json_bool_or(envelope, drain.envelope);
        ok = ok && drain.size >= 10 && (drain.kind != TL_DRAIN_LINEAR || drain.length >= 10) && drain.slope >= 0;
        if (ok) {
            char merge_key[48] = "";
            if (tl_json_length(parsed) == 1)
                snprintf(merge_key, sizeof merge_key, "drain_%d_%s", index, tl_json_key_at(parsed, 0));
            tl_web_checkpoint(merge_key, now_seconds);
            project->drains.items[index] = drain;
            tl_project_update_spaces(project);
        }
    }
    tl_json_free(parsed);
    return ok;
}

int tl_web_add_fixture(const char *kind, double x, double y, bool free, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlFixture fixture = tl_fixture_default(tl_fixture_kind_from_key(kind), tl_vec2(x, y));
    TlSpace *space = free ? NULL : tl_project_space_at(project, fixture.center);
    if (space) { /* у стены — спинкой к ней */
        TlPolygon walls = tl_space_wall_outline(space);
        tl_fixture_snap_to_walls(&fixture, &walls, FIXTURE_WALL_REACH_MM);
        tl_polygon_free(&walls);
    }
    tl_web_checkpoint("", now_seconds);
    tl_fixture_list_push(&project->fixtures, fixture);
    tl_project_update_spaces(project);
    return (int)project->fixtures.count - 1;
}

bool tl_web_set_fixture(int index, const char *fields, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlJson *parsed = tl_json_parse(fields, NULL, 0);
    bool ok = index >= 0 && (size_t)index < project->fixtures.count && parsed && tl_json_type(parsed) == TL_JSON_OBJECT;
    if (ok) {
        TlFixture fixture = project->fixtures.items[index];
        const TlJson *kind = tl_json_get(parsed, "kind");
        if (kind && tl_fixture_kind_from_key(tl_json_string_or(kind, "")) != fixture.kind) { /* вид — обычные размеры */
            double angle = fixture.angle;
            fixture = tl_fixture_default(tl_fixture_kind_from_key(tl_json_string_or(kind, "")), fixture.center);
            fixture.angle = angle;
        }
        static const char *const NUMBERS[] = {"w", "d", "h", "angle", "x", "y"};
        double *const targets[] = {&fixture.width, &fixture.depth, &fixture.height, &fixture.angle, &fixture.center.x,
                                   &fixture.center.y};
        for (size_t i = 0; i < 6; i++) {
            const TlJson *value = tl_json_get(parsed, NUMBERS[i]);
            if (value)
                ok = ok && isfinite(*targets[i] = tl_json_number_or(value, NAN));
        }
        const TlJson *under = tl_json_get(parsed, "under"), *behind = tl_json_get(parsed, "behind");
        if (under)
            fixture.under = tl_json_bool_or(under, fixture.under);
        if (behind)
            fixture.behind = tl_json_bool_or(behind, fixture.behind);
        const TlJson *steps = tl_json_get(parsed, "steps");
        if (steps && fixture.kind == TL_FIXTURE_STAIRS)
            fixture.steps = (int)tl_json_number_or(steps, fixture.steps);
        ok = ok && (fixture.kind != TL_FIXTURE_STAIRS || (fixture.steps >= 1 && fixture.steps <= 50));
        ok = ok && fixture.width >= 10 && fixture.depth >= 10 && fixture.height >= 0;
        if (ok) {
            char merge_key[48] = "";
            if (tl_json_length(parsed) == 1)
                snprintf(merge_key, sizeof merge_key, "fixture_%d_%s", index, tl_json_key_at(parsed, 0));
            tl_web_checkpoint(merge_key, now_seconds);
            project->fixtures.items[index] = fixture;
            tl_project_update_spaces(project);
        }
    }
    tl_json_free(parsed);
    return ok;
}

int tl_web_add_pipe(double x, double y, double diameter, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (!(diameter >= TL_PIPE_MIN_DIAMETER && diameter <= TL_PIPE_MAX_DIAMETER))
        return -1;
    tl_web_checkpoint("", now_seconds);
    tl_project_add_pipe(project, tl_pipe_new(tl_vec2(x, y), diameter));
    tl_project_update_spaces(project);
    return (int)project->pipes.count - 1;
}

bool tl_web_set_pipe(int index, double x, double y, double diameter, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (index < 0 || (size_t)index >= project->pipes.count ||
        !(diameter >= TL_PIPE_MIN_DIAMETER && diameter <= TL_PIPE_MAX_DIAMETER) || !isfinite(x) || !isfinite(y))
        return false;
    tl_web_checkpoint("pipe", now_seconds);
    project->pipes.items[index]->center = tl_vec2(x, y);
    project->pipes.items[index]->diameter = diameter;
    tl_project_update_spaces(project);
    return true;
}

const char *tl_web_measure_point(double x, double y, bool has_anchor, double anchor_x, double anchor_y,
                                 const char *targets, double tolerance, bool free)
{
    TlVec2 anchor = tl_vec2(anchor_x, anchor_y), world = tl_vec2(x, y);
    TlSnapPoint snap = {world, TL_SNAP_NONE, {0, 0}};
    TlGuides guides = {0};
    TlVec2 point = free ? world
                        : tl_measure_point(tl_web_project(), world, has_anchor ? &anchor : NULL, tolerance,
                                           strcmp(targets, "walls") == 0 ? TL_SNAP_TO_WALL_SURFACES | TL_SNAP_TO_LINES
                                                                         : TL_SNAP_TO_ALL,
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

static const TlPartitionEnd ENDS[2] = {TL_PARTITION_START, TL_PARTITION_END};

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
        for (int e = 0; e < 2; e++)
            if (tl_distance(point, *tl_partition_end_point(partition, ENDS[e])) < reach &&
                !end_in_node(project, partition, ENDS[e])) {
                TlTee tee;
                if (tl_project_tee_of(project, partition, ENDS[e], &tee))
                    continue; /* конец на боку другой — его тянут как примыкание */
                *item = (Item){"partition", (int)i, e == 0 ? TL_DRAG_PARTITION_START : TL_DRAG_PARTITION_END, 0, 0};
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

/* Стык полов в проёме под точкой: 2/3 — конец у косяка, 1 — линия, 0 — мимо.
   Полоса у линии — не шире трети глубины проёма: остальной проём — сама дверь. */
static int threshold_part(TlProject *project, const TlOpening *opening, TlVec2 point, double tolerance)
{
    TlVec2 from, to, right, left;
    if (!tl_project_opening_threshold(project, opening, &from, &to) ||
        !tl_project_opening_section(project, opening, &right, &left))
        return 0;
    double band = fmin(tolerance * 0.6, tl_distance(right, left) / 6);
    if (tl_distance(point, from) < band * 1.5)
        return TL_DRAG_THRESHOLD_FROM;
    if (tl_distance(point, to) < band * 1.5)
        return TL_DRAG_THRESHOLD_TO;
    return tl_point_segment_distance(point, from, to, NULL) < band ? TL_DRAG_THRESHOLD_LINE : 0;
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
    for (size_t i = project->pipes.count; i-- > 0;) {
        const TlPipe *pipe = project->pipes.items[i];
        if (tl_distance(point, pipe->center) < fmax(pipe->diameter / 2, tolerance))
            return item_json((Item){"pipe", (int)i, 0, 0, 0});
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
    for (size_t i = project->drains.count; i-- > 0;) { /* трапы — поверх пола и предметов */
        TlPolygon outline = tl_drain_outline(&project->drains.items[i]);
        bool inside = tl_polygon_contains(&outline, point) || tl_distance(point, project->drains.items[i].center) < tolerance;
        tl_polygon_free(&outline);
        if (inside)
            return item_json((Item){"drain", (int)i, 0, 0, 0});
    }
    for (size_t i = project->fixtures.count; i-- > 0;) { /* предметы — поверх пола */
        TlPolygon outline = tl_fixture_outline(&project->fixtures.items[i]);
        bool inside = tl_polygon_contains(&outline, point);
        tl_polygon_free(&outline);
        if (inside)
            return item_json((Item){"fixture", (int)i, 0, 0, 0});
    }
    TlSpace *space = tl_project_space_at(project, point);
    for (size_t i = 0; space && i < space->pannos.count; i++) { /* панно на полу — поверх комнаты */
        if (space->pannos.items[i].wall != TL_PANNO_FLOOR)
            continue;
        TlPolygon outline = tl_space_panno_outline(space, i);
        bool inside = tl_polygon_contains(&outline, point);
        tl_polygon_free(&outline);
        if (inside)
            return item_json((Item){"panno", (int)i, 0, 0, space->id});
    }
    if (!space)
        return item_json(item);
    size_t region = space->regions.count > 1 ? tl_space_region_at(space, point) : SIZE_MAX; /* часть пола + 1 */
    return item_json((Item){"space", 0, region == SIZE_MAX ? 0 : (int)region + 1, 0, space->id});
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

/* Маркер выделенного предмета: точка, вид ("end" — конец, "corner" — угол тела или
   узла, "middle" — середина, "node" — середина узла, "point" — точка примыкания,
   угол дома, конец стыка) и что за него тянут (как от tl_web_pick). */
static void add_handle(TlJson *handles, TlVec2 point, const char *shape, Item grab)
{
    TlJson *handle = tl_json_object();
    tl_json_object_set(handle, "point", point_json(point));
    tl_json_object_set(handle, "shape", tl_json_string(shape));
    tl_json_object_set(handle, "grab", item_object(grab));
    tl_json_array_push(handles, handle);
}

/* Середина отрезка — маркер, за который тянут предмет целиком. */
static void add_middle(TlJson *handles, TlVec2 a, TlVec2 b, Item grab)
{
    add_handle(handles, tl_vec2_midpoint(a, b), "middle", grab);
}

static void add_outline(TlJson *outlines, const TlPolygon *polygon) { tl_json_array_push(outlines, tl_web_polygon(polygon)); }

static int index_of_house(const TlOutline *house)
{
    TlProject *project = tl_web_project();
    for (size_t i = 0; i < project->outlines.count; i++)
        if (project->outlines.items[i] == house)
            return (int)i;
    return -1;
}

/* Точки узла: середина тянет узел целиком, углы — углы тел у узла. */
static void add_node_handles(TlJson *handles, const TlNode *node, int partition_index, int end)
{
    TlNodeCorner corners[TL_NODE_MAX];
    size_t count = tl_node_corners(node, corners);
    for (size_t k = 0; k < count; k++)
        add_handle(handles, corners[k].first, "corner", (Item){"node", partition_index, (int)k + 1, end, 0});
    add_handle(handles, tl_node_center(node), "node", (Item){"node", partition_index, 0, end, 0});
}

/* Маркеры перегородки: свободный конец и углы тела у него, у конца в узле —
   точки узла, точки примыканий, середина. */
static void add_partition_handles(TlProject *project, TlJson *handles, TlPartition *partition, int index)
{
    TlPolygon body = tl_project_partition_body(project, partition, 0);
    for (int e = 0; e < 2; e++) {
        TlPartitionEnd end = ENDS[e];
        TlNode node;
        if (tl_project_node_of(project, partition, end, &node)) {
            add_node_handles(handles, &node, index, (int)end);
            continue;
        }
        TlTee tee;
        if (tl_project_tee_of(project, partition, end, &tee))
            continue; /* конец на боку другой — точка примыкания ниже */
        for (size_t corner = 0; corner < body.count; corner++)
            if (tl_partition_corner_end((TlPartitionCorner)corner) == end)
                add_handle(handles, body.items[corner], "corner",
                           (Item){"partition", index, TL_DRAG_PARTITION_FIRST_CORNER + (int)corner, 0, 0});
        add_handle(handles, *tl_partition_end_point(partition, end), "end",
                   (Item){"partition", index, e == 0 ? TL_DRAG_PARTITION_START : TL_DRAG_PARTITION_END, 0, 0});
    }
    tl_polygon_free(&body);
    TlTeeList tees = tl_project_tees(project);
    for (size_t i = 0; i < tees.count; i++)
        if (tees.items[i].branch == partition || tees.items[i].host == partition)
            add_handle(handles, tees.items[i].point, "point",
                       (Item){"tee", index_of_partition(project, tees.items[i].branch), 0, (int)tees.items[i].end, 0});
    tl_tee_list_free(&tees);
    add_middle(handles, partition->start, partition->end, (Item){"partition", index, TL_DRAG_PARTITION_BODY, 0, 0});
}

/* Свойства дома: подсветка — обе стороны стен, маркеры — углы. */
static void house_properties(TlJson *json, TlOutline *house, TlJson *outlines, TlJson *handles)
{
    TlPolygon outer = tl_outline_outer(house);
    add_outline(outlines, &outer);
    add_outline(outlines, &house->inner);
    for (size_t i = 0; i < house->inner.count; i++)
        add_handle(handles, house->inner.items[i], "point", (Item){"vertex", index_of_house(house), (int)i, 0, 0});
    tl_json_object_set(json, "walls", tl_json_number((double)tl_outline_wall_count(house)));
    tl_json_object_set(json, "locked", tl_json_bool(tl_outline_any_locked(house)));
    tl_polygon_free(&outer);
}

#define ROOM_PROBE_MM 5.0 /* насколько за грань проёма искать комнату */

/* Стык: глубина проёма, мм до стыка у комнаты справа (по середине), наклон, комнаты. */
static void threshold_properties(TlProject *project, const TlOpening *opening, TlJson *json)
{
    TlVec2 right, left;
    if (!tl_project_opening_section(project, opening, &right, &left))
        return;
    TlVec2 across = tl_vec2_sub(left, right);
    double depth = tl_vec2_length(across);
    TlVec2 step = tl_vec2_scale(tl_vec2_normalized(across), ROOM_PROBE_MM);
    TlSpace *right_room = tl_project_space_at(project, tl_vec2_sub(right, step));
    TlSpace *left_room = tl_project_space_at(project, tl_vec2_add(left, step));
    bool outside = tl_project_opening_outside(project, opening);
    double middle = (opening->threshold + opening->threshold_end) / 2;
    tl_json_object_set(json, "depth", tl_json_number(round(depth)));
    tl_json_object_set(json, "right", tl_json_number(round(middle * depth)));
    tl_json_object_set(json, "skew", tl_json_number(round((opening->threshold_end - opening->threshold) * depth)));
    tl_json_object_set(json, "right_room", tl_json_string(right_room ? right_room->name : "комната"));
    tl_json_object_set(json, "left_room", tl_json_string(outside ? "" : left_room ? left_room->name : "комната"));
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
        add_partition_handles(project, handles, partition, index);
        tl_json_object_set(json, "thickness", tl_json_number(tl_partition_max_thickness(partition)));
        tl_json_object_set(json, "length", tl_json_number(round(tl_partition_length(partition))));
        tl_json_object_set(json, "angle", tl_json_number(round(tl_partition_angle(partition) * 10) / 10));
        tl_json_object_set(json, "locked", tl_json_bool(partition->locked));
    } else if (is(kind, "opening") && opening) {
        TlBand band;
        bool hosted = tl_project_opening_host(project, opening, &band);
        TlPolygon cut = {0};
        if (tl_project_opening_cut(project, opening, &cut))
            add_outline(outlines, &cut);
        tl_polygon_free(&cut);
        tl_json_object_set(json, "width", tl_json_number(opening->width));
        tl_json_object_set(json, "name", tl_json_string(opening->name));
        static const char *const KINDS[] = {"door", "window", "arch"};
        tl_json_object_set(json, "opening_kind", tl_json_string(KINDS[opening->kind]));
        tl_json_object_set(json, "height", tl_json_number(opening->height));
        tl_json_object_set(json, "sill", tl_json_number(opening->sill));
        tl_json_object_set(json, "reveal", tl_json_number(opening->reveal));
        double span_start, span_end;
        if (hosted && tl_project_opening_span(project, opening, &span_start, &span_end)) {
            double along;
            tl_band_local(&band, opening->center, &along, NULL);
            tl_json_object_set(json, "from_start",
                               tl_json_number(round((along - opening->width / 2 - span_start) * 10) / 10));
            tl_json_object_set(json, "to_end", tl_json_number(round((span_end - along - opening->width / 2) * 10) / 10));
        }
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
            tl_json_object_set(json, "outer_length", tl_json_number(round(tl_outline_outer_length(house, (size_t)part))));
            tl_json_object_set(json, "locked", tl_json_bool(house->locked.items[part]));
        } else if (is(kind, "vertex")) {
            tl_json_object_set(json, "x", tl_json_number(round(house->inner.items[part].x * 10) / 10));
            tl_json_object_set(json, "y", tl_json_number(round(house->inner.items[part].y * 10) / 10));
            tl_json_object_set(json, "locked", tl_json_bool(tl_outline_vertex_locked(house, (size_t)part)));
        }
    } else if (is(kind, "ruler") && index >= 0 && (size_t)index < project->rulers.count) {
        const TlRuler *ruler = project->rulers.items[index];
        tl_json_object_set(json, "name", tl_json_string(ruler->name));
        TlPolygon line = {0};
        tl_polygon_push(&line, ruler->start);
        tl_polygon_push(&line, ruler->end);
        add_outline(outlines, &line);
        tl_polygon_free(&line);
        add_handle(handles, ruler->start, "end", (Item){"ruler", index, 1, 0, 0});
        add_handle(handles, ruler->end, "end", (Item){"ruler", index, 2, 0, 0});
        add_middle(handles, ruler->start, ruler->end, (Item){"ruler", index, 0, 0, 0});
        tl_json_object_set(json, "length", tl_json_number(round(tl_ruler_length(ruler))));
        tl_json_object_set(json, "locked", tl_json_bool(false));
    } else if (is(kind, "joint") && index >= 0 && (size_t)index < project->expansion_joints.count) {
        const TlExpansionJoint *joint = project->expansion_joints.items[index];
        TlPolygon strip = tl_expansion_joint_strip(joint);
        add_outline(outlines, &strip);
        tl_polygon_free(&strip);
        add_handle(handles, joint->start, "end", (Item){"joint", index, 1, 0, 0});
        add_handle(handles, joint->end, "end", (Item){"joint", index, 2, 0, 0});
        add_middle(handles, joint->start, joint->end, (Item){"joint", index, 0, 0, 0});
        tl_json_object_set(json, "gap", tl_json_number(joint->gap));
        tl_json_object_set(json, "divider", tl_json_bool(joint->kind == TL_LINE_DIVIDER));
        tl_json_object_set(json, "length", tl_json_number(round(tl_expansion_joint_length(joint))));
        tl_json_object_set(json, "locked", tl_json_bool(false));
    } else if (is(kind, "drain") && index >= 0 && (size_t)index < project->drains.count) {
        const TlDrain *drain = &project->drains.items[index];
        TlPolygon outline = tl_drain_outline(drain);
        add_outline(outlines, &outline);
        tl_polygon_free(&outline);
        add_handle(handles, drain->center, "middle", (Item){"drain", index, 0, 0, 0});
        tl_json_object_set(json, "drain_kind", tl_json_string(tl_drain_kind_key(drain->kind)));
        tl_json_object_set(json, "name", tl_json_string(tl_drain_kind_name(drain->kind)));
        tl_json_object_set(json, "size", tl_json_number(drain->size));
        if (drain->kind == TL_DRAIN_LINEAR)
            tl_json_object_set(json, "length", tl_json_number(drain->length));
        else
            tl_json_object_set(json, "envelope", tl_json_bool(drain->envelope));
        tl_json_object_set(json, "angle", tl_json_number(round(drain->angle * 10) / 10));
        tl_json_object_set(json, "slope", tl_json_number(drain->slope));
        TlSpace *space = tl_project_space_at(project, drain->center);
        size_t region = space ? tl_space_region_at(space, drain->center) : SIZE_MAX;
        if (region != SIZE_MAX)
            tl_json_object_set(json, "drop", tl_json_number(round(tl_drain_drop(drain, &space->regions.items[region].outline))));
        tl_json_object_set(json, "locked", tl_json_bool(false));
    } else if (is(kind, "fixture") && index >= 0 && (size_t)index < project->fixtures.count) {
        const TlFixture *fixture = &project->fixtures.items[index];
        TlPolygon outline = tl_fixture_outline(fixture);
        add_outline(outlines, &outline);
        tl_polygon_free(&outline);
        add_handle(handles, fixture->center, "middle", (Item){"fixture", index, 0, 0, 0});
        tl_json_object_set(json, "fixture_kind", tl_json_string(tl_fixture_kind_key(fixture->kind)));
        tl_json_object_set(json, "name", tl_json_string(tl_fixture_kind_name(fixture->kind)));
        tl_json_object_set(json, "w", tl_json_number(fixture->width));
        tl_json_object_set(json, "d", tl_json_number(fixture->depth));
        tl_json_object_set(json, "h", tl_json_number(fixture->height));
        tl_json_object_set(json, "angle", tl_json_number(round(fixture->angle * 10) / 10));
        tl_json_object_set(json, "under", tl_json_bool(fixture->under));
        tl_json_object_set(json, "behind", tl_json_bool(fixture->behind));
        if (fixture->kind == TL_FIXTURE_STAIRS)
            tl_json_object_set(json, "steps", tl_json_number(fixture->steps));
        tl_json_object_set(json, "locked", tl_json_bool(false));
    } else if (is(kind, "pipe") && index >= 0 && (size_t)index < project->pipes.count) {
        const TlPipe *pipe = project->pipes.items[index];
        TlPolygon circle = {0}; /* для подсветки — круг коронки, но не меньше нескольких мм */
        double radius = fmax(pipe->diameter / 2, 10);
        for (int k = 0; k < 24; k++)
            tl_polygon_push(&circle, tl_vec2(pipe->center.x + radius * cos(k * TL_PI / 12),
                                             pipe->center.y + radius * sin(k * TL_PI / 12)));
        add_outline(outlines, &circle);
        tl_polygon_free(&circle);
        tl_json_object_set(json, "x", tl_json_number(round(pipe->center.x * 10) / 10));
        tl_json_object_set(json, "y", tl_json_number(round(pipe->center.y * 10) / 10));
        tl_json_object_set(json, "diameter", tl_json_number(pipe->diameter));
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
            threshold_properties(project, opening, json);
            add_handle(handles, from, "point", (Item){"threshold", index, TL_DRAG_THRESHOLD_FROM, 0, 0});
            add_handle(handles, to, "point", (Item){"threshold", index, TL_DRAG_THRESHOLD_TO, 0, 0});
            add_middle(handles, from, to, (Item){"threshold", index, TL_DRAG_THRESHOLD_LINE, 0, 0});
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
            add_node_handles(handles, &node, index, end);
            TlVec2 point = tl_node_point(&node, part);
            tl_json_object_set(json, "x", tl_json_number(round(point.x * 10) / 10));
            tl_json_object_set(json, "y", tl_json_number(round(point.y * 10) / 10));
            tl_json_object_set(json, "count", tl_json_number((double)node.count));
            tl_json_object_set(json, "locked", tl_json_bool(tl_node_locked(&node)));
        } else if (is(kind, "tee") && tl_project_tee_of(project, partition, (TlPartitionEnd)end, &tee)) {
            TlPolygon body = tl_project_partition_body(project, tee.branch, 0);
            add_outline(outlines, &body);
            tl_polygon_free(&body);
            add_handle(handles, tee.point, "point", (Item){"tee", index, 0, end, 0});
            tl_json_object_set(json, "x", tl_json_number(round(tee.point.x * 10) / 10));
            tl_json_object_set(json, "y", tl_json_number(round(tee.point.y * 10) / 10));
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

/* Правка перегородки как ведущей: узлы и примыкания следуют за ней. */
typedef void (*PartitionChange)(TlPartition *partition, double value);

/* own_ends — концы заданы правкой (длина, угол): не возвращать их на грани,
   к которым были привязаны (иначе перегородка съезжает вдоль стены). */
static bool change_partition(int index, double value, const char *merge_key, PartitionChange change, bool own_ends,
                             double now_seconds)
{
    TlPartition *partition = partition_at(index);
    if (!partition || partition->locked)
        return false;
    TlProject *project = tl_web_project();
    tl_web_checkpoint(merge_key, now_seconds);
    TlEditToken *token = tl_project_begin_edit(project, NULL);
    change(partition, value);
    TlEditOptions options = {.master = partition, .frozen = own_ends ? partition : NULL};
    tl_project_end_edit(project, token, &options);
    tl_project_merge_straight_nodes(project);
    tl_project_update_spaces(project);
    return true;
}

bool tl_web_set_partition_thickness(int index, double thickness, double now_seconds)
{
    return thickness >= 10 && thickness <= 1000 &&
           change_partition(index, thickness, "pthick", tl_partition_set_thickness, false, now_seconds);
}

bool tl_web_set_partition_length(int index, double length, double now_seconds)
{
    return length >= 10 && length <= 100000 &&
           change_partition(index, length, "plen", tl_partition_set_length, true, now_seconds);
}

bool tl_web_set_partition_angle(int index, double degrees, double now_seconds)
{
    return isfinite(degrees) && change_partition(index, degrees, "pangle", tl_partition_set_angle, true, now_seconds);
}

bool tl_web_set_opening_width(int index, double width, double now_seconds)
{
    TlOpening *opening = opening_at(index);
    if (!opening || opening->locked || !(width >= 100 && width <= 10000))
        return false;
    tl_web_checkpoint("owidth", now_seconds);
    TlProject *project = tl_web_project();
    tl_project_set_opening_width(project, opening, width);
    tl_project_merge_straight_nodes(project);
    tl_project_update_spaces(project);
    return true;
}

bool tl_web_set_opening(int index, const char *kind, double height, double sill, double now_seconds)
{
    TlOpening *opening = opening_at(index);
    static const char *const KINDS[] = {"door", "window", "arch"};
    int new_kind = -1;
    for (int k = 0; k < 3; k++)
        if (is(kind, KINDS[k]))
            new_kind = k;
    if (!opening || (kind[0] && new_kind < 0) || !(height > 0) || !(sill >= 0))
        return false;
    tl_web_checkpoint(new_kind >= 0 ? "" : "osize", now_seconds);
    if (new_kind >= 0 && new_kind != (int)opening->kind) {
        tl_project_set_opening_kind(tl_web_project(), opening, (TlOpeningKind)new_kind);
    } else {
        opening->height = height;
        opening->sill = sill;
    }
    tl_project_update_spaces(tl_web_project());
    return true;
}

bool tl_web_set_opening_reveal(int index, double depth, double now_seconds)
{
    TlOpening *opening = opening_at(index);
    if (!opening || !(depth >= 0 && depth <= 1000))
        return false;
    tl_web_checkpoint("oreveal", now_seconds);
    opening->reveal = depth;
    tl_project_update_spaces(tl_web_project());
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

const char *tl_web_set_point(const char *kind, int index, int part, int end, double x, double y, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlVec2 point = tl_vec2(x, y);
    TlPartition *partition = partition_at(index);
    TlOutline *house = house_at(index);
    if (is(kind, "vertex") && house && part >= 0) {
        tl_web_checkpoint("", now_seconds);
        if (!tl_project_set_vertex(project, house, (size_t)part, point))
            return "null";
        return item_json((Item){"vertex", index, part, 0, 0});
    }
    if (is(kind, "node") && partition) {
        TlNode node;
        if (!tl_project_node_of(project, partition, (TlPartitionEnd)end, &node))
            return "null";
        tl_web_checkpoint("", now_seconds);
        if (!tl_project_set_node_point(project, &node, part, point))
            return "null";
        return item_json((Item){"node", index_of_partition(project, partition), part, end, 0});
    }
    if (is(kind, "tee") && partition) {
        bool became_node = false;
        tl_web_checkpoint("", now_seconds);
        if (!tl_project_set_tee_point(project, partition, (TlPartitionEnd)end, point, &became_node))
            return "null";
        return item_json((Item){became_node ? "node" : "tee", index_of_partition(project, partition), 0, end, 0});
    }
    return "null";
}

bool tl_web_set_wall_length(int house_index, int wall, double length, bool outer, bool keep_angles, double now_seconds)
{
    TlOutline *house = house_at(house_index);
    if (!house || wall < 0 || (size_t)wall >= tl_outline_wall_count(house))
        return false;
    /* наружная: разница с внутренней при сохранении углов не меняется */
    double inner = outer ? length - (tl_outline_outer_length(house, (size_t)wall) - tl_outline_inner_length(house, (size_t)wall))
                         : length;
    if (inner <= 1)
        return false;
    tl_web_checkpoint(outer ? "len_outer" : "len_inner", now_seconds);
    return tl_project_set_wall_length(tl_web_project(), house, (size_t)wall, inner, keep_angles, true);
}

bool tl_web_place_opening(int index, double distance, bool from_end, double now_seconds)
{
    TlOpening *opening = opening_at(index);
    if (!opening || !(distance >= 0))
        return false;
    tl_web_checkpoint("opos", now_seconds);
    return tl_project_place_opening_at(tl_web_project(), opening, distance, from_end);
}

bool tl_web_set_threshold(int index, double right, double skew, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlOpening *opening = opening_at(index);
    TlVec2 a, b;
    if (!opening || !tl_project_opening_section(project, opening, &a, &b))
        return false;
    double depth = tl_distance(a, b);
    if (depth <= 0 || !isfinite(right) || !isfinite(skew))
        return false;
    tl_web_checkpoint("threshold", now_seconds);
    return tl_project_set_threshold(project, opening, right / depth, skew / depth);
}

bool tl_web_set_locked(const char *kind, int index, int part, bool locked, double now_seconds)
{
    TlPartition *partition = partition_at(index);
    TlOpening *opening = opening_at(index);
    TlOutline *house = house_at(index);
    bool *flag = is(kind, "partition") && partition ? &partition->locked
                 : is(kind, "opening") && opening   ? &opening->locked
                 : is(kind, "wall") && house && part >= 0 && (size_t)part < tl_outline_wall_count(house)
                     ? &house->locked.items[part]
                     : NULL;
    if (!flag)
        return false;
    tl_web_checkpoint("", now_seconds);
    *flag = locked;
    return true;
}

bool tl_web_insert_vertex(int house_index, int wall, double x, double y, double grid, double now_seconds)
{
    TlOutline *house = house_at(house_index);
    if (!house || wall < 0 || (size_t)wall >= tl_outline_wall_count(house) || house->locked.items[wall])
        return false;
    TlVec2 a = tl_outline_wall_start(house, (size_t)wall), b = tl_outline_wall_end(house, (size_t)wall);
    double t;
    tl_point_segment_distance(tl_vec2(x, y), a, b, &t);
    TlVec2 on_grid = tl_snap_grid(grid, tl_vec2_lerp(a, b, t));
    tl_point_segment_distance(on_grid, a, b, &t); /* точка должна остаться на стене */
    if (!(t > 0 && t < 1))
        return false;
    tl_web_checkpoint("", now_seconds);
    tl_outline_insert_vertex(house, (size_t)wall, tl_vec2_lerp(a, b, t));
    tl_project_update_spaces(tl_web_project());
    return true;
}

int tl_web_add_house(double width, double height, double thickness, double now_seconds)
{
    TlProject *project = tl_web_project();
    if (!(width >= 100 && height >= 100 && thickness >= 0 && thickness <= 2000))
        return 0;
    double x = 0; /* справа от существующих, через 1,5 м */
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlPolygon outer = tl_outline_outer(project->outlines.items[o]);
        for (size_t i = 0; i < outer.count; i++)
            x = fmax(x, outer.items[i].x + 1500 + thickness);
        tl_polygon_free(&outer);
    }
    const TlVec2 corners[] = {{x, 0}, {x + width, 0}, {x + width, height}, {x, height}};
    tl_web_checkpoint("", now_seconds);
    tl_project_add_contour(project, corners, 4, thickness, 100);
    TlSpace *space = tl_project_space_at(project, tl_vec2(x + width / 2, height / 2));
    return space ? space->id : 0;
}

bool tl_web_set_name(const char *kind, int index, const char *name, double now_seconds)
{
    TlProject *project = tl_web_project();
    char *target = is(kind, "ruler") && index >= 0 && (size_t)index < project->rulers.count
                       ? project->rulers.items[index]->name
                   : is(kind, "opening") && index >= 0 && (size_t)index < project->openings.count
                       ? project->openings.items[index]->name
                       : NULL;
    if (!target || !name[0] || strlen(name) >= TL_NAME_LENGTH)
        return false;
    tl_web_checkpoint("name", now_seconds);
    snprintf(target, TL_NAME_LENGTH, "%s", name);
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
    } else if (is(kind, "drain") && index >= 0 && (size_t)index < project->drains.count) {
        tl_web_checkpoint("", now_seconds);
        tl_drain_list_remove_at(&project->drains, (size_t)index);
    } else if (is(kind, "fixture") && index >= 0 && (size_t)index < project->fixtures.count) {
        tl_web_checkpoint("", now_seconds);
        tl_fixture_list_remove_at(&project->fixtures, (size_t)index);
    } else if (is(kind, "pipe") && index >= 0 && (size_t)index < project->pipes.count) {
        tl_web_checkpoint("", now_seconds);
        tl_project_delete_pipe(project, project->pipes.items[index]);
    } else if (is(kind, "node") && partition) {
        TlNode node;
        if (!tl_project_node_of(project, partition, (TlPartitionEnd)end, &node) || tl_node_locked(&node))
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
            target.index = part; /* 0 — середина (узел целиком), 1… — угол узла */
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
    } else if (is(kind, "pipe") && index >= 0 && (size_t)index < project->pipes.count) {
        target.kind = TL_DRAG_PIPE;
        target.pipe = project->pipes.items[index];
    } else if (is(kind, "drain") && index >= 0 && (size_t)index < project->drains.count) {
        target.kind = TL_DRAG_DRAIN;
        target.index = index;
    } else if (is(kind, "fixture") && index >= 0 && (size_t)index < project->fixtures.count) {
        target.kind = TL_DRAG_FIXTURE;
        target.index = index;
    } else if (is(kind, "panno")) { /* панно на полу: комната — под точкой */
        TlSpace *space = tl_project_space_at(project, tl_vec2(x, y));
        if (!space)
            return false;
        target.kind = TL_DRAG_PANNO;
        target.space = space;
        target.index = index;
        dragging.space = space->id;
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
    case TL_DRAG_PANNO: return (Item){"panno", t->index, 0, 0, dragging.space};
    case TL_DRAG_FIXTURE: return (Item){"fixture", t->index, 0, 0, 0};
    case TL_DRAG_DRAIN: return (Item){"drain", t->index, 0, 0, 0};
    case TL_DRAG_PIPE: {
        int index = -1;
        for (size_t i = 0; i < project->pipes.count; i++)
            if (project->pipes.items[i] == t->pipe)
                index = (int)i;
        return (Item){"pipe", index, 0, 0, 0};
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

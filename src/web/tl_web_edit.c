/*
 * Правка плана из браузера: притягивание курсора, контур (дом или комната),
 * перегородка, проём, выбор предмета под курсором, его свойства и удаление.
 * Геометрия — в ядре (tl_project_commands.c, tl_snap.c), как у программы.
 */
#include <math.h>
#include <string.h>

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

/* ---- выбор -------------------------------------------------------------------- */

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

static bool on_house_walls(const TlOutline *outline, TlVec2 point)
{
    TlPolygon outer = tl_outline_outer(outline);
    bool inside = tl_polygon_contains(&outer, point) && !tl_polygon_contains(&outline->inner, point);
    tl_polygon_free(&outer);
    return inside;
}

static const char *picked(const char *kind, int index, int space)
{
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "kind", tl_json_string(kind));
    tl_json_object_set(json, "index", tl_json_number(index));
    tl_json_object_set(json, "space", tl_json_number(space));
    return keep(json);
}

const char *tl_web_pick(double x, double y, double tolerance)
{
    TlProject *project = tl_web_project();
    TlVec2 point = tl_vec2(x, y);
    for (size_t i = 0; i < project->openings.count; i++)
        if (on_opening(project, project->openings.items[i], point, tolerance))
            return picked("opening", (int)i, 0);
    for (size_t i = 0; i < project->partitions.count; i++) {
        const TlPartition *partition = project->partitions.items[i];
        if (tl_point_segment_distance(point, partition->start, partition->end, NULL) <=
            tl_partition_max_thickness(partition) / 2 + tolerance)
            return picked("partition", (int)i, 0);
    }
    for (size_t i = 0; i < project->outlines.count; i++)
        if (on_house_walls(project->outlines.items[i], point))
            return picked("house", (int)i, 0);
    TlSpace *space = tl_project_space_at(project, point);
    return space ? picked("space", 0, space->id) : picked("none", 0, 0);
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

const char *tl_web_object(const char *kind, int index)
{
    TlProject *project = tl_web_project();
    TlJson *json = tl_json_object();
    tl_json_object_set(json, "kind", tl_json_string(kind));
    tl_json_object_set(json, "index", tl_json_number(index));
    TlPartition *partition = strcmp(kind, "partition") == 0 ? partition_at(index) : NULL;
    TlOpening *opening = strcmp(kind, "opening") == 0 ? opening_at(index) : NULL;
    TlOutline *house = strcmp(kind, "house") == 0 ? house_at(index) : NULL;
    if (partition) {
        tl_json_object_set(json, "thickness", tl_json_number(tl_partition_max_thickness(partition)));
        tl_json_object_set(json, "length", tl_json_number(round(tl_partition_length(partition))));
        TlPolygon body = tl_project_partition_body(project, partition, 0);
        tl_json_object_set(json, "body", tl_web_polygon(&body));
        tl_polygon_free(&body);
        tl_json_object_set(json, "locked", tl_json_bool(partition->locked));
    } else if (opening) {
        TlBand band;
        bool hosted = tl_project_opening_host(project, opening, &band);
        tl_json_object_set(json, "width", tl_json_number(opening->width));
        tl_json_object_set(json, "host", tl_json_string(!hosted ? "none"
                                                        : band.host.kind == TL_HOST_PARTITION ? "partition"
                                                                                              : "wall"));
        TlPolygon cut = {0};
        tl_json_object_set(json, "cut", tl_project_opening_cut(project, opening, &cut) ? tl_web_polygon(&cut)
                                                                                         : tl_json_null());
        tl_polygon_free(&cut);
        tl_json_object_set(json, "locked", tl_json_bool(opening->locked));
    } else if (house) {
        TlPolygon outer = tl_outline_outer(house);
        tl_json_object_set(json, "walls", tl_json_number((double)tl_outline_wall_count(house)));
        tl_json_object_set(json, "thickness", tl_json_number(house->thickness.items[0]));
        tl_json_object_set(json, "outer", tl_web_polygon(&outer));
        tl_json_object_set(json, "inner", tl_web_polygon(&house->inner));
        tl_json_object_set(json, "locked", tl_json_bool(tl_outline_any_locked(house)));
        tl_polygon_free(&outer);
    } else {
        tl_json_free(json);
        return "null";
    }
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

bool tl_web_delete(const char *kind, int index, double now_seconds)
{
    TlProject *project = tl_web_project();
    TlPartition *partition = strcmp(kind, "partition") == 0 ? partition_at(index) : NULL;
    TlOpening *opening = strcmp(kind, "opening") == 0 ? opening_at(index) : NULL;
    TlOutline *house = strcmp(kind, "house") == 0 ? house_at(index) : NULL;
    if ((partition && partition->locked) || (opening && opening->locked) || (house && tl_outline_any_locked(house)) ||
        !(partition || opening || house))
        return false;
    tl_web_checkpoint("", now_seconds);
    if (partition) {
        tl_project_delete_partition(project, partition);
        tl_project_remove_orphan_openings(project);
    } else if (opening) {
        tl_project_delete_opening(project, opening);
    } else {
        tl_project_delete_outline(project, house);
    }
    tl_project_update_spaces(project);
    return true;
}

#include "tl_regions.h"

#include <math.h>
#include <stdlib.h>

#define MERGE_EPSILON 1e-6  /* точки ближе этого — одна вершина, мм */
#define PROBE_OFFSET 1e-3   /* насколько внутрь грани отступает пробная точка, мм */

typedef struct Segment {
    TlVec2 start, end;
} Segment;

typedef struct EdgeKey {
    int low, high;
} EdgeKey;

typedef struct HalfEdge {
    int from, to;
    double angle;
    int position;  /* индекс в списке исходящих рёбер вершины from */
    bool visited;
    int cycle;     /* какой цикл (грань или внешний контур) обходит полуребро */
} HalfEdge;

TL_ARRAY_DEFINE(SegmentArray, Segment, segment_array)
TL_ARRAY_DEFINE(EdgeKeyArray, EdgeKey, edge_key_array)
TL_ARRAY_DEFINE(HalfEdgeArray, HalfEdge, half_edge_array)

/* Планарный граф: вершины и полурёбра (2k и 2k+1 — пара встречных). */
typedef struct Graph {
    TlPolygon vertices;
    HalfEdgeArray half_edges;
    TlIntArray *outgoing;  /* по вершине: полурёбра, упорядоченные по углу */
    int *component;        /* по вершине: номер связной компоненты */
} Graph;

typedef struct Cycle {
    TlPolygon points;
    double signed_area;
    int component;
} Cycle;

TL_ARRAY_DEFINE(CycleArray, Cycle, cycle_array)

/* ---- построение графа ------------------------------------------------- */

static int vertex_id(TlPolygon *vertices, TlVec2 point)
{
    for (size_t i = 0; i < vertices->count; i++)
        if (tl_distance(vertices->items[i], point) < MERGE_EPSILON)
            return (int)i;
    tl_polygon_push(vertices, point);
    return (int)vertices->count - 1;
}

static SegmentArray collect_segments(const TlPolygon *polygons, size_t polygon_count)
{
    SegmentArray segments = {0};
    for (size_t p = 0; p < polygon_count; p++) {
        const TlPolygon *polygon = &polygons[p];
        if (polygon->count < 3)
            continue;
        for (size_t i = 0; i < polygon->count; i++) {
            Segment segment = {tl_polygon_edge_start(polygon, i), tl_polygon_edge_end(polygon, i)};
            if (tl_distance(segment.start, segment.end) > MERGE_EPSILON)
                segment_array_push(&segments, segment);
        }
    }
    return segments;
}

/* Параметры t на отрезке base, где его касаются или пересекают другие отрезки. */
static void add_split_parameters(const Segment *base, const Segment *other, TlDoubleArray *params)
{
    TlVec2 direction = tl_vec2_sub(base->end, base->start);
    const TlVec2 other_ends[2] = {other->start, other->end};
    for (int k = 0; k < 2; k++) {
        double t;
        if (tl_point_segment_distance(other_ends[k], base->start, base->end, &t) < MERGE_EPSILON)
            tl_double_array_push(params, t);
    }
    TlVec2 other_direction = tl_vec2_sub(other->end, other->start);
    double denominator = tl_vec2_cross(direction, other_direction);
    if (fabs(denominator) < 1e-12)
        return;
    TlVec2 offset = tl_vec2_sub(other->start, base->start);
    double t = tl_vec2_cross(offset, other_direction) / denominator;
    double u = tl_vec2_cross(offset, direction) / denominator;
    if (t > 0 && t < 1 && u >= 0 && u <= 1)
        tl_double_array_push(params, t);
}

static int compare_doubles(const void *left, const void *right)
{
    double a = *(const double *)left, b = *(const double *)right;
    return a < b ? -1 : a > b;
}

static int compare_edge_keys(const void *left, const void *right)
{
    const EdgeKey *a = left, *b = right;
    if (a->low != b->low)
        return a->low < b->low ? -1 : 1;
    return a->high < b->high ? -1 : a->high > b->high;
}

/* Разбивает все отрезки в точках пересечения; возвращает уникальные рёбра. */
static EdgeKeyArray split_into_edges(const SegmentArray *segments, TlPolygon *vertices)
{
    EdgeKeyArray edges = {0};
    for (size_t i = 0; i < segments->count; i++) {
        const Segment *segment = &segments->items[i];
        TlDoubleArray params = {0};
        tl_double_array_push(&params, 0.0);
        tl_double_array_push(&params, 1.0);
        for (size_t j = 0; j < segments->count; j++)
            if (j != i)
                add_split_parameters(segment, &segments->items[j], &params);
        qsort(params.items, params.count, sizeof(double), compare_doubles);

        int previous = -1;
        for (size_t k = 0; k < params.count; k++) {
            TlVec2 point = params.items[k] <= 0.0   ? segment->start
                           : params.items[k] >= 1.0 ? segment->end
                                                    : tl_vec2_lerp(segment->start, segment->end, params.items[k]);
            int current = vertex_id(vertices, point);
            if (previous >= 0 && current != previous) {
                EdgeKey key = {previous < current ? previous : current,
                               previous < current ? current : previous};
                edge_key_array_push(&edges, key);
            }
            previous = current;
        }
        tl_double_array_free(&params);
    }
    qsort(edges.items, edges.count, sizeof(EdgeKey), compare_edge_keys);
    size_t unique = 0;
    for (size_t i = 0; i < edges.count; i++)
        if (unique == 0 || compare_edge_keys(&edges.items[i], &edges.items[unique - 1]) != 0)
            edges.items[unique++] = edges.items[i];
    edges.count = unique;
    return edges;
}

static const HalfEdgeArray *sorting_half_edges; /* для qsort: углы полурёбер */

static int compare_by_angle(const void *left, const void *right)
{
    double a = sorting_half_edges->items[*(const int *)left].angle;
    double b = sorting_half_edges->items[*(const int *)right].angle;
    return a < b ? -1 : a > b;
}

static int find_root(int *parent, int v)
{
    while (parent[v] != v)
        v = parent[v] = parent[parent[v]];
    return v;
}

static Graph build_graph_from_segments(const SegmentArray *segments)
{
    Graph graph = {0};
    EdgeKeyArray edges = split_into_edges(segments, &graph.vertices);

    size_t vertex_count = graph.vertices.count;
    graph.outgoing = tl_calloc(vertex_count ? vertex_count : 1, sizeof(TlIntArray));
    graph.component = tl_malloc((vertex_count ? vertex_count : 1) * sizeof(int));
    for (size_t v = 0; v < vertex_count; v++)
        graph.component[v] = (int)v;

    for (size_t k = 0; k < edges.count; k++) {
        int ends[2] = {edges.items[k].low, edges.items[k].high};
        for (int side = 0; side < 2; side++) {
            TlVec2 from = graph.vertices.items[ends[side]], to = graph.vertices.items[ends[1 - side]];
            HalfEdge half = {ends[side], ends[1 - side], atan2(to.y - from.y, to.x - from.x), 0, false, -1};
            half_edge_array_push(&graph.half_edges, half);
            tl_int_array_push(&graph.outgoing[ends[side]], (int)graph.half_edges.count - 1);
        }
        int a = find_root(graph.component, ends[0]), b = find_root(graph.component, ends[1]);
        graph.component[a] = b;
    }
    edge_key_array_free(&edges);

    sorting_half_edges = &graph.half_edges;
    for (size_t v = 0; v < vertex_count; v++) {
        TlIntArray *list = &graph.outgoing[v];
        qsort(list->items, list->count, sizeof(int), compare_by_angle);
        for (size_t i = 0; i < list->count; i++)
            graph.half_edges.items[list->items[i]].position = (int)i;
    }
    for (size_t v = 0; v < vertex_count; v++)
        graph.component[v] = find_root(graph.component, (int)v);
    return graph;
}

static Graph build_graph(const TlPolygon *polygons, size_t polygon_count)
{
    SegmentArray segments = collect_segments(polygons, polygon_count);
    Graph graph = build_graph_from_segments(&segments);
    segment_array_free(&segments);
    return graph;
}

static void free_graph(Graph *graph)
{
    for (size_t v = 0; v < graph->vertices.count; v++)
        tl_int_array_free(&graph->outgoing[v]);
    free(graph->outgoing);
    free(graph->component);
    tl_polygon_free(&graph->vertices);
    half_edge_array_free(&graph->half_edges);
}

/* Следующее полуребро той же грани: у конца ребра — предыдущее по углу
 * после встречного полуребра. */
static int next_half_edge(const Graph *graph, int half)
{
    int twin = half ^ 1;
    const HalfEdge *back = &graph->half_edges.items[twin];
    const TlIntArray *around = &graph->outgoing[back->from];
    int count = (int)around->count;
    return around->items[(back->position - 1 + count) % count];
}

static CycleArray trace_cycles(Graph *graph)
{
    CycleArray cycles = {0};
    for (size_t start = 0; start < graph->half_edges.count; start++) {
        if (graph->half_edges.items[start].visited)
            continue;
        Cycle cycle = {0};
        int half = (int)start;
        while (!graph->half_edges.items[half].visited) {
            graph->half_edges.items[half].visited = true;
            graph->half_edges.items[half].cycle = (int)cycles.count;
            tl_polygon_push(&cycle.points, graph->vertices.items[graph->half_edges.items[half].from]);
            half = next_half_edge(graph, half);
        }
        cycle.signed_area = tl_polygon_signed_area(&cycle.points);
        cycle.component = graph->component[graph->half_edges.items[start].from];
        cycle_array_push(&cycles, cycle);
    }
    return cycles;
}

/* Точка чуть внутри грани: у середины самой длинной стороны. */
static TlVec2 probe_point(const TlPolygon *face)
{
    size_t longest = 0;
    for (size_t i = 1; i < face->count; i++)
        if (tl_polygon_edge_length(face, i) > tl_polygon_edge_length(face, longest))
            longest = i;
    TlVec2 normal;
    tl_polygon_edge_frame(face, longest, NULL, &normal);
    TlVec2 middle = tl_vec2_midpoint(tl_polygon_edge_start(face, longest), tl_polygon_edge_end(face, longest));
    return tl_vec2_sub(middle, tl_vec2_scale(normal, PROBE_OFFSET));
}

static bool is_face(const Cycle *cycle) { return cycle->signed_area > 1e-9; }

/* Самая маленькая грань другой компоненты, внутри которой лежит контур. */
static long containing_face(const CycleArray *cycles, const Cycle *hole)
{
    long best = -1;
    for (size_t i = 0; i < cycles->count; i++) {
        const Cycle *face = &cycles->items[i];
        if (!is_face(face) || face->component == hole->component)
            continue;
        if (!tl_polygon_contains(&face->points, hole->points.items[0]))
            continue;
        if (best < 0 || face->signed_area < cycles->items[best].signed_area)
            best = (long)i;
    }
    return best;
}

/* Решение предиката для каждой грани; у внешних контуров — false. */
static bool *keep_faces(const CycleArray *cycles, const TlPolygon *polygons, size_t polygon_count,
                        TlRegionPredicate keep, void *context)
{
    bool *kept = tl_calloc(cycles->count ? cycles->count : 1, sizeof(bool));
    bool *inside = tl_malloc((polygon_count ? polygon_count : 1) * sizeof(bool));
    for (size_t i = 0; i < cycles->count; i++) {
        if (!is_face(&cycles->items[i]))
            continue;
        TlVec2 probe = probe_point(&cycles->items[i].points);
        for (size_t p = 0; p < polygon_count; p++)
            inside[p] = tl_polygon_contains(&polygons[p], probe);
        kept[i] = keep(inside, polygon_count, context);
    }
    free(inside);
    return kept;
}

/* Оставленные грани — области; внешние контуры других компонент — их дыры. */
static TlRegionList collect_regions(const CycleArray *cycles, const bool *kept)
{
    TlRegionList regions = {0};
    long *region_of_cycle = tl_malloc((cycles->count ? cycles->count : 1) * sizeof(long));
    for (size_t i = 0; i < cycles->count; i++) {
        region_of_cycle[i] = -1;
        if (!kept[i])
            continue;
        TlRegion region = {tl_polygon_copy(&cycles->items[i].points), {0}};
        tl_region_list_push(&regions, region);
        region_of_cycle[i] = (long)regions.count - 1;
    }
    for (size_t i = 0; i < cycles->count; i++) {
        if (is_face(&cycles->items[i]) || cycles->items[i].points.count < 3)
            continue;
        long face = containing_face(cycles, &cycles->items[i]);
        if (face >= 0 && region_of_cycle[face] >= 0)
            tl_polygon_list_push(&regions.items[region_of_cycle[face]].holes,
                                 tl_polygon_copy(&cycles->items[i].points));
    }
    free(region_of_cycle);
    return regions;
}

static void free_cycles(CycleArray *cycles)
{
    for (size_t i = 0; i < cycles->count; i++)
        tl_polygon_free(&cycles->items[i].points);
    cycle_array_free(cycles);
}

static TlRegionList regions_of_graph(Graph *graph, const TlPolygon *polygons, size_t polygon_count,
                                     TlRegionPredicate keep, void *context)
{
    CycleArray cycles = trace_cycles(graph);
    bool *kept = keep_faces(&cycles, polygons, polygon_count, keep, context);
    TlRegionList regions = collect_regions(&cycles, kept);
    free(kept);
    free_cycles(&cycles);
    return regions;
}

TlRegionList tl_regions_build(const TlPolygon *polygons, size_t polygon_count,
                              TlRegionPredicate keep, void *context)
{
    Graph graph = build_graph(polygons, polygon_count);
    TlRegionList regions = regions_of_graph(&graph, polygons, polygon_count, keep, context);
    free_graph(&graph);
    return regions;
}

/* Оставлено ли то, что по эту сторону цикла: грань — по предикату; внешний
   контур компоненты — по грани, в которой он лежит (снаружи всего — нет). */
static bool *kept_sides(const CycleArray *cycles, const bool *kept)
{
    bool *sides = tl_calloc(cycles->count ? cycles->count : 1, sizeof(bool));
    for (size_t i = 0; i < cycles->count; i++) {
        if (is_face(&cycles->items[i])) {
            sides[i] = kept[i];
            continue;
        }
        long outer = containing_face(cycles, &cycles->items[i]);
        sides[i] = outer >= 0 && kept[outer];
    }
    return sides;
}

/* Рёбра между оставленной и выброшенной частью: внутренние рёбра оставленных
   областей пропадают — соседние грани сливаются. */
TlRegionList tl_regions_build_merged(const TlPolygon *polygons, size_t polygon_count,
                                     TlRegionPredicate keep, void *context)
{
    Graph graph = build_graph(polygons, polygon_count);
    CycleArray cycles = trace_cycles(&graph);
    bool *kept = keep_faces(&cycles, polygons, polygon_count, keep, context);
    bool *sides = kept_sides(&cycles, kept);
    SegmentArray boundary = {0};
    for (size_t h = 0; h + 1 < graph.half_edges.count; h += 2) {
        const HalfEdge *half = &graph.half_edges.items[h], *twin = &graph.half_edges.items[h + 1];
        if (sides[half->cycle] != sides[twin->cycle])
            segment_array_push(&boundary,
                               (Segment){graph.vertices.items[half->from], graph.vertices.items[half->to]});
    }
    free(sides);
    free(kept);
    free_cycles(&cycles);
    free_graph(&graph);

    Graph merged = build_graph_from_segments(&boundary);
    segment_array_free(&boundary);
    TlRegionList regions = regions_of_graph(&merged, polygons, polygon_count, keep, context);
    free_graph(&merged);
    return regions;
}

/* ---- готовые операции --------------------------------------------------- */

static bool keep_difference(const bool *inside, size_t polygon_count, void *context)
{
    if (!inside[0])
        return false;
    for (size_t i = 1; i < polygon_count; i++)
        if (inside[i])
            return false;
    return true;
}

static bool keep_intersection(const bool *inside, size_t polygon_count, void *context)
{
    return polygon_count == 2 && inside[0] && inside[1];
}

TlRegionList tl_regions_difference(const TlPolygon *base, const TlPolygon *cutters,
                                   size_t cutter_count)
{
    TlPolygon *all = tl_malloc((cutter_count + 1) * sizeof(TlPolygon));
    all[0] = *base;
    for (size_t i = 0; i < cutter_count; i++)
        all[i + 1] = cutters[i];
    TlRegionList regions = tl_regions_build(all, cutter_count + 1, keep_difference, NULL);
    free(all);
    return regions;
}

double tl_polygons_overlap_area(const TlPolygon *a, const TlPolygon *b)
{
    if (!tl_rect_intersects(tl_polygon_bounds(a), tl_polygon_bounds(b)))
        return 0.0;
    TlPolygon pair[2] = {*a, *b};
    TlRegionList regions = tl_regions_build(pair, 2, keep_intersection, NULL);
    double area = 0.0;
    for (size_t i = 0; i < regions.count; i++)
        area += tl_region_area(&regions.items[i]);
    tl_region_list_free_all(&regions);
    return area;
}

/* ---- области -------------------------------------------------------------- */

double tl_region_area(const TlRegion *region)
{
    double area = tl_polygon_area(&region->outline);
    for (size_t i = 0; i < region->holes.count; i++)
        area -= tl_polygon_area(&region->holes.items[i]);
    return area;
}

bool tl_region_contains(const TlRegion *region, TlVec2 point)
{
    if (!tl_polygon_contains(&region->outline, point))
        return false;
    for (size_t i = 0; i < region->holes.count; i++)
        if (tl_polygon_contains(&region->holes.items[i], point))
            return false;
    return true;
}

void tl_region_free(TlRegion *region)
{
    tl_polygon_free(&region->outline);
    tl_polygon_list_free_all(&region->holes);
}

void tl_region_list_free_all(TlRegionList *regions)
{
    for (size_t i = 0; i < regions->count; i++)
        tl_region_free(&regions->items[i]);
    tl_region_list_free(regions);
}

static bool inside_slab_not_holes(const bool *inside, size_t count, void *context)
{
    (void)context;
    if (!inside[0] || !inside[1])
        return false;
    for (size_t i = 2; i < count; i++)
        if (inside[i])
            return false;
    return true;
}

TlPolygonList tl_region_pieces(const TlRegion *region)
{
    TlPolygonList pieces = {0};
    if (region->holes.count == 0) {
        tl_polygon_list_push(&pieces, tl_polygon_copy(&region->outline));
        return pieces;
    }
    TlRect bounds = tl_polygon_bounds(&region->outline);
    TlDoubleArray cuts = {0}; /* полосы между вертикалями через середины дыр: дыра режется пополам */
    tl_double_array_push(&cuts, bounds.left - 1);
    for (size_t h = 0; h < region->holes.count; h++) {
        TlRect hole = tl_polygon_bounds(&region->holes.items[h]);
        tl_double_array_push(&cuts, (hole.left + hole.right) / 2);
    }
    tl_double_array_push(&cuts, bounds.right + 1);
    qsort(cuts.items, cuts.count, sizeof(double), compare_doubles);
    for (size_t i = 0; i + 1 < cuts.count; i++) {
        if (cuts.items[i + 1] - cuts.items[i] < 0.01)
            continue;
        TlPolygonList polygons = {0};
        TlRect slab = {cuts.items[i], bounds.top - 1, cuts.items[i + 1], bounds.bottom + 1};
        tl_polygon_list_push(&polygons, tl_polygon_from_rect(slab));
        tl_polygon_list_push(&polygons, tl_polygon_copy(&region->outline));
        for (size_t h = 0; h < region->holes.count; h++)
            tl_polygon_list_push(&polygons, tl_polygon_copy(&region->holes.items[h]));
        TlRegionList parts = tl_regions_build_merged(polygons.items, polygons.count, inside_slab_not_holes, NULL);
        for (size_t k = 0; k < parts.count; k++) /* дыры разрезаны — у кусков их нет */
            tl_polygon_list_push(&pieces, tl_polygon_copy(&parts.items[k].outline));
        tl_region_list_free_all(&parts);
        tl_polygon_list_free_all(&polygons);
    }
    tl_double_array_free(&cuts);
    return pieces;
}

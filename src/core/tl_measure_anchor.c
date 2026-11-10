#include "tl_measure_anchor.h"

#include <math.h>

#include "tl_drag.h"

#define ON_CONTOUR_MM 0.5 /* конец ближе этого к углу или ребру — примагничен к нему */

/* Контуры, к которым примагничиваются концы (как в tl_measure_snap): у каждого — вид и владелец. */
typedef enum ContourKind {
    HOUSE_INNER,
    HOUSE_OUTER,
    HOUSE_AXIS,
    PARTITION_BODY,
    PARTITION_AXIS, /* отрезок: два угла, одно ребро */
    OPENING_CUT,
    OPENING_JAMBS,  /* только точки: углы косяков на гранях стены */
    THRESHOLD,      /* стык полов в проёме — отрезок; владелец — проём */
    LINE,           /* деформационный шов или линия раздела — отрезок */
    PIPE_POINTS,    /* только точки: центр, верх, низ, лево, право */
    FIXTURE_OUTLINE,
    DRAIN_CENTER,   /* только точка */
    DRAIN_OUTLINE
} ContourKind;

typedef struct Contour {
    ContourKind kind;
    const void *owner;
    TlPolygon points;
} Contour;

TL_ARRAY_DEFINE(ContourList, Contour, contour_list)

static void add_contour(ContourList *list, ContourKind kind, const void *owner, TlPolygon points)
{
    contour_list_push(list, (Contour){kind, owner, points});
}

static TlPolygon segment(TlVec2 start, TlVec2 end)
{
    TlPolygon points = {0};
    tl_polygon_push(&points, start);
    tl_polygon_push(&points, end);
    return points;
}

/* Швы, стыки в проёмах, трубы, предметы и трапы. */
static void add_objects(ContourList *list, TlProject *project)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        TlVec2 from, to;
        if (tl_project_opening_threshold(project, project->openings.items[i], &from, &to))
            add_contour(list, THRESHOLD, project->openings.items[i], segment(from, to));
    }
    for (size_t i = 0; i < project->expansion_joints.count; i++) {
        const TlExpansionJoint *line = project->expansion_joints.items[i];
        add_contour(list, LINE, line, segment(line->start, line->end));
    }
    for (size_t i = 0; i < project->pipes.count; i++) {
        TlVec2 points[TL_PIPE_POINT_COUNT];
        tl_pipe_points(project->pipes.items[i], points);
        TlPolygon contour = {0};
        for (size_t p = 0; p < TL_PIPE_POINT_COUNT; p++)
            tl_polygon_push(&contour, points[p]);
        add_contour(list, PIPE_POINTS, project->pipes.items[i], contour);
    }
    for (size_t i = 0; i < project->fixtures.count; i++)
        add_contour(list, FIXTURE_OUTLINE, &project->fixtures.items[i], tl_fixture_outline(&project->fixtures.items[i]));
    for (size_t i = 0; i < project->drains.count; i++) {
        TlPolygon center = {0};
        tl_polygon_push(&center, project->drains.items[i].center);
        add_contour(list, DRAIN_CENTER, &project->drains.items[i], center);
        add_contour(list, DRAIN_OUTLINE, &project->drains.items[i], tl_drain_outline(&project->drains.items[i]));
    }
}

static ContourList contours_of(TlProject *project)
{
    ContourList list = {0};
    add_objects(&list, project); /* сначала предметы: середина стороны тумбы у стены — за тумбу, не за стену */
    for (size_t i = 0; i < project->outlines.count; i++) {
        TlOutline *outline = project->outlines.items[i];
        add_contour(&list, HOUSE_INNER, outline, tl_polygon_copy(&outline->inner));
        add_contour(&list, HOUSE_OUTER, outline, tl_outline_outer(outline));
        add_contour(&list, HOUSE_AXIS, outline, tl_outline_axis(outline));
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        add_contour(&list, PARTITION_BODY, partition, tl_project_partition_body(project, partition, 0));
        add_contour(&list, PARTITION_AXIS, partition, segment(partition->start, partition->end));
    }
    for (size_t i = 0; i < project->openings.count; i++) {
        TlVec2 from[2], to[2];
        if (tl_project_opening_jambs(project, project->openings.items[i], from, to)) {
            TlPolygon jambs = segment(from[0], from[1]);
            tl_polygon_push(&jambs, to[0]);
            tl_polygon_push(&jambs, to[1]);
            add_contour(&list, OPENING_JAMBS, project->openings.items[i], jambs);
        }
        TlPolygon cut;
        if (tl_project_opening_cut(project, project->openings.items[i], &cut))
            add_contour(&list, OPENING_CUT, project->openings.items[i], cut);
    }
    return list;
}

static void free_contours(ContourList *list)
{
    for (size_t i = 0; i < list->count; i++)
        tl_polygon_free(&list->items[i].points);
    contour_list_free(list);
}

/* Рёбер у контура: у отрезка — одно, у точек — ни одного, у многоугольника — сколько углов. */
static size_t edge_count(const Contour *contour)
{
    switch (contour->kind) {
    case PARTITION_AXIS:
    case THRESHOLD:
    case LINE: return 1;
    case PIPE_POINTS:
    case OPENING_JAMBS:
    case DRAIN_CENTER: return 0;
    default: return contour->points.count;
    }
}

static TlVec2 edge_point(const Contour *contour, size_t edge, double along)
{
    const TlPolygon *p = &contour->points;
    return tl_vec2_lerp(p->items[edge], p->items[(edge + 1) % p->count], along);
}

typedef struct Anchor {
    TlVec2 *point;      /* конец линейки или шва */
    ContourKind kind;
    const void *owner;
    size_t corners;     /* углов контура, когда запоминали */
    size_t edge;
    double along;       /* доля вдоль ребра: 0 — угол */
    bool on_edge;       /* на ребре, а не в углу — может скользить по нему */
} Anchor;

TL_ARRAY_DEFINE(AnchorList, Anchor, anchor_list)

/* Линейка: её концы и направление, когда запоминали. */
typedef struct RulerEnds {
    TlVec2 *start, *end;
    TlVec2 original_start, original_end;
    long start_anchor, end_anchor; /* номера в anchors; −1 — конец ни за что не держится */
} RulerEnds;

TL_ARRAY_DEFINE(RulerEndsList, RulerEnds, ruler_ends_list)

struct TlMeasureAnchors {
    AnchorList anchors;
    RulerEndsList rulers;
};

/* Сначала углы (они точнее), потом рёбра; контуры владельца self (сам шов) — мимо. */
static bool find_anchor_except(const ContourList *contours, TlVec2 *point, const void *self, Anchor *anchor)
{
    for (int pass = 0; pass < 2; pass++)
        for (size_t c = 0; c < contours->count; c++) {
            const Contour *contour = &contours->items[c];
            if (self && contour->owner == self)
                continue;
            size_t count = pass == 0 ? contour->points.count : edge_count(contour);
            for (size_t e = 0; e < count; e++) {
                double along = 0;
                double distance = pass == 0 ? tl_distance(*point, edge_point(contour, e, 0))
                                            : tl_point_segment_distance(*point, edge_point(contour, e, 0),
                                                                        edge_point(contour, e, 1), &along);
                if (distance < ON_CONTOUR_MM) {
                    *anchor = (Anchor){point, contour->kind, contour->owner, contour->points.count, e, along, pass == 1};
                    return true;
                }
            }
        }
    return false;
}

static bool find_anchor(const ContourList *contours, TlVec2 *point, Anchor *anchor)
{
    return find_anchor_except(contours, point, NULL, anchor);
}

static long remember(TlMeasureAnchors *anchors, const ContourList *contours, TlVec2 *point, const void *self)
{
    Anchor anchor;
    if (!find_anchor_except(contours, point, self, &anchor))
        return -1;
    anchor_list_push(&anchors->anchors, anchor);
    return (long)anchors->anchors.count - 1;
}

TlMeasureAnchors *tl_measure_anchors_remember(TlProject *project)
{
    TlMeasureAnchors *anchors = TL_NEW(TlMeasureAnchors);
    ContourList contours = contours_of(project);
    for (size_t i = 0; i < project->rulers.count; i++) {
        TlRuler *ruler = project->rulers.items[i];
        RulerEnds ends = {&ruler->start, &ruler->end, ruler->start, ruler->end, -1, -1};
        ends.start_anchor = remember(anchors, &contours, &ruler->start, NULL);
        ends.end_anchor = remember(anchors, &contours, &ruler->end, NULL);
        ruler_ends_list_push(&anchors->rulers, ends);
    }
    for (size_t i = 0; i < project->expansion_joints.count; i++) { /* шов — не за самого себя */
        TlExpansionJoint *joint = project->expansion_joints.items[i];
        remember(anchors, &contours, &joint->start, joint);
        remember(anchors, &contours, &joint->end, joint);
    }
    free_contours(&contours);
    return anchors;
}

static const Contour *contour_of(const ContourList *contours, const Anchor *anchor)
{
    for (size_t c = 0; c < contours->count; c++) {
        const Contour *contour = &contours->items[c];
        if (contour->kind == anchor->kind && contour->owner == anchor->owner &&
            contour->points.count == anchor->corners)
            return contour;
    }
    return NULL;
}

/* Сдвинулся один конец линейки, а другой лежит на ребре (грань стены, перегородки) — другой
   скользит по ребру, и линейка остаётся параллельной себе (под прямым углом к стене — так и стоит). */
static void keep_direction(const ContourList *contours, const TlMeasureAnchors *anchors, const RulerEnds *ends)
{
    bool start_moved = tl_distance(*ends->start, ends->original_start) > 1e-6;
    bool end_moved = tl_distance(*ends->end, ends->original_end) > 1e-6;
    long other_index = start_moved ? ends->end_anchor : ends->start_anchor;
    if (start_moved == end_moved || other_index < 0)
        return;
    const Anchor *other = &anchors->anchors.items[other_index];
    const Contour *contour = contour_of(contours, other);
    if (!other->on_edge || !contour)
        return;
    TlVec2 from = edge_point(contour, other->edge, 0), to = edge_point(contour, other->edge, 1);
    TlVec2 moved = start_moved ? *ends->start : *ends->end;
    TlVec2 hit;
    if (!tl_line_intersection(moved, tl_vec2_sub(ends->original_end, ends->original_start), from,
                              tl_vec2_sub(to, from), &hit))
        return;
    double along;
    if (tl_point_segment_distance(hit, from, to, &along) < ON_CONTOUR_MM) /* за концом ребра — не тянем */
        *other->point = hit;
}

void tl_measure_anchors_apply(TlProject *project, const TlMeasureAnchors *anchors)
{
    if (!anchors || anchors->anchors.count == 0)
        return;
    ContourList contours = contours_of(project);
    for (size_t a = 0; a < anchors->anchors.count; a++) {
        const Anchor *anchor = &anchors->anchors.items[a];
        const Contour *contour = contour_of(&contours, anchor);
        if (contour)
            *anchor->point = edge_point(contour, anchor->edge, anchor->along);
    }
    for (size_t r = 0; r < anchors->rulers.count; r++)
        keep_direction(&contours, anchors, &anchors->rulers.items[r]);
    free_contours(&contours);
}

void tl_measure_anchors_free(TlMeasureAnchors *anchors)
{
    if (!anchors)
        return;
    anchor_list_free(&anchors->anchors);
    ruler_ends_list_free(&anchors->rulers);
    free(anchors);
}

/* ---- длина линейки числом ------------------------------------------------------------ */

/* Что тянуть, чтобы сдвинуть то, за что держится конец; TL_DRAG_NONE — двигать нечего (стена дома). */
static TlDragTarget drag_target_of(TlProject *project, const Anchor *anchor)
{
    void *owner = (void *)anchor->owner;
    switch (anchor->kind) {
    case PARTITION_BODY:
    case PARTITION_AXIS:
        return (TlDragTarget){.kind = TL_DRAG_PARTITION, .partition = owner, .index = TL_DRAG_PARTITION_BODY};
    case OPENING_CUT:
    case OPENING_JAMBS: return (TlDragTarget){.kind = TL_DRAG_OPENING, .opening = owner};
    case THRESHOLD:
        return (TlDragTarget){.kind = TL_DRAG_THRESHOLD, .opening = owner, .index = TL_DRAG_THRESHOLD_LINE};
    case LINE: return (TlDragTarget){.kind = TL_DRAG_JOINT, .joint = owner, .index = 0};
    case PIPE_POINTS: return (TlDragTarget){.kind = TL_DRAG_PIPE, .pipe = owner};
    case FIXTURE_OUTLINE:
        return (TlDragTarget){.kind = TL_DRAG_FIXTURE, .index = (int)((const TlFixture *)owner - project->fixtures.items)};
    case DRAIN_CENTER:
    case DRAIN_OUTLINE:
        return (TlDragTarget){.kind = TL_DRAG_DRAIN, .index = (int)((const TlDrain *)owner - project->drains.items)};
    default: return (TlDragTarget){.kind = TL_DRAG_NONE};
    }
}

static TlDragTarget held_by(TlProject *project, const ContourList *contours, TlVec2 *point, Anchor *anchor)
{
    if (!find_anchor(contours, point, anchor)) {
        anchor->owner = NULL;
        return (TlDragTarget){.kind = TL_DRAG_NONE};
    }
    return drag_target_of(project, anchor);
}

/* Что сдвигать охотнее: стена дома — никогда, перегородка — в последнюю очередь, проём — вдоль
   стены, предметы, швы и стыки — в первую. */
static int mobility(TlDragKind kind)
{
    switch (kind) {
    case TL_DRAG_NONE: return 0;
    case TL_DRAG_PARTITION: return 1;
    case TL_DRAG_OPENING: return 2;
    default: return 3;
    }
}

bool tl_measure_set_ruler_length(TlProject *project, TlRuler *ruler, double length)
{
    TlRuler moved = *ruler;
    if (!tl_ruler_set_length(&moved, length))
        return false;
    ContourList contours = contours_of(project);
    Anchor start_anchor, end_anchor;
    TlDragTarget at_start = held_by(project, &contours, &ruler->start, &start_anchor);
    TlDragTarget at_end = held_by(project, &contours, &ruler->end, &end_anchor);
    free_contours(&contours);
    bool move_start = mobility(at_start.kind) > mobility(at_end.kind); /* поровну — конец */
    TlDragTarget object = move_start ? at_start : at_end;
    if (move_start) { /* конец на месте, начало — по той же прямой */
        TlVec2 back = tl_vec2_normalized(tl_vec2_sub(ruler->start, ruler->end));
        moved = *ruler;
        moved.start = tl_vec2_add(ruler->end, tl_vec2_scale(back, length));
    }
    TlDrag drag;
    TlVec2 from = move_start ? ruler->start : ruler->end;
    bool same_object = start_anchor.owner && start_anchor.owner == end_anchor.owner; /* длина не изменится */
    if (object.kind == TL_DRAG_NONE || same_object || !tl_drag_begin(&drag, project, object, from)) {
        *ruler = moved;
        return true;
    }
    TlSnapContext exact = {.project = project};
    tl_drag_move(&drag, project, move_start ? moved.start : moved.end, &exact, true, NULL);
    tl_drag_end(&drag, project);
    TlMeasureAnchors held = {0}; /* конец — туда, куда на самом деле встал предмет (проём — только вдоль стены) */
    anchor_list_push(&held.anchors, move_start ? start_anchor : end_anchor);
    tl_measure_anchors_apply(project, &held);
    anchor_list_free(&held.anchors);
    return true;
}

#include "tl_measure_anchor.h"

#include <math.h>

#define ON_CONTOUR_MM 0.5 /* конец ближе этого к углу или ребру — примагничен к нему */

/* Контуры, к которым примагничиваются концы (как в tl_measure_snap): у каждого — вид и владелец. */
typedef enum ContourKind {
    HOUSE_INNER,
    HOUSE_OUTER,
    HOUSE_AXIS,
    PARTITION_BODY,
    PARTITION_AXIS, /* отрезок: два угла, одно ребро */
    OPENING_CUT
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

static ContourList contours_of(TlProject *project)
{
    ContourList list = {0};
    for (size_t i = 0; i < project->outlines.count; i++) {
        TlOutline *outline = project->outlines.items[i];
        add_contour(&list, HOUSE_INNER, outline, tl_polygon_copy(&outline->inner));
        add_contour(&list, HOUSE_OUTER, outline, tl_outline_outer(outline));
        add_contour(&list, HOUSE_AXIS, outline, tl_outline_axis(outline));
    }
    for (size_t i = 0; i < project->partitions.count; i++) {
        TlPartition *partition = project->partitions.items[i];
        add_contour(&list, PARTITION_BODY, partition, tl_project_partition_body(project, partition, 0));
        TlPolygon axis = {0};
        tl_polygon_push(&axis, partition->start);
        tl_polygon_push(&axis, partition->end);
        add_contour(&list, PARTITION_AXIS, partition, axis);
    }
    for (size_t i = 0; i < project->openings.count; i++) {
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

/* Рёбер у контура: у отрезка — одно, у многоугольника — сколько углов. */
static size_t edge_count(const Contour *contour)
{
    return contour->kind == PARTITION_AXIS ? 1 : contour->points.count;
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
} Anchor;

TL_ARRAY_DEFINE(AnchorList, Anchor, anchor_list)

struct TlMeasureAnchors {
    AnchorList anchors;
};

/* Сначала углы (они точнее), потом рёбра. */
static bool find_anchor(const ContourList *contours, TlVec2 *point, Anchor *anchor)
{
    for (int pass = 0; pass < 2; pass++)
        for (size_t c = 0; c < contours->count; c++) {
            const Contour *contour = &contours->items[c];
            for (size_t e = 0; e < edge_count(contour); e++) {
                double along = 0;
                double distance = pass == 0 ? tl_distance(*point, edge_point(contour, e, 0))
                                            : tl_point_segment_distance(*point, edge_point(contour, e, 0),
                                                                        edge_point(contour, e, 1), &along);
                if (distance < ON_CONTOUR_MM) {
                    *anchor = (Anchor){point, contour->kind, contour->owner, contour->points.count, e, along};
                    return true;
                }
            }
        }
    return false;
}

static void remember(TlMeasureAnchors *anchors, const ContourList *contours, TlVec2 *point)
{
    Anchor anchor;
    if (find_anchor(contours, point, &anchor))
        anchor_list_push(&anchors->anchors, anchor);
}

TlMeasureAnchors *tl_measure_anchors_remember(TlProject *project)
{
    TlMeasureAnchors *anchors = TL_NEW(TlMeasureAnchors);
    ContourList contours = contours_of(project);
    for (size_t i = 0; i < project->rulers.count; i++) {
        remember(anchors, &contours, &project->rulers.items[i]->start);
        remember(anchors, &contours, &project->rulers.items[i]->end);
    }
    for (size_t i = 0; i < project->expansion_joints.count; i++) {
        remember(anchors, &contours, &project->expansion_joints.items[i]->start);
        remember(anchors, &contours, &project->expansion_joints.items[i]->end);
    }
    free_contours(&contours);
    return anchors;
}

void tl_measure_anchors_apply(TlProject *project, const TlMeasureAnchors *anchors)
{
    if (!anchors || anchors->anchors.count == 0)
        return;
    ContourList contours = contours_of(project);
    for (size_t a = 0; a < anchors->anchors.count; a++) {
        const Anchor *anchor = &anchors->anchors.items[a];
        for (size_t c = 0; c < contours.count; c++) {
            const Contour *contour = &contours.items[c];
            if (contour->kind == anchor->kind && contour->owner == anchor->owner &&
                contour->points.count == anchor->corners) {
                *anchor->point = edge_point(contour, anchor->edge, anchor->along);
                break;
            }
        }
    }
    free_contours(&contours);
}

void tl_measure_anchors_free(TlMeasureAnchors *anchors)
{
    if (!anchors)
        return;
    anchor_list_free(&anchors->anchors);
    free(anchors);
}

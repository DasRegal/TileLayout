/* Проёмы: в теле какой стены стоят, какую часть стены вырезают и какие
 * участки стены остаются глухими. */
#include "tl_project.h"

#include <math.h>
#include <stdlib.h>

#include "tl_regions.h"

#define HOST_TOLERANCE 1.0
#define CUT_MARGIN 1.0

bool tl_project_opening_host(TlProject *project, const TlOpening *opening, TlBand *band)
{
    return tl_project_find_band(project, opening->center, HOST_TOLERANCE, band);
}

/* ---- форма выреза ---------------------------------------------------------- */

/* Наружная стена: грани параллельны — прямоугольник на всю толщину, вместе с
   примыкающей стеной соседнего дома. */
static TlPolygon wall_opening_shape(TlProject *project, const TlBand *band, double from, double to)
{
    TlWallRefList links = tl_project_linked_walls(project, band->host.outline, band->host.wall);
    double neighbour = 0.0;
    for (size_t i = 0; i < links.count; i++)
        neighbour = fmax(neighbour, links.items[i].outline->thickness.items[links.items[i].wall]);
    tl_wall_ref_list_free(&links);
    return tl_band_rect(band, from, to, band->near - CUT_MARGIN, band->far + neighbour + CUT_MARGIN);
}

/* Оси полосы: x — вдоль стены от её начала, y — поперёк. */
static TlVec2 to_band(const TlBand *band, TlVec2 point)
{
    TlVec2 offset = tl_vec2_sub(point, band->origin);
    return tl_vec2(tl_vec2_dot(offset, band->direction), tl_vec2_cross(band->direction, offset));
}

static TlVec2 from_band(const TlBand *band, TlVec2 local)
{
    return tl_vec2_add(band->origin, tl_vec2_add(tl_vec2_scale(band->direction, local.x),
                                                 tl_vec2_scale(tl_vec2_perpendicular(band->direction), local.y)));
}

/* Часть многоугольника между поперечными линиями along = from и along = to. */
static TlPolygon clip_along(const TlBand *band, const TlPolygon *polygon, double from, double to)
{
    const double unlimited = 1e9;
    TlPolygon local = {0};
    for (size_t i = 0; i < polygon->count; i++)
        tl_polygon_push(&local, to_band(band, polygon->items[i]));
    TlPolygon clipped = tl_polygon_clip_rect(&local, (TlRect){from, -unlimited, to, unlimited});
    for (size_t i = 0; i < clipped.count; i++)
        clipped.items[i] = from_band(band, clipped.items[i]);
    tl_polygon_free(&local);
    return clipped;
}

/* Перегородка: тело между краями проёма, расширенное на margin (у выреза —
   чтобы он закрыл и обводку боков). */
static TlPolygon partition_opening_shape(TlProject *project, const TlBand *band, double from, double to,
                                         double margin)
{
    TlPolygon body = tl_project_partition_body(project, band->host.partition, 0);
    TlPolygon widened = margin > 0 ? tl_polygon_offset_uniform(&body, margin) : tl_polygon_copy(&body);
    TlPolygon shape = clip_along(band, &widened, from, to);
    tl_polygon_free(&widened);
    tl_polygon_free(&body);
    return shape;
}

TlPolygon tl_project_opening_shape(TlProject *project, const TlBand *band, double along, double width)
{
    double from = along - width / 2, to = along + width / 2;
    return band->host.kind == TL_HOST_WALL ? wall_opening_shape(project, band, from, to)
                                           : partition_opening_shape(project, band, from, to, CUT_MARGIN);
}

bool tl_project_opening_cut(TlProject *project, const TlOpening *opening, TlPolygon *cut)
{
    TlBand band;
    if (!tl_project_opening_host(project, opening, &band))
        return false;
    double along;
    tl_band_local(&band, opening->center, &along, NULL);
    *cut = tl_project_opening_shape(project, &band, along, opening->width);
    return true;
}

/* ---- пол под проёмом ---------------------------------------------------------- */

#define WALL_FLOOR_OVERLAP 0.05 /* мм — пол проёма заходит в пол дома, чтобы сомкнуться с ним */
#define DOORWAY_OVERLAP 0.05    /* мм — то же у проёма в перегородке */
#define MIN_FLOOR_DEPTH 0.5     /* мм — меньше этого пола под проёмом в наружной стене нет */

/* Проём в основной стене: поперёк — от внутренней грани (0) до дальней (depth):
   у стены между домами это внутренняя грань соседнего дома, у наружной — улица. */
typedef struct WallPassage {
    TlBand band;
    double from, to;   /* края проёма вдоль стены */
    double depth;
    TlOutline *neighbour; /* дом за стеной; NULL — за стеной улица */
} WallPassage;

static bool opening_span(TlProject *project, const TlOpening *opening, TlHostKind kind, TlBand *band,
                         double *from, double *to)
{
    if (!tl_project_opening_host(project, opening, band) || band->host.kind != kind)
        return false;
    double along;
    tl_band_local(band, opening->center, &along, NULL);
    *from = along - opening->width / 2;
    *to = along + opening->width / 2;
    return true;
}

static bool wall_passage(TlProject *project, const TlOpening *opening, WallPassage *passage)
{
    if (!opening_span(project, opening, TL_HOST_WALL, &passage->band, &passage->from, &passage->to))
        return false;
    TlWallRefList links = tl_project_linked_walls(project, passage->band.host.outline, passage->band.host.wall);
    passage->depth = passage->band.far;
    passage->neighbour = NULL;
    for (size_t i = 0; i < links.count; i++) {
        double neighbour_depth = passage->band.far + links.items[i].outline->thickness.items[links.items[i].wall];
        if (!passage->neighbour || neighbour_depth > passage->depth) {
            passage->depth = neighbour_depth;
            passage->neighbour = links.items[i].outline;
        }
    }
    tl_wall_ref_list_free(&links);
    return true;
}

/* Стык поперёк стены у косяка в начале (at_end = false) или в конце проёма. */
static double wall_split(const WallPassage *passage, const TlOpening *opening, bool at_end)
{
    return (at_end ? opening->threshold_end : opening->threshold) * passage->depth;
}

/* Четырёхугольник проёма от across_from до across_to (у каждого косяка своё). */
static TlPolygon passage_quad(const WallPassage *passage, const double across_from[2], const double across_to[2])
{
    const TlVec2 corners[] = {tl_band_point(&passage->band, passage->from, across_from[0]),
                              tl_band_point(&passage->band, passage->to, across_from[1]),
                              tl_band_point(&passage->band, passage->to, across_to[1]),
                              tl_band_point(&passage->band, passage->from, across_to[0])};
    return tl_polygon_from_points(corners, 4);
}

/* Точка стыка на расстоянии along от начала перегородки: между её боками. */
static TlVec2 threshold_point(const TlPartition *partition, double along, double threshold)
{
    TlPolygon body = tl_partition_body(partition, 0);
    double t = along / tl_partition_length(partition);
    TlVec2 right = tl_vec2_lerp(body.items[TL_PARTITION_START_RIGHT], body.items[TL_PARTITION_END_RIGHT], t);
    TlVec2 left = tl_vec2_lerp(body.items[TL_PARTITION_START_LEFT], body.items[TL_PARTITION_END_LEFT], t);
    tl_polygon_free(&body);
    return tl_vec2_lerp(right, left, threshold);
}

bool tl_project_opening_doorway(TlProject *project, const TlOpening *opening, TlPolygon *doorway)
{
    TlBand band;
    double from, to;
    if (opening_span(project, opening, TL_HOST_PARTITION, &band, &from, &to)) {
        /* чуть за бока: у наклонного бока (трапеция) край пола проёма иначе расходится
           с боком тела на доли микрона, и щель между ними отрезает пол от комнаты */
        *doorway = partition_opening_shape(project, &band, from, to, DOORWAY_OVERLAP);
        return true;
    }
    WallPassage passage;
    if (!wall_passage(project, opening, &passage))
        return false;
    const double zero[2] = {0, 0};
    double floor_to[2] = {passage.depth, passage.depth};
    if (!passage.neighbour) {
        floor_to[0] = wall_split(&passage, opening, false);
        floor_to[1] = wall_split(&passage, opening, true);
    }
    if (fmax(floor_to[0], floor_to[1]) < MIN_FLOOR_DEPTH)
        return false;
    *doorway = passage_quad(&passage, zero, floor_to);
    return true;
}

bool tl_project_opening_wall_floor(TlProject *project, const TlOpening *opening, const TlOutline *house,
                                   TlPolygon *floor)
{
    WallPassage passage;
    if (!wall_passage(project, opening, &passage))
        return false;
    const double split[2] = {wall_split(&passage, opening, false), wall_split(&passage, opening, true)};
    double across_from[2], across_to[2];
    for (int e = 0; e < 2; e++) {
        if (house == passage.band.host.outline) {
            across_from[e] = -WALL_FLOOR_OVERLAP;
            across_to[e] = split[e];
        } else if (house == passage.neighbour) {
            across_from[e] = split[e];
            across_to[e] = passage.depth + WALL_FLOOR_OVERLAP;
        } else {
            return false;
        }
    }
    if (fmax(across_to[0] - across_from[0], across_to[1] - across_from[1]) < MIN_FLOOR_DEPTH + WALL_FLOOR_OVERLAP)
        return false;
    *floor = passage_quad(&passage, across_from, across_to);
    return true;
}

bool tl_project_opening_threshold(TlProject *project, const TlOpening *opening, TlVec2 *from, TlVec2 *to)
{
    TlBand band;
    double from_along, to_along;
    if (opening_span(project, opening, TL_HOST_PARTITION, &band, &from_along, &to_along)) {
        *from = threshold_point(band.host.partition, from_along, opening->threshold);
        *to = threshold_point(band.host.partition, to_along, opening->threshold_end);
        return true;
    }
    WallPassage passage;
    if (!wall_passage(project, opening, &passage))
        return false;
    *from = tl_band_point(&passage.band, passage.from, wall_split(&passage, opening, false));
    *to = tl_band_point(&passage.band, passage.to, wall_split(&passage, opening, true));
    return true;
}

bool tl_project_opening_section(TlProject *project, const TlOpening *opening, TlVec2 *right, TlVec2 *left)
{
    TlBand band;
    double from_along, to_along;
    if (opening_span(project, opening, TL_HOST_PARTITION, &band, &from_along, &to_along)) {
        double middle = (from_along + to_along) / 2;
        *right = threshold_point(band.host.partition, middle, 0);
        *left = threshold_point(band.host.partition, middle, 1);
        return true;
    }
    WallPassage passage;
    if (!wall_passage(project, opening, &passage))
        return false;
    double middle = (passage.from + passage.to) / 2;
    *right = tl_band_point(&passage.band, middle, 0);
    *left = tl_band_point(&passage.band, middle, passage.depth);
    return true;
}

bool tl_project_opening_outside(TlProject *project, const TlOpening *opening)
{
    WallPassage passage;
    return wall_passage(project, opening, &passage) && !passage.neighbour;
}

double tl_project_default_threshold(TlProject *project, const TlOpening *opening)
{
    return tl_project_opening_outside(project, opening) ? 0 : TL_OPENING_DEFAULT_THRESHOLD;
}

void tl_project_remove_orphan_openings(TlProject *project)
{
    for (size_t i = project->openings.count; i-- > 0;) {
        TlBand band;
        if (!tl_project_opening_host(project, project->openings.items[i], &band))
            tl_project_delete_opening(project, project->openings.items[i]);
    }
}

/* ---- глухие участки ---------------------------------------------------------- */

#define MIN_OVERLAP_AREA 1.0 /* мм²: вырез действительно задевает стену */

typedef struct Interval {
    double from, to;
} Interval;

static int compare_intervals(const void *a, const void *b)
{
    double difference = ((const Interval *)a)->from - ((const Interval *)b)->from;
    return (difference > 0) - (difference < 0);
}

static TlPolygon host_body(TlProject *project, const TlBand *band)
{
    if (band->host.kind == TL_HOST_WALL)
        return tl_outline_wall_quad(band->host.outline, band->host.wall, NULL);
    return tl_project_partition_body(project, band->host.partition, 0);
}

/* Участок вдоль стены, который закрывает вырез (false — вырез мимо стены). */
static bool covered_interval(const TlBand *band, const TlPolygon *body, const TlPolygon *cut, Interval *interval)
{
    if (tl_polygons_overlap_area(cut, body) < MIN_OVERLAP_AREA)
        return false;
    *interval = (Interval){INFINITY, -INFINITY};
    for (size_t i = 0; i < cut->count; i++) {
        double along = to_band(band, cut->items[i]).x;
        interval->from = fmin(interval->from, along);
        interval->to = fmax(interval->to, along);
    }
    return true;
}

void tl_project_solid_stretch(TlProject *project, const TlBand *band, double *from, double *to)
{
    TlPolygon body = host_body(project, band);
    Interval *covered = tl_malloc((project->openings.count + 1) * sizeof *covered);
    size_t count = 0;
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon cut;
        if (!tl_project_opening_cut(project, project->openings.items[i], &cut))
            continue;
        if (covered_interval(band, &body, &cut, &covered[count]))
            count++;
        tl_polygon_free(&cut);
    }
    tl_polygon_free(&body);
    qsort(covered, count, sizeof *covered, compare_intervals);

    *from = *to = 0;
    double solid_from = 0;
    for (size_t i = 0; i <= count; i++) {
        double solid_to = i < count ? fmin(covered[i].from, band->length) : band->length;
        if (solid_to - solid_from > *to - *from) {
            *from = solid_from;
            *to = solid_to;
        }
        if (i < count)
            solid_from = fmax(solid_from, covered[i].to);
    }
    free(covered);
}

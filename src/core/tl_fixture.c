#include "tl_fixture.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static const struct {
    const char *key, *name;
    double width, depth, height;
    bool under, behind;
} KINDS[TL_FIXTURE_KIND_COUNT] = {
    {"bath", "Ванна", 1700, 750, 600, false, false},
    {"tray", "Душевой поддон", 900, 900, 150, false, false},
    {"installation", "Инсталляция", 600, 250, 1200, false, false},
    {"cabinet", "Тумба", 600, 450, 850, true, true},
    {"kitchen", "Кухня", 2400, 600, 850, false, false},
    {"stairs", "Лестница", 900, 840, 510, false, true},
};

TlFixture tl_fixture_default(TlFixtureKind kind, TlVec2 center)
{
    if ((unsigned)kind >= TL_FIXTURE_KIND_COUNT)
        kind = TL_FIXTURE_BATH;
    return (TlFixture){kind, center, KINDS[kind].width, KINDS[kind].depth, KINDS[kind].height, 0,
                       KINDS[kind].under, KINDS[kind].behind, kind == TL_FIXTURE_STAIRS ? 3 : 0};
}

const char *tl_fixture_kind_name(TlFixtureKind kind)
{
    return (unsigned)kind < TL_FIXTURE_KIND_COUNT ? KINDS[kind].name : "Предмет";
}

const char *tl_fixture_kind_key(TlFixtureKind kind)
{
    return (unsigned)kind < TL_FIXTURE_KIND_COUNT ? KINDS[kind].key : "bath";
}

TlFixtureKind tl_fixture_kind_from_key(const char *key)
{
    for (int kind = 0; kind < TL_FIXTURE_KIND_COUNT; kind++)
        if (strcmp(KINDS[kind].key, key) == 0)
            return (TlFixtureKind)kind;
    return TL_FIXTURE_BATH;
}

TlPolygon tl_fixture_outline(const TlFixture *fixture)
{
    double radians = fixture->angle * TL_PI / 180;
    TlVec2 x = {cos(radians), sin(radians)}, y = {-sin(radians), cos(radians)};
    double w = fixture->width / 2, d = fixture->depth / 2;
    const double corners[4][2] = {{-w, -d}, {w, -d}, {w, d}, {-w, d}};
    TlPolygon outline = {0};
    for (int i = 0; i < 4; i++)
        tl_polygon_push(&outline, tl_vec2_add(fixture->center, tl_vec2_add(tl_vec2_scale(x, corners[i][0]),
                                                                           tl_vec2_scale(y, corners[i][1]))));
    return outline;
}

bool tl_fixture_snap_to_walls(TlFixture *fixture, const TlPolygon *walls, double reach)
{
    size_t best = SIZE_MAX;
    double best_distance = reach + fixture->depth / 2;
    for (size_t e = 0; e < walls->count; e++) {
        TlVec2 a = tl_polygon_edge_start(walls, e), b = tl_polygon_edge_end(walls, e);
        if (tl_distance(a, b) < 1)
            continue;
        double distance = tl_point_segment_distance(fixture->center, a, b, NULL);
        if (distance < best_distance) {
            best = e;
            best_distance = distance;
        }
    }
    if (best == SIZE_MAX)
        return false;
    TlVec2 a = tl_polygon_edge_start(walls, best), b = tl_polygon_edge_end(walls, best);
    TlVec2 along = tl_vec2_normalized(tl_vec2_sub(b, a));
    TlVec2 inward = {-along.y, along.x}; /* в комнату: сторона, где центр предмета */
    if (tl_vec2_dot(tl_vec2_sub(fixture->center, a), inward) < 0)
        inward = tl_vec2_scale(inward, -1);
    double t = tl_vec2_dot(tl_vec2_sub(fixture->center, a), along);
    double length = tl_distance(a, b);
    t = fmin(fmax(t, fmin(fixture->width / 2, length / 2)), fmax(length - fixture->width / 2, length / 2));
    /* ось Y предмета — от спинки к переду, то есть в комнату */
    fixture->angle = atan2(-inward.x, inward.y) * 180 / TL_PI;
    fixture->center = tl_vec2_add(tl_vec2_add(a, tl_vec2_scale(along, t)), tl_vec2_scale(inward, fixture->depth / 2));
    return true;
}

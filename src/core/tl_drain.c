#include "tl_drain.h"

#include <math.h>
#include <string.h>

#include "tl_fixture.h"

TlDrain tl_drain_default(TlDrainKind kind, TlVec2 center)
{
    if (kind == TL_DRAIN_LINEAR)
        return (TlDrain){TL_DRAIN_LINEAR, center, 70, 800, 0, 1.5, false};
    return (TlDrain){TL_DRAIN_POINT, center, 150, 0, 0, 1.5, true};
}

const char *tl_drain_kind_name(TlDrainKind kind) { return kind == TL_DRAIN_LINEAR ? "Линейный трап" : "Трап"; }

const char *tl_drain_kind_key(TlDrainKind kind) { return kind == TL_DRAIN_LINEAR ? "linear" : "point"; }

TlDrainKind tl_drain_kind_from_key(const char *key)
{
    return strcmp(key, "linear") == 0 ? TL_DRAIN_LINEAR : TL_DRAIN_POINT;
}

/* Трап как предмет: длина — вдоль, глубина — поперёк. */
static TlFixture as_fixture(const TlDrain *drain)
{
    TlFixture fixture = {.center = drain->center, .angle = drain->angle, .depth = drain->size,
                         .width = drain->kind == TL_DRAIN_LINEAR ? drain->length : drain->size};
    return fixture;
}

TlPolygon tl_drain_outline(const TlDrain *drain)
{
    TlFixture fixture = as_fixture(drain);
    return tl_fixture_outline(&fixture);
}

bool tl_drain_snap_to_walls(TlDrain *drain, const TlPolygon *walls, double reach)
{
    TlFixture fixture = as_fixture(drain);
    if (!tl_fixture_snap_to_walls(&fixture, walls, reach))
        return false;
    drain->center = fixture.center;
    drain->angle = fixture.angle;
    return true;
}

double tl_drain_drop(const TlDrain *drain, const TlPolygon *part)
{
    double far = 0, radians = drain->angle * TL_PI / 180;
    TlVec2 across = {-sin(radians), cos(radians)}; /* у лотка — расстояние поперёк него */
    for (size_t i = 0; i < part->count; i++) {
        TlVec2 offset = tl_vec2_sub(part->items[i], drain->center);
        double distance = drain->kind == TL_DRAIN_LINEAR ? fabs(tl_vec2_dot(offset, across)) : tl_distance(part->items[i], drain->center);
        far = fmax(far, distance);
    }
    return far * drain->slope / 100;
}

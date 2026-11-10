#include "tl_opening.h"

TlOpening *tl_opening_new(TlVec2 center, double width)
{
    TlOpening *opening = TL_NEW(TlOpening);
    opening->center = center;
    opening->width = width;
    tl_opening_set_threshold(opening, TL_OPENING_DEFAULT_THRESHOLD);
    opening->threshold_gap = TL_OPENING_DEFAULT_THRESHOLD_GAP;
    opening->kind = TL_OPENING_DOOR;
    opening->height = TL_DOOR_HEIGHT;
    return opening;
}

void tl_opening_set_kind(TlOpening *opening, TlOpeningKind kind)
{
    opening->kind = kind;
    opening->height = kind == TL_OPENING_WINDOW ? TL_WINDOW_HEIGHT : TL_DOOR_HEIGHT;
    opening->sill = kind == TL_OPENING_WINDOW ? TL_WINDOW_SILL : 0;
}

bool tl_opening_has_floor(const TlOpening *opening) { return opening->kind != TL_OPENING_WINDOW; }

const char *tl_opening_kind_name(TlOpeningKind kind)
{
    return kind == TL_OPENING_WINDOW ? "Окно" : kind == TL_OPENING_ARCH ? "Арка" : "Дверь";
}

TlOpening *tl_opening_clone(const TlOpening *opening)
{
    TlOpening *copy = TL_NEW(TlOpening);
    *copy = *opening;
    return copy;
}

void tl_opening_free(TlOpening *opening) { free(opening); }

void tl_opening_set_threshold(TlOpening *opening, double share)
{
    opening->threshold = opening->threshold_end = share;
}

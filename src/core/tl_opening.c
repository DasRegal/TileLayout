#include "tl_opening.h"

TlOpening *tl_opening_new(TlVec2 center, double width)
{
    TlOpening *opening = TL_NEW(TlOpening);
    opening->center = center;
    opening->width = width;
    opening->threshold = TL_OPENING_DEFAULT_THRESHOLD;
    return opening;
}

TlOpening *tl_opening_clone(const TlOpening *opening)
{
    TlOpening *copy = TL_NEW(TlOpening);
    *copy = *opening;
    return copy;
}

void tl_opening_free(TlOpening *opening) { free(opening); }

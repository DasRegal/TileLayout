#include "tl_ruler.h"

TlRuler *tl_ruler_new(TlVec2 start, TlVec2 end)
{
    TlRuler *ruler = TL_NEW(TlRuler);
    ruler->start = start;
    ruler->end = end;
    return ruler;
}

TlRuler *tl_ruler_clone(const TlRuler *ruler)
{
    TlRuler *copy = TL_NEW(TlRuler);
    *copy = *ruler;
    return copy;
}

void tl_ruler_free(TlRuler *ruler) { free(ruler); }

double tl_ruler_length(const TlRuler *ruler) { return tl_distance(ruler->start, ruler->end); }

void tl_ruler_translate(TlRuler *ruler, TlVec2 offset)
{
    ruler->start = tl_vec2_add(ruler->start, offset);
    ruler->end = tl_vec2_add(ruler->end, offset);
}

TlVec2 *tl_ruler_end_point(TlRuler *ruler, TlRulerEnd which)
{
    return which == TL_RULER_START ? &ruler->start : &ruler->end;
}

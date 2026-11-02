#include "tl_pipe.h"

#include <math.h>

#include "tl_memory.h"

#define TOUCH_MM 0.5 /* круг заходит в плитку меньше чем на полмиллиметра — не задевает */

TlPipe *tl_pipe_new(TlVec2 center, double diameter)
{
    TlPipe *pipe = TL_NEW(TlPipe);
    pipe->center = center;
    pipe->diameter = diameter;
    return pipe;
}

void tl_pipe_free(TlPipe *pipe) { free(pipe); }

TlCircle tl_pipe_circle(const TlPipe *pipe) { return (TlCircle){pipe->center, pipe->diameter}; }

void tl_pipe_points(const TlPipe *pipe, TlVec2 points[TL_PIPE_POINT_COUNT])
{
    double r = pipe->diameter / 2;
    TlVec2 c = pipe->center;
    points[0] = c;
    points[1] = tl_vec2(c.x, c.y - r);
    points[2] = tl_vec2(c.x, c.y + r);
    points[3] = tl_vec2(c.x - r, c.y);
    points[4] = tl_vec2(c.x + r, c.y);
}

bool tl_circle_touches_rect(TlCircle circle, TlRect rect)
{
    double x = fmax(rect.left, fmin(circle.center.x, rect.right));
    double y = fmax(rect.top, fmin(circle.center.y, rect.bottom));
    return tl_distance(circle.center, tl_vec2(x, y)) < circle.diameter / 2 - TOUCH_MM;
}

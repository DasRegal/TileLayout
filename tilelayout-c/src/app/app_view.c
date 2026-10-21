#include "app_view.h"

#include <math.h>

static double clamp_scale(double scale)
{
    return fmax(APP_VIEW_MIN_SCALE, fmin(scale, APP_VIEW_MAX_SCALE));
}

TlVec2 app_view_to_screen(const AppView *view, TlVec2 world)
{
    return tl_vec2(world.x * view->scale + view->pan_x, world.y * view->scale + view->pan_y);
}

TlVec2 app_view_to_world(const AppView *view, TlVec2 screen)
{
    return tl_vec2((screen.x - view->pan_x) / view->scale, (screen.y - view->pan_y) / view->scale);
}

double app_view_pixels_to_mm(const AppView *view, double pixels) { return pixels / view->scale; }

void app_view_fit(AppView *view, TlRect world, double margin)
{
    double world_width = fmax(tl_rect_width(world), 1000.0);
    double world_height = fmax(tl_rect_height(world), 1000.0);
    view->scale = clamp_scale(fmin((view->width - 2 * margin) / world_width,
                                   (view->height - 2 * margin) / world_height));
    TlVec2 center = tl_rect_center(world);
    view->pan_x = view->width / 2 - center.x * view->scale;
    view->pan_y = view->height / 2 - center.y * view->scale;
}

void app_view_zoom_at(AppView *view, double factor, TlVec2 screen_anchor)
{
    TlVec2 anchor_world = app_view_to_world(view, screen_anchor);
    view->scale = clamp_scale(view->scale * factor);
    view->pan_x = screen_anchor.x - anchor_world.x * view->scale;
    view->pan_y = screen_anchor.y - anchor_world.y * view->scale;
}

#include "tl_room3d.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_layout.h"
#include "tl_regions.h"

#define NEAR_MM 20.0
#define DEFAULT_FOV 45.0
#define DEFAULT_YAW 235.0
#define DEFAULT_PITCH 38.0
#define TARGET_HEIGHT 0.3        /* смотрим в точку на этой доле высоты помещения */
#define GROUT_SHADE 0.78         /* шов — темнее плитки */
#define FLOOR_BASE "#d9d3c8"     /* пол без плитки */
#define WALL_BASE "#eeebe5"      /* стена без плитки */
#define EDGE "#7d838c"           /* рёбра стен и предметов */
#define STAIRS_BASE "#d3cdc2"
#define JAMB "#dcd8d0"           /* откосы проёмов */
#define GLASS "#cfe1ea"
#define JAMB_DEPTH 120.0         /* глубина проёма без откосов плиткой, мм */
#define TILE_STROKE_PX 0.7
#define EDGE_STROKE_PX 1.0

TL_ARRAY_DEFINE(Points3, TlVec3, points3)
TL_ARRAY_DEFINE(Contours3, Points3, contours3)

typedef struct Face {
    Contours3 contours;
    double rgb[3];
    double stroke_rgb[3];
    bool stroked;
    double stroke_width;
} Face;

TL_ARRAY_DEFINE(FaceList, Face, face_list)

/* Плоская поверхность: основа (первая грань) и плитка на ней. */
typedef struct Group {
    TlVec3 normal, center;
    bool floor;
    FaceList faces;
} Group;

TL_ARRAY_DEFINE(GroupList, Group, group_list)

/* Точка поверхности (план или развёртка) → объём. */
typedef enum MapKind { MAP_FLOOR, MAP_WALL, MAP_STEP } MapKind;

typedef struct Mapper {
    MapKind kind;
    /* стена: начало и направление на плане, начало в развёртке, высота помещения */
    TlVec2 start, direction;
    double wall_x, height;
    /* ступень: где лестница, её поворот, начало столбца в развёртке; part: 0 — подступенок, 1 — проступь */
    TlVec2 center, axis_x, axis_y;
    double column_left, width, depth, riser, tread;
    int step, part;
} Mapper;

static TlVec3 vec3(double x, double y, double z) { return (TlVec3){x, y, z}; }
static TlVec3 sub3(TlVec3 a, TlVec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
static double dot3(TlVec3 a, TlVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static TlVec3 cross3(TlVec3 a, TlVec3 b)
{
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static TlVec3 normalized3(TlVec3 a)
{
    double length = sqrt(dot3(a, a));
    return length > 1e-12 ? vec3(a.x / length, a.y / length, a.z / length) : a;
}

/* Точка плана (y — вниз) на высоте z → объём (Y — вверх по плану). */
static TlVec3 plan_point(TlVec2 p, double z) { return vec3(p.x, -p.y, z); }

/* Ступень на плане: lx — поперёк марша от левого края, ly — от переднего края к спинке. */
static TlVec3 step_point(const Mapper *m, double lx, double ly, double z)
{
    double x = -m->width / 2 + lx, y = m->depth / 2 - ly;
    TlVec2 p = {m->center.x + m->axis_x.x * x + m->axis_y.x * y, m->center.y + m->axis_x.y * x + m->axis_y.y * y};
    return plan_point(p, z);
}

static TlVec3 map_point(const Mapper *m, TlVec2 p)
{
    switch (m->kind) {
    case MAP_FLOOR: return plan_point(p, 0);
    case MAP_WALL: {
        double s = p.x - m->wall_x;
        return plan_point(tl_vec2(m->start.x + m->direction.x * s, m->start.y + m->direction.y * s), m->height - p.y);
    }
    case MAP_STEP: {
        double along = m->height - p.y - m->step * (m->riser + m->tread); /* по развёрнутому профилю ступени */
        double lx = p.x - m->column_left;
        if (m->part == 0)
            return step_point(m, lx, m->step * m->tread, m->step * m->riser + along);
        return step_point(m, lx, m->step * m->tread + along - m->riser, (m->step + 1) * m->riser);
    }
    }
    return vec3(0, 0, 0);
}

static void parse_color(const char *hex, double rgb[3])
{
    unsigned value = 0xcccccc;
    if (hex && hex[0] == '#' && strlen(hex) >= 7)
        sscanf(hex + 1, "%6x", &value);
    rgb[0] = ((value >> 16) & 255) / 255.0;
    rgb[1] = ((value >> 8) & 255) / 255.0;
    rgb[2] = (value & 255) / 255.0;
}

static Face face_new(const char *color, const char *stroke, double stroke_width)
{
    Face face = {.stroked = stroke != NULL, .stroke_width = stroke_width};
    parse_color(color, face.rgb);
    parse_color(stroke, face.stroke_rgb);
    return face;
}

/* Плитка: шов — тем же цветом, только темнее. */
static Face tile_face(const char *color)
{
    Face face = face_new(color, NULL, TILE_STROKE_PX);
    face.stroked = true;
    for (int i = 0; i < 3; i++)
        face.stroke_rgb[i] = face.rgb[i] * GROUT_SHADE;
    return face;
}

/* Площадь куска: внешний контур без дыр. Плитку, которую проём закрыл целиком, раскладка хранит
   подрезкой с дырой во весь кусок — её не рисуют. */
static double piece_area(const TlPolygonList *shapes)
{
    double area = tl_polygon_area(&shapes->items[0]);
    for (size_t i = 1; i < shapes->count; i++)
        area -= tl_polygon_area(&shapes->items[i]);
    return area;
}

static void face_add_polygon(Face *face, const TlPolygon *polygon, const Mapper *mapper, const TlAffine *m,
                             TlVec2 shift)
{
    if (polygon->count < 3)
        return;
    Points3 points = {0};
    for (size_t i = 0; i < polygon->count; i++) {
        TlVec2 p = tl_vec2(polygon->items[i].x + shift.x, polygon->items[i].y + shift.y);
        if (m)
            p = tl_affine_apply(m, p);
        points3_push(&points, map_point(mapper, p));
    }
    contours3_push(&face->contours, points);
}

static void face_add_points(Face *face, const TlVec3 *points, size_t count)
{
    Points3 contour = {0};
    for (size_t i = 0; i < count; i++)
        points3_push(&contour, points[i]);
    contours3_push(&face->contours, contour);
}

static void face_free(Face *face)
{
    for (size_t i = 0; i < face->contours.count; i++)
        points3_free(&face->contours.items[i]);
    contours3_free(&face->contours);
}

static Group group_new(TlVec3 normal, Face base)
{
    Group group = {.normal = normal};
    TlVec3 sum = {0, 0, 0};
    size_t count = 0;
    for (size_t c = 0; c < base.contours.count && c < 1; c++)
        for (size_t i = 0; i < base.contours.items[c].count; i++, count++) {
            TlVec3 p = base.contours.items[c].items[i];
            sum = vec3(sum.x + p.x, sum.y + p.y, sum.z + p.z);
        }
    if (count)
        group.center = vec3(sum.x / count, sum.y / count, sum.z / count);
    face_list_push(&group.faces, base);
    return group;
}

static void groups_free(GroupList *groups)
{
    for (size_t g = 0; g < groups->count; g++) {
        for (size_t f = 0; f < groups->items[g].faces.count; f++)
            face_free(&groups->items[g].faces.items[f]);
        face_list_free(&groups->items[g].faces);
    }
    group_list_free(groups);
}

/* Плитка раскладки — гранями на поверхности. */
static void add_tiles(Group *group, const TlLayout *layout, const Mapper *mapper)
{
    if (!layout || layout->too_many)
        return;
    const TlAffine *m = &layout->local_to_world;
    for (size_t t = 0; t < layout->tiles.count; t++) {
        const TlTile *tile = &layout->tiles.items[t];
        const char *color = (size_t)tile->spec < layout->pattern.spec_count ? layout->pattern.specs[tile->spec].color : NULL;
        if (tile->kind != TL_TILE_WHOLE && tile->shapes.count && piece_area(&tile->shapes) < 1)
            continue;
        Face face = tile_face(color);
        TlVec2 corner = {tile->rect.left, tile->rect.top};
        if (tile->kind != TL_TILE_WHOLE && tile->shapes.count > 1) {
            /* кусок с дырами: дыра у края (проём, панно) — вырез, иначе обводка шла бы поперёк проёма */
            TlRegionList parts = tl_regions_difference(&tile->shapes.items[0], tile->shapes.items + 1, tile->shapes.count - 1);
            for (size_t r = 0; r < parts.count; r++) {
                face_add_polygon(&face, &parts.items[r].outline, mapper, m, corner);
                for (size_t h = 0; h < parts.items[r].holes.count; h++)
                    face_add_polygon(&face, &parts.items[r].holes.items[h], mapper, m, corner);
            }
            tl_region_list_free_all(&parts);
        } else if (tile->kind != TL_TILE_WHOLE && tile->shapes.count) {
            face_add_polygon(&face, &tile->shapes.items[0], mapper, m, corner);
        } else if (tile->outline.count) {
            face_add_polygon(&face, &tile->outline, mapper, m, corner);
        } else {
            const TlVec2 corners[] = {{tile->rect.left, tile->rect.top},
                                      {tile->rect.right, tile->rect.top},
                                      {tile->rect.right, tile->rect.bottom},
                                      {tile->rect.left, tile->rect.bottom}};
            TlPolygon rect = tl_polygon_from_points(corners, 4);
            face_add_polygon(&face, &rect, mapper, m, tl_vec2(0, 0));
            tl_polygon_free(&rect);
        }
        if (face.contours.count)
            face_list_push(&group->faces, face);
        else
            face_free(&face);
    }
}

static void add_panno(Group *group, TlSpace *space, size_t index, const Mapper *mapper)
{
    TlLayout *layout = tl_space_panno_layout(space, index);
    add_tiles(group, layout, mapper);
    tl_layout_free(layout);
}

/* ---- поверхности комнаты ----------------------------------------------------------- */

static void build_floor(GroupList *groups, TlSpace *space)
{
    Mapper mapper = {.kind = MAP_FLOOR};
    Face base = face_new(FLOOR_BASE, EDGE, EDGE_STROKE_PX);
    face_add_polygon(&base, &space->outline, &mapper, NULL, tl_vec2(0, 0));
    for (size_t i = 0; i < space->holes.count; i++)
        face_add_polygon(&base, &space->holes.items[i], &mapper, NULL, tl_vec2(0, 0));
    Group group = group_new(vec3(0, 0, 1), base);
    group.floor = true;
    if (tl_space_effective_tile(space)->enabled)
        for (size_t l = 0; l < tl_space_floor_layout_count(space); l++)
            if (tl_space_layout_tile(space, l)->enabled)
                add_tiles(&group, tl_space_layout_at(space, l), &mapper);
    for (size_t p = 0; p < space->pannos.count; p++)
        if (space->pannos.items[p].wall == TL_PANNO_FLOOR)
            add_panno(&group, space, p, &mapper);
    group_list_push(groups, group);
}

/* Нормаль стены — в комнату. */
static TlVec2 inward(TlSpace *space, const TlRoomWall *wall)
{
    TlVec2 d = tl_vec2_normalized(tl_vec2_sub(wall->end, wall->start)), n = {-d.y, d.x};
    TlVec2 middle = tl_vec2_lerp(wall->start, wall->end, 0.5);
    if (!tl_polygon_contains(tl_space_room_outline(space), tl_vec2_add(middle, tl_vec2_scale(n, 20))))
        n = tl_vec2(d.y, -d.x);
    return n;
}

/* Откосы проёма (и стекло окна) — за плоскостью стены, наружу на глубину откоса. Рисуются до стены:
   стена закрывает то, чего через проём не видно. */
static void add_jambs(FaceList *faces, TlProject *project, const TlWallHole *hole, const Mapper *mapper, TlVec2 out)
{
    double depth = hole->reveal > 0 ? hole->reveal : JAMB_DEPTH;
    double left = hole->rect.left - mapper->wall_x, right = hole->rect.right - mapper->wall_x;
    double bottom = mapper->height - hole->rect.bottom, top = mapper->height - hole->rect.top;
    TlVec2 a = tl_vec2_add(mapper->start, tl_vec2_scale(mapper->direction, left));
    TlVec2 b = tl_vec2_add(mapper->start, tl_vec2_scale(mapper->direction, right));
    TlVec2 a2 = tl_vec2_add(a, tl_vec2_scale(out, depth)), b2 = tl_vec2_add(b, tl_vec2_scale(out, depth));
    bool window = hole->opening < project->openings.count &&
                  project->openings.items[hole->opening]->kind == TL_OPENING_WINDOW;
    if (window) {
        Face glass = face_new(GLASS, EDGE, EDGE_STROKE_PX);
        const TlVec3 pane[4] = {plan_point(a2, bottom), plan_point(b2, bottom), plan_point(b2, top), plan_point(a2, top)};
        face_add_points(&glass, pane, 4);
        face_list_push(faces, glass);
    }
    const TlVec3 sides[4][4] = {
        {plan_point(a, bottom), plan_point(a, top), plan_point(a2, top), plan_point(a2, bottom)},
        {plan_point(b, bottom), plan_point(b, top), plan_point(b2, top), plan_point(b2, bottom)},
        {plan_point(a, top), plan_point(b, top), plan_point(b2, top), plan_point(a2, top)},
        {plan_point(a, bottom), plan_point(b, bottom), plan_point(b2, bottom), plan_point(a2, bottom)},
    };
    for (int i = 0; i < 4; i++) {
        if (i == 3 && bottom < 1) /* у двери низа нет — там пол */
            continue;
        Face jamb = face_new(JAMB, EDGE, EDGE_STROKE_PX);
        for (int c = 0; c < 3; c++)
            jamb.rgb[c] *= i == 3 ? 1.0 : i == 2 ? 0.86 : 0.93;
        face_add_points(&jamb, sides[i], 4);
        face_list_push(faces, jamb);
    }
}

static void build_walls(GroupList *groups, TlProject *project, TlSpace *space)
{
    size_t floors = tl_space_floor_layout_count(space);
    for (size_t w = 0; w < tl_space_wall_count(space); w++) {
        const TlRoomWall *wall = tl_space_wall_at(space, w);
        Mapper mapper = {.kind = MAP_WALL, .start = wall->start,
                         .direction = tl_vec2_normalized(tl_vec2_sub(wall->end, wall->start)), .wall_x = wall->x,
                         .height = space->height};
        Face base = face_new(WALL_BASE, EDGE, EDGE_STROKE_PX);
        const TlVec2 corners[] = {{wall->x, 0}, {wall->x + wall->length, 0}, {wall->x + wall->length, space->height},
                                  {wall->x, space->height}};
        TlPolygon rect = tl_polygon_from_points(corners, 4);
        face_add_polygon(&base, &rect, &mapper, NULL, tl_vec2(0, 0));
        tl_polygon_free(&rect);
        for (size_t h = 0; h < tl_space_wall_hole_count(space); h++) { /* проёмы — насквозь */
            const TlWallHole *hole = tl_space_wall_hole_at(space, h);
            if (hole->wall != w || hole->opening == SIZE_MAX)
                continue;
            const TlVec2 hc[] = {{hole->rect.left, hole->rect.top}, {hole->rect.right, hole->rect.top},
                                 {hole->rect.right, hole->rect.bottom}, {hole->rect.left, hole->rect.bottom}};
            TlPolygon cut = tl_polygon_from_points(hc, 4);
            face_add_polygon(&base, &cut, &mapper, NULL, tl_vec2(0, 0));
            tl_polygon_free(&cut);
        }
        TlVec2 n = inward(space, wall);
        FaceList jambs = {0};
        for (size_t h = 0; h < tl_space_wall_hole_count(space); h++) {
            const TlWallHole *hole = tl_space_wall_hole_at(space, h);
            if (hole->wall == w && hole->opening != SIZE_MAX)
                add_jambs(&jambs, project, hole, &mapper, tl_vec2_scale(n, -1));
        }
        Group group = group_new(vec3(n.x, -n.y, 0), base);
        for (size_t j = 0; j < jambs.count; j++) /* откосы — раньше стены */
            face_list_push(&group.faces, jambs.items[j]);
        if (jambs.count) {
            Face first = group.faces.items[0];
            memmove(group.faces.items, group.faces.items + 1, (group.faces.count - 1) * sizeof(Face));
            group.faces.items[group.faces.count - 1] = first;
        }
        face_list_free(&jambs);
        for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
            size_t on_wall = SIZE_MAX;
            TlLayout *layout = tl_space_wall_layout_at(space, i, &on_wall, NULL);
            if (on_wall == w && tl_space_wall_layout_reveal(space, i) == SIZE_MAX &&
                tl_space_wall_layout_stairs(space, i, NULL, NULL) == SIZE_MAX)
                add_tiles(&group, layout, &mapper);
        }
        (void)floors;
        for (size_t p = 0; space->walls.enabled && p < space->pannos.count; p++)
            if (space->pannos.items[p].wall != TL_PANNO_FLOOR && tl_space_wall_of_edge(space, space->pannos.items[p].wall) == w)
                add_panno(&group, space, p, &mapper);
        group_list_push(groups, group);
    }
}

/* Прямоугольник по четырём точкам — отдельная поверхность. */
static void add_quad(GroupList *groups, const TlVec3 quad[4], TlVec3 normal, const char *color)
{
    Face face = face_new(color, EDGE, EDGE_STROKE_PX);
    face_add_points(&face, quad, 4);
    group_list_push(groups, group_new(normal, face));
}

static void build_box(GroupList *groups, const TlFixture *fixture, const char *color)
{
    TlPolygon outline = tl_fixture_outline(fixture);
    double h = fmax(fixture->height, 1);
    TlVec3 top[4], bottom[4];
    for (int i = 0; i < 4; i++) {
        top[i] = plan_point(outline.items[i], h);
        bottom[i] = plan_point(outline.items[i], 0);
    }
    add_quad(groups, top, vec3(0, 0, 1), color);
    TlVec2 center = fixture->center;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        const TlVec3 side[4] = {bottom[i], bottom[j], top[j], top[i]};
        TlVec2 middle = tl_vec2_lerp(outline.items[i], outline.items[j], 0.5);
        TlVec2 out = tl_vec2_normalized(tl_vec2_sub(middle, center));
        add_quad(groups, side, vec3(out.x, -out.y, 0), color);
    }
    tl_polygon_free(&outline);
}

static const char *fixture_color(TlFixtureKind kind)
{
    switch (kind) {
    case TL_FIXTURE_BATH:
    case TL_FIXTURE_SHOWER_TRAY: return "#f7f7f5";
    case TL_FIXTURE_INSTALLATION: return "#dcdcdc";
    case TL_FIXTURE_CABINET: return "#d8c8ad";
    case TL_FIXTURE_KITCHEN: return "#cdbb9f";
    default: return "#d6d6d6";
    }
}

static void build_stairs(GroupList *groups, TlSpace *space, size_t k)
{
    const TlFixture *stairs = &space->stairs.items[k];
    TlRect column;
    if (!tl_space_stairs_column(space, k, &column))
        return;
    double radians = stairs->angle * TL_PI / 180;
    Mapper base = {.kind = MAP_STEP, .center = stairs->center, .axis_x = {cos(radians), sin(radians)},
                   .axis_y = {-sin(radians), cos(radians)}, .column_left = column.left, .width = stairs->width,
                   .depth = stairs->depth, .height = space->height, .riser = stairs->height / stairs->steps,
                   .tread = stairs->depth / stairs->steps};
    TlVec2 front = tl_vec2_scale(base.axis_y, 1), side = base.axis_x;
    size_t floors = tl_space_floor_layout_count(space);
    (void)floors;
    for (int step = 0; step < stairs->steps; step++)
        for (int part = 0; part < 2; part++) {
            Mapper mapper = base;
            mapper.step = step;
            mapper.part = part;
            double ly = step * base.tread, z = step * base.riser;
            TlVec3 quad[4];
            if (part == 0) { /* подступенок — вертикально, лицом вперёд */
                quad[0] = step_point(&mapper, 0, ly, z);
                quad[1] = step_point(&mapper, stairs->width, ly, z);
                quad[2] = step_point(&mapper, stairs->width, ly, z + base.riser);
                quad[3] = step_point(&mapper, 0, ly, z + base.riser);
            } else {
                double top = z + base.riser;
                quad[0] = step_point(&mapper, 0, ly, top);
                quad[1] = step_point(&mapper, stairs->width, ly, top);
                quad[2] = step_point(&mapper, stairs->width, ly + base.tread, top);
                quad[3] = step_point(&mapper, 0, ly + base.tread, top);
            }
            Face face = face_new(STAIRS_BASE, EDGE, EDGE_STROKE_PX);
            face_add_points(&face, quad, 4);
            Group group = group_new(part == 0 ? vec3(front.x, -front.y, 0) : vec3(0, 0, 1), face);
            for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
                int layout_step;
                bool tread;
                if (tl_space_wall_layout_stairs(space, i, &layout_step, &tread) == k && layout_step == step &&
                    tread == (part == 1))
                    add_tiles(&group, tl_space_wall_layout_at(space, i, NULL, NULL), &mapper);
            }
            group_list_push(groups, group);
        }
    /* боковины — профиль ступеней, спинка — до верха */
    for (int s = 0; s < 2; s++) {
        double lx = s ? stairs->width : 0;
        Face face = face_new("#c8c2b7", EDGE, EDGE_STROKE_PX);
        Points3 profile = {0};
        points3_push(&profile, step_point(&base, lx, 0, 0));
        for (int step = 0; step < stairs->steps; step++) {
            points3_push(&profile, step_point(&base, lx, step * base.tread, (step + 1) * base.riser));
            points3_push(&profile, step_point(&base, lx, (step + 1) * base.tread, (step + 1) * base.riser));
        }
        points3_push(&profile, step_point(&base, lx, stairs->depth, 0));
        contours3_push(&face.contours, profile);
        TlVec2 out = tl_vec2_scale(side, s ? 1 : -1);
        group_list_push(groups, group_new(vec3(out.x, -out.y, 0), face));
    }
    const TlVec3 back[4] = {step_point(&base, 0, stairs->depth, 0), step_point(&base, stairs->width, stairs->depth, 0),
                            step_point(&base, stairs->width, stairs->depth, stairs->height),
                            step_point(&base, 0, stairs->depth, stairs->height)};
    add_quad(groups, back, vec3(-front.x, front.y, 0), "#c8c2b7");
}

static GroupList build(TlProject *project, TlSpace *space)
{
    GroupList groups = {0};
    build_floor(&groups, space);
    build_walls(&groups, project, space);
    for (size_t k = 0; k < tl_space_stairs_count(space); k++)
        build_stairs(&groups, space, k);
    for (size_t i = 0; i < project->fixtures.count; i++) {
        const TlFixture *fixture = &project->fixtures.items[i];
        if (fixture->kind != TL_FIXTURE_STAIRS && tl_polygon_contains(&space->outline, fixture->center))
            build_box(&groups, fixture, fixture_color(fixture->kind));
    }
    return groups;
}

/* ---- камера и проекция ----------------------------------------------------------- */

static TlRect floor_bounds(TlSpace *space) { return tl_polygon_bounds(&space->outline); }

static double fit_distance(TlSpace *space, double fov, double width, double height)
{
    TlRect b = floor_bounds(space);
    double w = tl_rect_width(b), d = tl_rect_height(b), h = space->height;
    double radius = 0.5 * sqrt(w * w + d * d + h * h) + (0.5 - TARGET_HEIGHT) * h; /* от точки, куда смотрим */
    double vertical = fov * TL_PI / 180, aspect = height > 0 ? width / height : 1;
    double horizontal = 2 * atan(tan(vertical / 2) * aspect);
    return radius / sin(fmin(vertical, horizontal) / 2);
}

TlCamera tl_room3d_default_camera(TlProject *project, TlSpace *space, double width, double height)
{
    (void)project;
    TlRect b = floor_bounds(space);
    TlCamera camera = {.yaw = DEFAULT_YAW, .pitch = DEFAULT_PITCH, .fov = DEFAULT_FOV,
                       .target = vec3((b.left + b.right) / 2, -(b.top + b.bottom) / 2, space->height * TARGET_HEIGHT)};
    camera.distance = fit_distance(space, camera.fov, width, height);
    return camera;
}

typedef struct Eye {
    TlVec3 position, right, up, forward;
    double focal, cx, cy;
} Eye;

static Eye eye_of(const TlCamera *camera, double width, double height)
{
    double yaw = camera->yaw * TL_PI / 180, pitch = camera->pitch * TL_PI / 180;
    TlVec3 offset = vec3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch));
    Eye eye;
    eye.position = vec3(camera->target.x + offset.x * camera->distance, camera->target.y + offset.y * camera->distance,
                        camera->target.z + offset.z * camera->distance);
    eye.forward = normalized3(vec3(-offset.x, -offset.y, -offset.z));
    eye.right = normalized3(cross3(eye.forward, vec3(0, 0, 1)));
    eye.up = cross3(eye.right, eye.forward);
    eye.focal = height / 2 / tan(camera->fov * TL_PI / 180 / 2);
    eye.cx = width / 2;
    eye.cy = height / 2;
    return eye;
}

/* Контур — в пиксели, с отсечением всего, что ближе NEAR_MM к глазу. */
static TlPolygon project_contour(const Eye *eye, const Points3 *contour)
{
    Points3 view = {0}, clipped = {0};
    for (size_t i = 0; i < contour->count; i++) {
        TlVec3 d = sub3(contour->items[i], eye->position);
        points3_push(&view, vec3(dot3(d, eye->right), dot3(d, eye->up), dot3(d, eye->forward)));
    }
    for (size_t i = 0; i < view.count; i++) {
        TlVec3 a = view.items[i], b = view.items[(i + 1) % view.count];
        bool in_a = a.z >= NEAR_MM, in_b = b.z >= NEAR_MM;
        if (in_a)
            points3_push(&clipped, a);
        if (in_a != in_b) {
            double t = (NEAR_MM - a.z) / (b.z - a.z);
            points3_push(&clipped, vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NEAR_MM));
        }
    }
    TlPolygon screen = {0};
    for (size_t i = 0; i < clipped.count; i++) {
        TlVec3 p = clipped.items[i];
        tl_polygon_push(&screen, tl_vec2(eye->cx + p.x / p.z * eye->focal, eye->cy - p.y / p.z * eye->focal));
    }
    points3_free(&view);
    points3_free(&clipped);
    return screen;
}

static void hex_color(char out[8], const double rgb[3], double shade)
{
    int c[3];
    for (int i = 0; i < 3; i++)
        c[i] = (int)lround(fmax(0, fmin(1, rgb[i] * shade)) * 255);
    snprintf(out, 8, "#%02x%02x%02x", c[0], c[1], c[2]);
}

typedef struct Ordered {
    size_t group;
    double distance;
} Ordered;

static int farther_first(const void *a, const void *b)
{
    double da = ((const Ordered *)a)->distance, db = ((const Ordered *)b)->distance;
    return da < db ? 1 : da > db ? -1 : 0;
}

void tl_paint_list_free_all(TlPaintList *list)
{
    for (size_t i = 0; i < list->count; i++)
        tl_polygon_list_free_all(&list->items[i].contours);
    tl_paint_list_free(list);
}

TlPaintList tl_room3d_paint(TlProject *project, TlSpace *space, TlCamera *camera, double width, double height)
{
    TlPaintList paints = {0};
    if (!space || space->outline.count < 3 || width <= 0 || height <= 0)
        return paints;
    camera->pitch = fmax(TL_CAMERA_MIN_PITCH, fmin(TL_CAMERA_MAX_PITCH, camera->pitch));
    if (!(camera->fov > 1))
        camera->fov = DEFAULT_FOV;
    if (!(camera->distance > 0)) { /* подобрать: смотреть в середину комнаты, вся она в кадре */
        camera->target = tl_room3d_default_camera(project, space, width, height).target;
        camera->distance = fit_distance(space, camera->fov, width, height);
    }
    Eye eye = eye_of(camera, width, height);
    const TlVec3 light = normalized3(vec3(-0.45, -0.6, 0.85));
    GroupList groups = build(project, space);
    Ordered *order = malloc(sizeof *order * (groups.count ? groups.count : 1));
    size_t count = 0;
    for (size_t g = 0; g < groups.count; g++) {
        const Group *group = &groups.items[g];
        TlVec3 to_eye = sub3(eye.position, group->center);
        if (!group->floor && dot3(group->normal, to_eye) <= 0) /* к зрителю спиной — ближние стены, задние грани */
            continue;
        order[count++] = (Ordered){g, group->floor ? INFINITY : sqrt(dot3(to_eye, to_eye))};
    }
    qsort(order, count, sizeof *order, farther_first);
    for (size_t o = 0; o < count; o++) {
        const Group *group = &groups.items[order[o].group];
        double shade = 0.62 + 0.38 * fmax(0, dot3(group->normal, light));
        for (size_t f = 0; f < group->faces.count; f++) {
            const Face *face = &group->faces.items[f];
            TlPaint paint = {.stroke_width = face->stroked ? face->stroke_width : 0};
            for (size_t c = 0; c < face->contours.count; c++) {
                TlPolygon screen = project_contour(&eye, &face->contours.items[c]);
                if (screen.count >= 3)
                    tl_polygon_list_push(&paint.contours, screen);
                else
                    tl_polygon_free(&screen);
            }
            if (!paint.contours.count)
                continue;
            hex_color(paint.fill, face->rgb, shade);
            if (face->stroked)
                hex_color(paint.stroke, face->stroke_rgb, shade);
            tl_paint_list_push(&paints, paint);
        }
    }
    free(order);
    groups_free(&groups);
    return paints;
}

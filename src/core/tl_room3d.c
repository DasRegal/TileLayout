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
    const char *texture;  /* рисунок плитки из каталога (настройки раскладки) или NULL */
    TlVec3 corner[3];     /* углы плитки: левый верхний, правый верхний, левый нижний (оси раскладки);
                             правый нижний — четвёртый угол параллелограмма */
    double tile_width, tile_height;
} Face;

TL_ARRAY_DEFINE(FaceList, Face, face_list)

/* Пол рисуется первым (камера над ним — всё остальное перед ним); стены и предметы — в порядке,
   который даёт paint_order: что за плоскостью другой поверхности — раньше неё. */
typedef enum Layer { LAYER_FLOOR, LAYER_WALLS, LAYER_OBJECTS } Layer;

/* Плоская поверхность: основа (первая грань) и плитка на ней. */
typedef struct Group {
    TlVec3 normal, center;
    Points3 outline;   /* контур основы — для порядка рисования */
    Layer layer;
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

/* Контуры куска (tl_layout.h: куски, за каждым — его дыры) по «чёт-нечет»: внутри нечётного числа контуров. */
static bool odd_inside(const bool *inside, size_t count, void *context)
{
    (void)context;
    size_t n = 0;
    for (size_t i = 0; i < count; i++)
        n += inside[i];
    return n % 2 == 1;
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
    Group group = {.normal = normal, .layer = LAYER_OBJECTS};
    TlVec3 sum = {0, 0, 0};
    size_t count = 0;
    for (size_t c = 0; c < base.contours.count && c < 1; c++)
        for (size_t i = 0; i < base.contours.items[c].count; i++, count++) {
            TlVec3 p = base.contours.items[c].items[i];
            sum = vec3(sum.x + p.x, sum.y + p.y, sum.z + p.z);
        }
    if (count)
        group.center = vec3(sum.x / count, sum.y / count, sum.z / count);
    for (size_t i = 0; base.contours.count && i < base.contours.items[0].count; i++)
        points3_push(&group.outline, base.contours.items[0].items[i]);
    face_list_push(&group.faces, base);
    return group;
}

static void groups_free(GroupList *groups)
{
    for (size_t g = 0; g < groups->count; g++) {
        for (size_t f = 0; f < groups->items[g].faces.count; f++)
            face_free(&groups->items[g].faces.items[f]);
        face_list_free(&groups->items[g].faces);
        points3_free(&groups->items[g].outline);
    }
    group_list_free(groups);
}

/* Плитка раскладки — гранями на поверхности. */
static void add_tiles(Group *group, const TlLayout *layout, const Mapper *mapper, const TlTileSettings *settings)
{
    if (!layout || layout->too_many)
        return;
    const TlAffine *m = &layout->local_to_world;
    for (size_t t = 0; t < layout->tiles.count; t++) {
        const TlTile *tile = &layout->tiles.items[t];
        const char *color = (size_t)tile->spec < layout->pattern.spec_count ? layout->pattern.specs[tile->spec].color : NULL;
        Face face = tile_face(color);
        TlVec2 corner = {tile->rect.left, tile->rect.top};
        if (settings && settings->texture[0] && (size_t)tile->spec < layout->pattern.spec_count &&
            layout->pattern.specs[tile->spec].textured) { /* рисунок — на прямоугольник плитки, как на плане */
            const TlVec2 corners[3] = {{tile->rect.left, tile->rect.top}, {tile->rect.right, tile->rect.top},
                                       {tile->rect.left, tile->rect.bottom}};
            for (int k = 0; k < 3; k++)
                face.corner[k] = map_point(mapper, tl_affine_apply(m, corners[k]));
            face.texture = settings->texture;
            face.tile_width = tl_rect_width(tile->rect);
            face.tile_height = tl_rect_height(tile->rect);
        }
        if (tile->kind != TL_TILE_WHOLE && tile->shapes.count > 1) {
            /* куски разрезанной швом плитки и дыры (у края — вырез: иначе обводка шла бы поперёк проёма);
               плитку, которую проём закрыл целиком, раскладка хранит с дырой во весь кусок — её нет */
            TlRegionList parts = tl_regions_build_merged(tile->shapes.items, tile->shapes.count, odd_inside, NULL);
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
    add_tiles(group, layout, mapper, NULL); /* панно — цветом */
    tl_layout_free(layout);
}

#define PIPE_SEGMENTS 28
#define PIPE_HOLE "#3a3e45"     /* отверстие под трубу: тёмный круг коронки */
#define PIPE_RIM "#e8eaee"

/* Отверстие под трубу — круг center диаметром diameter на поверхности (точки — в осях mapper). */
static void add_pipe(Group *group, TlVec2 center, double diameter, const Mapper *mapper)
{
    TlPolygon circle = {0};
    for (int i = 0; i < PIPE_SEGMENTS; i++) {
        double angle = 2 * TL_PI * i / PIPE_SEGMENTS;
        tl_polygon_push(&circle, tl_vec2(center.x + cos(angle) * diameter / 2, center.y + sin(angle) * diameter / 2));
    }
    Face face = face_new(PIPE_HOLE, PIPE_RIM, 1.2);
    face_add_polygon(&face, &circle, mapper, NULL, tl_vec2(0, 0));
    face_list_push(&group->faces, face);
    tl_polygon_free(&circle);
}

/* ---- поверхности комнаты ----------------------------------------------------------- */

static void build_floor(GroupList *groups, TlProject *project, TlSpace *space)
{
    Mapper mapper = {.kind = MAP_FLOOR};
    Face base = face_new(FLOOR_BASE, EDGE, EDGE_STROKE_PX);
    face_add_polygon(&base, &space->outline, &mapper, NULL, tl_vec2(0, 0));
    for (size_t i = 0; i < space->holes.count; i++)
        face_add_polygon(&base, &space->holes.items[i], &mapper, NULL, tl_vec2(0, 0));
    Group group = group_new(vec3(0, 0, 1), base);
    group.layer = LAYER_FLOOR;
    for (size_t l = 0; l < tl_space_floor_layout_count(space); l++) /* у участка плитка своя, даже без плитки комнаты */
        if (tl_space_layout_counted(space, l))
            add_tiles(&group, tl_space_layout_at(space, l), &mapper, tl_space_layout_tile(space, l));
    for (size_t p = 0; p < space->pannos.count; p++)
        if (space->pannos.items[p].wall == TL_PANNO_FLOOR)
            add_panno(&group, space, p, &mapper);
    for (size_t i = 0; i < project->pipes.count; i++) { /* отверстия в плитке пола */
        const TlPipe *pipe = project->pipes.items[i];
        if (tl_polygon_contains(&space->outline, pipe->center))
            add_pipe(&group, pipe->center, pipe->diameter, &mapper);
    }
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
        group.layer = LAYER_WALLS;
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
            TlWallBand band = TL_WALL_MAIN;
            TlLayout *layout = tl_space_wall_layout_at(space, i, &on_wall, &band);
            if (on_wall == w && tl_space_wall_layout_reveal(space, i) == SIZE_MAX &&
                tl_space_wall_layout_stairs(space, i, NULL, NULL) == SIZE_MAX)
                add_tiles(&group, layout, &mapper, tl_space_wall_tile(space, band));
        }
        (void)floors;
        for (size_t p = 0; space->walls.enabled && p < space->pannos.count; p++)
            if (space->pannos.items[p].wall != TL_PANNO_FLOOR && tl_space_wall_of_edge(space, space->pannos.items[p].wall) == w)
                add_panno(&group, space, p, &mapper);
        for (size_t p = 0; p < space->wall_pipes.count; p++) { /* отверстия на стене: розетки, смесители */
            const TlWallPipe *pipe = &space->wall_pipes.items[p];
            if (tl_space_wall_of_edge(space, pipe->wall) == w)
                add_pipe(&group, tl_space_wall_to_development(space, w, pipe->at), pipe->diameter, &mapper);
        }
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
                    add_tiles(&group, tl_space_wall_layout_at(space, i, NULL, NULL), &mapper, tl_space_effective_tile(space));
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
    build_floor(&groups, project, space);
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

#define PLANE_EPS_MM 1.0

/* Где поверхность b относительно плоскости a: -1 — за ней (от зрителя), +1 — перед ней (со стороны
   зрителя), 0 — по обе стороны. Лежащая в плоскости считается «за». */
static int side_of(const Group *a, const Group *b, TlVec3 eye)
{
    double toward = dot3(a->normal, sub3(eye, a->center)) >= 0 ? 1 : -1;
    bool behind = true, front = true;
    for (size_t i = 0; i < b->outline.count; i++) {
        double d = dot3(a->normal, sub3(b->outline.items[i], a->center)) * toward;
        behind = behind && d <= PLANE_EPS_MM;
        front = front && d >= -PLANE_EPS_MM;
    }
    return behind ? -1 : front ? 1 : 0;
}

typedef struct Box2 {
    double left, top, right, bottom;
    bool valid;
} Box2;

static Box2 screen_box(const Eye *eye, const Points3 *outline)
{
    TlPolygon screen = project_contour(eye, outline);
    Box2 box = {INFINITY, INFINITY, -INFINITY, -INFINITY, screen.count >= 3};
    for (size_t i = 0; i < screen.count; i++) {
        box.left = fmin(box.left, screen.items[i].x);
        box.right = fmax(box.right, screen.items[i].x);
        box.top = fmin(box.top, screen.items[i].y);
        box.bottom = fmax(box.bottom, screen.items[i].y);
    }
    tl_polygon_free(&screen);
    return box;
}

static bool boxes_overlap(Box2 a, Box2 b)
{
    return a.valid && b.valid && a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

/*
 * Порядок рисования видимых поверхностей visible[0..count): пол — первым; дальше «художник» по
 * плоскостям: из двух перекрывающихся на экране поверхностей раньше рисуется та, что за плоскостью
 * другой (стена Г-образной комнаты закрывает предмет в другом её колене, предмет у стены — перед ней).
 * Где плоскости не решают (или по кругу) — дальняя раньше.
 */
static void paint_order(const GroupList *groups, size_t *visible, size_t count, const Eye *eye)
{
    double *distance = malloc(sizeof *distance * (count ? count : 1));
    Box2 *box = malloc(sizeof *box * (count ? count : 1));
    size_t cells = count * count;
    unsigned char *before = calloc(cells ? cells : 1, 1); /* before[i*count+j]: i раньше j */
    int *pending = calloc(count ? count : 1, sizeof *pending);
    bool *done = calloc(count ? count : 1, sizeof *done);
    for (size_t i = 0; i < count; i++) {
        const Group *g = &groups->items[visible[i]];
        TlVec3 to_eye = sub3(eye->position, g->center);
        distance[i] = sqrt(dot3(to_eye, to_eye));
        box[i] = screen_box(eye, &g->outline);
    }
    for (size_t i = 0; i < count; i++)
        for (size_t j = i + 1; j < count; j++) {
            const Group *a = &groups->items[visible[i]], *b = &groups->items[visible[j]];
            if (a->layer == LAYER_FLOOR || b->layer == LAYER_FLOOR || !boxes_overlap(box[i], box[j]))
                continue;
            int side = side_of(a, b, eye->position); /* b относительно плоскости a */
            int order = side < 0 ? 1 : side > 0 ? -1 : 0; /* -1 — a раньше, +1 — b раньше */
            if (!order) {
                side = side_of(b, a, eye->position);
                order = side < 0 ? -1 : side > 0 ? 1 : 0;
            }
            if (order < 0) {
                before[i * count + j] = 1;
                pending[j]++;
            } else if (order > 0) {
                before[j * count + i] = 1;
                pending[i]++;
            }
        }
    size_t *result = malloc(sizeof *result * (count ? count : 1));
    for (size_t n = 0; n < count; n++) {
        size_t pick = SIZE_MAX;
        for (size_t i = 0; i < count; i++) { /* пол; иначе из готовых — дальняя */
            if (done[i])
                continue;
            bool floor = groups->items[visible[i]].layer == LAYER_FLOOR;
            if (floor) {
                pick = i;
                break;
            }
            if (pending[i] == 0 && (pick == SIZE_MAX || distance[i] > distance[pick]))
                pick = i;
        }
        if (pick == SIZE_MAX) /* по кругу — дальняя из оставшихся */
            for (size_t i = 0; i < count; i++)
                if (!done[i] && (pick == SIZE_MAX || distance[i] > distance[pick]))
                    pick = i;
        done[pick] = true;
        result[n] = visible[pick];
        for (size_t j = 0; j < count; j++)
            if (before[pick * count + j] && pending[j] > 0)
                pending[j]--;
    }
    memcpy(visible, result, sizeof *visible * count);
    free(result);
    free(distance);
    free(box);
    free(before);
    free(pending);
    free(done);
}

void tl_room3d_pan(TlCamera *camera, double dx, double dy, double height)
{
    if (!(height > 0) || !(camera->distance > 0))
        return;
    Eye eye = eye_of(camera, height, height);
    double mm = camera->distance / eye.focal; /* мм на пиксель у точки, куда смотрим */
    camera->target = vec3(camera->target.x - (eye.right.x * dx - eye.up.x * dy) * mm,
                          camera->target.y - (eye.right.y * dx - eye.up.y * dy) * mm,
                          camera->target.z - (eye.right.z * dx - eye.up.z * dy) * mm);
}

void tl_paint_list_free_all(TlPaintList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        tl_polygon_list_free_all(&list->items[i].contours);
        free(list->items[i].cells);
    }
    tl_paint_list_free(list);
}

#define CELL_PX 48.0  /* клетка рисунка — не больше стольких пикселей по стороне */
#define MAX_CELLS 10  /* по каждой стороне плитки */

static bool project_point(const Eye *eye, TlVec3 p, TlVec2 *out)
{
    TlVec3 d = sub3(p, eye->position);
    double z = dot3(d, eye->forward);
    if (z < NEAR_MM)
        return false;
    *out = tl_vec2(eye->cx + dot3(d, eye->right) / z * eye->focal, eye->cy - dot3(d, eye->up) / z * eye->focal);
    return true;
}

/* Рисунок плитки по клеткам: точка плитки (s, t ∈ 0…1) — corner[0] + s·(corner[1] − corner[0]) +
   t·(corner[2] − corner[0]). Клетка за камерой не рисуется (там видна заливка цветом). */
static void texture_cells(const Eye *eye, const Face *face, TlPaint *paint)
{
    TlVec3 o = face->corner[0], u = sub3(face->corner[1], o), v = sub3(face->corner[2], o);
    TlVec3 far = vec3(o.x + u.x + v.x, o.y + u.y + v.y, o.z + u.z + v.z);
    TlVec2 p[4];
    bool all = project_point(eye, o, &p[0]) && project_point(eye, face->corner[1], &p[1]) &&
               project_point(eye, face->corner[2], &p[2]) && project_point(eye, far, &p[3]);
    int ns = MAX_CELLS, nt = MAX_CELLS;
    if (all) { /* клеток — по размеру плитки на экране */
        double ls = fmax(tl_distance(p[0], p[1]), tl_distance(p[2], p[3]));
        double lt = fmax(tl_distance(p[0], p[2]), tl_distance(p[1], p[3]));
        ns = (int)fmax(1, fmin(MAX_CELLS, ceil(ls / CELL_PX)));
        nt = (int)fmax(1, fmin(MAX_CELLS, ceil(lt / CELL_PX)));
    }
    paint->cells = malloc(sizeof *paint->cells * (size_t)(ns * nt));
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < nt; j++) {
            double s0 = (double)i / ns, s1 = (double)(i + 1) / ns, t0 = (double)j / nt, t1 = (double)(j + 1) / nt;
            const double st[4][2] = {{s0, t0}, {s1, t0}, {s1, t1}, {s0, t1}};
            TlTextureCell cell;
            bool in_front = true;
            for (int k = 0; k < 4 && in_front; k++) {
                TlVec3 point = vec3(o.x + u.x * st[k][0] + v.x * st[k][1], o.y + u.y * st[k][0] + v.y * st[k][1],
                                    o.z + u.z * st[k][0] + v.z * st[k][1]);
                in_front = project_point(eye, point, &cell.quad[k]);
            }
            if (!in_front)
                continue;
            /* квадрат 0…1 → экран так, чтобы углы клетки (s0,t0), (s1,t0), (s0,t1) попали на свои места */
            double ds = s1 - s0, dt = t1 - t0;
            double a = (cell.quad[1].x - cell.quad[0].x) / ds, b = (cell.quad[1].y - cell.quad[0].y) / ds;
            double c = (cell.quad[3].x - cell.quad[0].x) / dt, d = (cell.quad[3].y - cell.quad[0].y) / dt;
            const double matrix[6] = {a, b, c, d, cell.quad[0].x - a * s0 - c * t0, cell.quad[0].y - b * s0 - d * t0};
            memcpy(cell.matrix, matrix, sizeof matrix);
            paint->cells[paint->cell_count++] = cell;
        }
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
    size_t *order = malloc(sizeof *order * (groups.count ? groups.count : 1));
    size_t count = 0;
    for (size_t g = 0; g < groups.count; g++) {
        const Group *group = &groups.items[g];
        TlVec3 to_eye = sub3(eye.position, group->center);
        if (group->layer != LAYER_FLOOR && dot3(group->normal, to_eye) <= 0) /* спиной к зрителю: ближние стены, задние грани */
            continue;
        order[count++] = g;
    }
    paint_order(&groups, order, count, &eye);
    for (size_t o = 0; o < count; o++) {
        const Group *group = &groups.items[order[o]];
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
            paint.shade = shade;
            if (face->texture) { /* рисунок по клеткам; за камерой — цветом */
                texture_cells(&eye, face, &paint);
                if (paint.cell_count) {
                    snprintf(paint.texture, sizeof paint.texture, "%s", face->texture);
                    paint.tile_width = face->tile_width;
                    paint.tile_height = face->tile_height;
                } else {
                    free(paint.cells);
                    paint.cells = NULL;
                }
            }
            if (face->stroked)
                hex_color(paint.stroke, face->stroke_rgb, shade);
            tl_paint_list_push(&paints, paint);
        }
    }
    free(order);
    groups_free(&groups);
    return paints;
}

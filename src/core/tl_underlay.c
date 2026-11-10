#include "tl_underlay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"

#define IMAGE_SCALE 10.0    /* мм на пиксель картинки до калибровки */
#define MAX_DEPTH 6         /* вложенность блоков */
#define ARC_STEP_DEGREES 10.0

TlUnderlay tl_underlay_default(void)
{
    return (TlUnderlay){.scale = 1, .opacity = 0.5, .visible = true};
}

bool tl_underlay_present(const TlUnderlay *underlay) { return underlay->image || underlay->lines.count > 0; }

void tl_underlay_clear(TlUnderlay *underlay)
{
    free(underlay->image);
    tl_polygon_list_free_all(&underlay->lines);
    *underlay = tl_underlay_default();
}

TlVec2 tl_underlay_to_plan(const TlUnderlay *underlay, TlVec2 point)
{
    return tl_vec2(underlay->origin.x + point.x * underlay->scale, underlay->origin.y + point.y * underlay->scale);
}

TlVec2 tl_underlay_from_plan(const TlUnderlay *underlay, TlVec2 point)
{
    return tl_vec2((point.x - underlay->origin.x) / underlay->scale, (point.y - underlay->origin.y) / underlay->scale);
}

TlRect tl_underlay_bounds(const TlUnderlay *underlay)
{
    TlRect local = {0, 0, 0, 0};
    if (underlay->image) {
        local = (TlRect){0, 0, underlay->image_width, underlay->image_height};
    } else if (underlay->lines.count) {
        local = (TlRect){INFINITY, INFINITY, -INFINITY, -INFINITY};
        for (size_t i = 0; i < underlay->lines.count; i++) {
            TlRect bounds = tl_polygon_bounds(&underlay->lines.items[i]);
            local = (TlRect){fmin(local.left, bounds.left), fmin(local.top, bounds.top), fmax(local.right, bounds.right),
                             fmax(local.bottom, bounds.bottom)};
        }
    }
    TlVec2 a = tl_underlay_to_plan(underlay, tl_vec2(local.left, local.top));
    TlVec2 b = tl_underlay_to_plan(underlay, tl_vec2(local.right, local.bottom));
    return (TlRect){a.x, a.y, b.x, b.y};
}

bool tl_underlay_set_image(TlUnderlay *underlay, const char *data_url, int width, int height)
{
    if (!data_url || strncmp(data_url, "data:image/", 11) != 0 || width <= 0 || height <= 0)
        return false;
    tl_underlay_clear(underlay);
    underlay->image = tl_strdup(data_url);
    underlay->image_width = width;
    underlay->image_height = height;
    underlay->scale = IMAGE_SCALE;
    underlay->placed = true;
    return true;
}

bool tl_underlay_calibrate(TlUnderlay *underlay, TlVec2 a, TlVec2 b, double distance)
{
    double measured = tl_distance(a, b);
    if (measured < 1e-6 || !(distance > 0) || !isfinite(distance))
        return false;
    TlVec2 local = tl_underlay_from_plan(underlay, a);
    underlay->scale *= distance / measured;
    underlay->origin = tl_vec2(a.x - local.x * underlay->scale, a.y - local.y * underlay->scale);
    return true;
}

TlUnderlay tl_underlay_take(TlUnderlay *underlay)
{
    TlUnderlay taken = *underlay;
    *underlay = tl_underlay_default();
    return taken;
}

void tl_underlay_restore(TlUnderlay *underlay, TlUnderlay *taken)
{
    TlUnderlay place = underlay->placed ? *underlay : *taken;
    tl_polygon_list_free_all(&underlay->lines);
    free(underlay->image);
    *underlay = *taken;
    underlay->origin = place.origin;
    underlay->scale = place.scale;
    underlay->opacity = place.opacity;
    underlay->visible = place.visible;
    underlay->placed = place.placed;
    *taken = tl_underlay_default();
}

/* ---- DXF ------------------------------------------------------------------------- */

typedef struct Pair {
    int code;
    const char *value; /* в общем буфере */
} Pair;

TL_ARRAY_DEFINE(PairList, Pair, pair_list)

typedef struct Insert {
    char name[128];
    TlVec2 at, scale;
    double angle; /* ° */
} Insert;

TL_ARRAY_DEFINE(InsertList, Insert, insert_list)

typedef struct Block {
    char name[128];
    TlVec2 base;
    TlPolygonList lines;
    InsertList inserts;
} Block;

TL_ARRAY_DEFINE(BlockList, Block, block_list)

typedef struct Reader {
    PairList pairs;
    size_t at;
    size_t points;
    bool too_big;
} Reader;

/* Текст — в пары «код — значение» (строки текста режутся на месте). */
static bool split_pairs(char *text, PairList *pairs)
{
    char *line = text;
    int code = 0;
    bool want_code = true;
    while (*line) {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        size_t length = strlen(line);
        if (length && line[length - 1] == '\r')
            line[length - 1] = '\0';
        if (want_code) {
            char *after;
            code = (int)strtol(line, &after, 10);
            if (after == line)
                return false;
        } else {
            while (*line == ' ')
                line++;
            pair_list_push(pairs, (Pair){code, line});
        }
        want_code = !want_code;
        if (!end)
            break;
        line = end + 1;
    }
    return pairs->count > 0;
}

static double number(const char *value) { return strtod(value, NULL); }

static void add_point(Reader *reader, TlPolygon *polygon, TlVec2 point)
{
    if (reader->points >= TL_UNDERLAY_MAX_POINTS) {
        reader->too_big = true;
        return;
    }
    reader->points++;
    tl_polygon_push(polygon, point);
}

static void add_arc(Reader *reader, TlPolygon *polygon, TlVec2 center, double radius, double from, double to)
{
    while (to <= from)
        to += 360;
    int steps = (int)fmax(2, ceil((to - from) / ARC_STEP_DEGREES));
    for (int i = 0; i <= steps; i++) {
        double angle = (from + (to - from) * i / steps) * TL_PI / 180;
        add_point(reader, polygon, tl_vec2(center.x + radius * cos(angle), center.y + radius * sin(angle)));
    }
}

/* Сторона ломаной с выпуклостью bulge (tg четверти угла дуги): дуга от a до b без точки a. */
static void add_bulge(Reader *reader, TlPolygon *polygon, TlVec2 a, TlVec2 b, double bulge)
{
    if (fabs(bulge) < 1e-9) {
        add_point(reader, polygon, b);
        return;
    }
    double sweep = 4 * atan(bulge), chord = tl_distance(a, b);
    if (chord < 1e-12) {
        add_point(reader, polygon, b);
        return;
    }
    double radius = chord / (2 * sin(fabs(sweep) / 2));
    TlVec2 middle = {(a.x + b.x) / 2, (a.y + b.y) / 2}, direction = {(b.x - a.x) / chord, (b.y - a.y) / chord};
    double offset = sqrt(fmax(0, radius * radius - chord * chord / 4)) * (fabs(sweep) > TL_PI ? -1 : 1);
    TlVec2 normal = bulge > 0 ? tl_vec2(-direction.y, direction.x) : tl_vec2(direction.y, -direction.x);
    TlVec2 center = {middle.x + normal.x * offset, middle.y + normal.y * offset};
    double start = atan2(a.y - center.y, a.x - center.x);
    int steps = (int)fmax(2, ceil(fabs(sweep) * 180 / TL_PI / ARC_STEP_DEGREES));
    for (int i = 1; i <= steps; i++) {
        double angle = start + sweep * i / steps;
        add_point(reader, polygon, tl_vec2(center.x + radius * cos(angle), center.y + radius * sin(angle)));
    }
}

static void finish_line(Reader *reader, Block *block, TlPolygon *polygon, bool closed)
{
    if (closed && polygon->count > 2)
        add_point(reader, polygon, polygon->items[0]);
    if (polygon->count >= 2)
        tl_polygon_list_push(&block->lines, *polygon);
    else
        tl_polygon_free(polygon);
}

/* Значения одной сущности: пары от reader->at до следующего кода 0. */
typedef struct Entity {
    const char *type;
    size_t first, end;
} Entity;

static Entity next_entity(Reader *reader)
{
    Entity entity = {reader->pairs.items[reader->at].value, reader->at + 1, reader->at + 1};
    while (entity.end < reader->pairs.count && reader->pairs.items[entity.end].code != 0)
        entity.end++;
    reader->at = entity.end;
    return entity;
}

static const char *value_of(const Reader *reader, const Entity *entity, int code, const char *fallback)
{
    for (size_t i = entity->first; i < entity->end; i++)
        if (reader->pairs.items[i].code == code)
            return reader->pairs.items[i].value;
    return fallback;
}

static double number_of(const Reader *reader, const Entity *entity, int code, double fallback)
{
    const char *value = value_of(reader, entity, code, NULL);
    return value ? number(value) : fallback;
}

static bool is(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void read_lwpolyline(Reader *reader, const Entity *entity, Block *block)
{
    TlPolygon polygon = {0};
    bool closed = ((int)number_of(reader, entity, 70, 0) & 1) != 0;
    double bulge = 0;
    TlVec2 last = {0, 0};
    bool have = false;
    for (size_t i = entity->first; i < entity->end; i++) {
        const Pair *pair = &reader->pairs.items[i];
        if (pair->code == 10 && i + 1 < entity->end && reader->pairs.items[i + 1].code == 20) {
            TlVec2 point = {number(pair->value), number(reader->pairs.items[i + 1].value)};
            if (have)
                add_bulge(reader, &polygon, last, point, bulge);
            else
                add_point(reader, &polygon, point);
            last = point;
            have = true;
            bulge = 0;
        } else if (pair->code == 42) {
            bulge = number(pair->value);
        }
    }
    if (closed && have && polygon.count > 1)
        add_bulge(reader, &polygon, last, polygon.items[0], bulge);
    finish_line(reader, block, &polygon, false);
}

/* POLYLINE и её VERTEX до SEQEND. */
static void read_polyline(Reader *reader, const Entity *entity, Block *block)
{
    int flags = (int)number_of(reader, entity, 70, 0);
    bool closed = (flags & 1) != 0, mesh = (flags & (16 | 64)) != 0;
    TlPolygon polygon = {0};
    TlVec2 last = {0, 0};
    double bulge = 0;
    bool have = false;
    while (reader->at < reader->pairs.count) {
        Entity vertex = next_entity(reader);
        if (!is(vertex.type, "VERTEX"))
            break;
        TlVec2 point = {number_of(reader, &vertex, 10, 0), number_of(reader, &vertex, 20, 0)};
        if (have)
            add_bulge(reader, &polygon, last, point, bulge);
        else
            add_point(reader, &polygon, point);
        bulge = number_of(reader, &vertex, 42, 0);
        last = point;
        have = true;
    }
    if (mesh) { /* сетки и грани — не план */
        tl_polygon_free(&polygon);
        return;
    }
    if (closed && have && polygon.count > 1)
        add_bulge(reader, &polygon, last, polygon.items[0], bulge);
    finish_line(reader, block, &polygon, false);
}

static void read_entity(Reader *reader, const Entity *entity, Block *block)
{
    if (number_of(reader, entity, 67, 0) == 1) /* лист, не модель */
        return;
    const char *type = entity->type;
    TlPolygon polygon = {0};
    if (is(type, "LINE")) {
        add_point(reader, &polygon, tl_vec2(number_of(reader, entity, 10, 0), number_of(reader, entity, 20, 0)));
        add_point(reader, &polygon, tl_vec2(number_of(reader, entity, 11, 0), number_of(reader, entity, 21, 0)));
        finish_line(reader, block, &polygon, false);
    } else if (is(type, "LWPOLYLINE")) {
        read_lwpolyline(reader, entity, block);
    } else if (is(type, "POLYLINE")) {
        read_polyline(reader, entity, block);
    } else if (is(type, "CIRCLE") || is(type, "ARC")) {
        TlVec2 center = {number_of(reader, entity, 10, 0), number_of(reader, entity, 20, 0)};
        double radius = number_of(reader, entity, 40, 0);
        bool circle = is(type, "CIRCLE");
        if (radius > 0)
            add_arc(reader, &polygon, center, radius, circle ? 0 : number_of(reader, entity, 50, 0),
                    circle ? 360 : number_of(reader, entity, 51, 360));
        finish_line(reader, block, &polygon, false);
    } else if (is(type, "ELLIPSE")) {
        TlVec2 center = {number_of(reader, entity, 10, 0), number_of(reader, entity, 20, 0)};
        TlVec2 major = {number_of(reader, entity, 11, 0), number_of(reader, entity, 21, 0)};
        double ratio = number_of(reader, entity, 40, 1), from = number_of(reader, entity, 41, 0);
        double to = number_of(reader, entity, 42, 2 * TL_PI);
        while (to <= from)
            to += 2 * TL_PI;
        TlVec2 minor = {-major.y * ratio, major.x * ratio};
        int steps = (int)fmax(4, ceil((to - from) * 180 / TL_PI / ARC_STEP_DEGREES));
        for (int i = 0; i <= steps; i++) {
            double t = from + (to - from) * i / steps;
            add_point(reader, &polygon, tl_vec2(center.x + major.x * cos(t) + minor.x * sin(t),
                                                center.y + major.y * cos(t) + minor.y * sin(t)));
        }
        finish_line(reader, block, &polygon, false);
    } else if (is(type, "SPLINE")) { /* по точкам, через которые проходит, иначе по управляющим */
        int code = value_of(reader, entity, 11, NULL) ? 11 : 10;
        for (size_t i = entity->first; i + 1 < entity->end; i++)
            if (reader->pairs.items[i].code == code && reader->pairs.items[i + 1].code == code + 10)
                add_point(reader, &polygon,
                          tl_vec2(number(reader->pairs.items[i].value), number(reader->pairs.items[i + 1].value)));
        finish_line(reader, block, &polygon, ((int)number_of(reader, entity, 70, 0) & 1) != 0);
    } else if (is(type, "INSERT")) {
        Insert insert = {.at = {number_of(reader, entity, 10, 0), number_of(reader, entity, 20, 0)},
                         .scale = {number_of(reader, entity, 41, 1), number_of(reader, entity, 42, 1)},
                         .angle = number_of(reader, entity, 50, 0)};
        snprintf(insert.name, sizeof insert.name, "%s", value_of(reader, entity, 2, ""));
        insert_list_push(&block->inserts, insert);
    }
}

/* Сущности до конца раздела (ENDSEC) или блока (ENDBLK). */
static void read_entities(Reader *reader, Block *block)
{
    while (reader->at < reader->pairs.count) {
        const Pair *pair = &reader->pairs.items[reader->at];
        if (pair->code != 0) {
            reader->at++;
            continue;
        }
        if (is(pair->value, "ENDSEC") || is(pair->value, "ENDBLK"))
            return;
        Entity entity = next_entity(reader);
        read_entity(reader, &entity, block);
    }
}

static void block_free(Block *block)
{
    tl_polygon_list_free_all(&block->lines);
    insert_list_free(&block->inserts);
}

static const Block *find_block(const BlockList *blocks, const char *name)
{
    for (size_t i = 0; i < blocks->count; i++)
        if (strcmp(blocks->items[i].name, name) == 0)
            return &blocks->items[i];
    return NULL;
}

/* Перенос в чертёж: точка блока p → insert.at + поворот(масштаб · (p − base)). */
typedef struct Placement {
    double a, b, c, d, tx, ty;
} Placement;

static TlVec2 place(const Placement *m, TlVec2 p) { return tl_vec2(m->a * p.x + m->c * p.y + m->tx, m->b * p.x + m->d * p.y + m->ty); }

static void emit(Reader *reader, const BlockList *blocks, const Block *block, const Placement *m, int depth,
                 TlPolygonList *out)
{
    for (size_t i = 0; i < block->lines.count; i++) {
        const TlPolygon *line = &block->lines.items[i];
        TlPolygon placed = {0};
        for (size_t k = 0; k < line->count; k++)
            tl_polygon_push(&placed, place(m, line->items[k]));
        tl_polygon_list_push(out, placed);
    }
    if (depth >= MAX_DEPTH)
        return;
    for (size_t i = 0; i < block->inserts.count; i++) {
        const Insert *insert = &block->inserts.items[i];
        const Block *inner = find_block(blocks, insert->name);
        if (!inner)
            continue;
        double angle = insert->angle * TL_PI / 180, cs = cos(angle), sn = sin(angle);
        /* local: p → R·S·(p − base) + at */
        Placement local = {cs * insert->scale.x, sn * insert->scale.x, -sn * insert->scale.y, cs * insert->scale.y, 0, 0};
        local.tx = insert->at.x - (local.a * inner->base.x + local.c * inner->base.y);
        local.ty = insert->at.y - (local.b * inner->base.x + local.d * inner->base.y);
        Placement combined = {m->a * local.a + m->c * local.b, m->b * local.a + m->d * local.b,
                              m->a * local.c + m->c * local.d, m->b * local.c + m->d * local.d,
                              m->a * local.tx + m->c * local.ty + m->tx, m->b * local.tx + m->d * local.ty + m->ty};
        for (size_t k = 0; k < inner->lines.count; k++) /* копии блока — тоже точки */
            reader->points += inner->lines.items[k].count;
        if (reader->points > TL_UNDERLAY_MAX_POINTS) {
            reader->too_big = true;
            return;
        }
        emit(reader, blocks, inner, &combined, depth + 1, out);
    }
}

static double unit_to_mm(int units)
{
    switch (units) {
    case 1: return 25.4;
    case 2: return 304.8;
    case 4: return 1;
    case 5: return 10;
    case 6: return 1000;
    case 14: return 100; /* дециметры */
    default: return 0;
    }
}

bool tl_underlay_load_dxf(TlUnderlay *underlay, const char *text, char *error, size_t error_size)
{
    if (!text || strncmp(text, "AutoCAD Binary DXF", 18) == 0) {
        snprintf(error, error_size, "%s", "двоичный DXF не поддерживается — сохраните чертёж как текстовый DXF");
        return false;
    }
    char *copy = tl_strdup(text);
    Reader reader = {0};
    if (!split_pairs(copy, &reader.pairs)) {
        free(copy);
        snprintf(error, error_size, "%s", "файл не похож на DXF");
        return false;
    }
    BlockList blocks = {0};
    Block model = {.name = "*MODEL"};
    int units = 0;
    while (reader.at < reader.pairs.count) {
        const Pair *pair = &reader.pairs.items[reader.at];
        if (pair->code == 9 && is(pair->value, "$INSUNITS") && reader.at + 1 < reader.pairs.count) {
            units = (int)number(reader.pairs.items[reader.at + 1].value);
            reader.at += 2;
        } else if (pair->code == 0 && is(pair->value, "BLOCK")) {
            Entity header = next_entity(&reader);
            Block block = {.base = {number_of(&reader, &header, 10, 0), number_of(&reader, &header, 20, 0)}};
            snprintf(block.name, sizeof block.name, "%s", value_of(&reader, &header, 2, ""));
            read_entities(&reader, &block);
            block_list_push(&blocks, block);
        } else if (pair->code == 2 && is(pair->value, "ENTITIES")) {
            reader.at++;
            read_entities(&reader, &model);
        } else {
            reader.at++;
        }
    }
    TlPolygonList lines = {0};
    Placement identity = {1, 0, 0, 1, 0, 0};
    emit(&reader, &blocks, &model, &identity, 0, &lines);
    block_free(&model);
    for (size_t i = 0; i < blocks.count; i++)
        block_free(&blocks.items[i]);
    block_list_free(&blocks);
    pair_list_free(&reader.pairs);
    free(copy);
    if (reader.too_big || lines.count == 0) {
        tl_polygon_list_free_all(&lines);
        snprintf(error, error_size, "%s",
                 reader.too_big ? "чертёж слишком большой для подложки — оставьте в нём только план"
                                : "в чертеже нет линий (отрезков, ломаных, дуг)");
        return false;
    }
    /* в мм, ось Y — вниз, угол габарита — в (0, 0) */
    TlRect bounds = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (size_t i = 0; i < lines.count; i++)
        for (size_t k = 0; k < lines.items[i].count; k++) {
            TlVec2 p = lines.items[i].items[k];
            bounds = (TlRect){fmin(bounds.left, p.x), fmin(bounds.top, -p.y), fmax(bounds.right, p.x),
                              fmax(bounds.bottom, -p.y)};
        }
    double factor = unit_to_mm(units);
    if (factor == 0) /* без единиц: план меньше 300 единиц — в метрах */
        factor = fmax(tl_rect_width(bounds), tl_rect_height(bounds)) < 300 ? 1000 : 1;
    for (size_t i = 0; i < lines.count; i++)
        for (size_t k = 0; k < lines.items[i].count; k++) {
            TlVec2 *p = &lines.items[i].items[k];
            *p = tl_vec2((p->x - bounds.left) * factor, (-p->y - bounds.top) * factor);
        }
    tl_underlay_clear(underlay);
    underlay->lines = lines;
    underlay->scale = 1;
    underlay->opacity = 0.8;
    underlay->placed = true;
    return true;
}

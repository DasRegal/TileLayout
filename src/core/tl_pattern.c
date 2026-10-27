#include "tl_pattern.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- модули ------------------------------------------------------------------- */

static int add_spec(TlPatternUnit *unit, double width, double height, double laid_width, double laid_height,
                    const char *color, bool textured)
{
    for (size_t i = 0; i < unit->spec_count; i++) {
        const TlTileSpec *spec = &unit->specs[i];
        bool same_size = (fabs(spec->width - width) < 0.05 && fabs(spec->height - height) < 0.05) ||
                         (fabs(spec->width - height) < 0.05 && fabs(spec->height - width) < 0.05);
        if (same_size && strcmp(spec->color, color) == 0 && spec->textured == textured)
            return (int)i;
    }
    TlTileSpec *spec = &unit->specs[unit->spec_count];
    *spec = (TlTileSpec){width, height, laid_width, laid_height, "", textured};
    snprintf(spec->color, sizeof spec->color, "%s", color);
    return (int)unit->spec_count++;
}

static void add_tile(TlPatternUnit *unit, TlRect rect, int spec)
{
    unit->tiles[unit->tile_count++] = (TlPatternTile){rect, spec};
}

static void main_spec(TlPatternUnit *unit, const TlTileSettings *s)
{
    add_spec(unit, s->tile_width, s->tile_height, s->tile_width, s->tile_height, s->color, true);
}

/* Прямая: ряды со сдвигом на долю плитки. */
static void straight_unit(const TlTileSettings *s, TlPatternUnit *unit)
{
    double pitch_x = s->tile_width + s->joint_width, pitch_y = s->tile_height + s->joint_width;
    main_spec(unit, s);
    add_tile(unit, (TlRect){0, 0, s->tile_width, s->tile_height}, 0);
    unit->a = tl_vec2(pitch_x, 0);
    unit->b = tl_vec2(s->row_shift * pitch_x, pitch_y);
}

/* Шахматка: плитки двух цветов через одну, в следующем ряду — наоборот. */
static void checker_unit(const TlTileSettings *s, TlPatternUnit *unit)
{
    double pitch_x = s->tile_width + s->joint_width, pitch_y = s->tile_height + s->joint_width;
    main_spec(unit, s);
    int second = add_spec(unit, s->tile_width, s->tile_height, s->tile_width, s->tile_height, s->color2, false);
    add_tile(unit, (TlRect){0, 0, s->tile_width, s->tile_height}, 0);
    add_tile(unit, (TlRect){pitch_x, 0, pitch_x + s->tile_width, s->tile_height}, second);
    unit->a = tl_vec2(2 * pitch_x, 0);
    unit->b = tl_vec2(pitch_x, pitch_y);
}

/* Ёлочка: плитка вдоль ряда, за её торцом — поперёк; пары идут лесенкой.
 *     a = (W + шов, W + шов), b = (L + шов, −(L + шов)),
 * где L и W — длинная и короткая стороны: у каждого шва ровно шов. */
static void herringbone_unit(const TlTileSettings *s, TlPatternUnit *unit)
{
    double long_side = fmax(s->tile_width, s->tile_height), short_side = fmin(s->tile_width, s->tile_height);
    double gap = s->joint_width;
    main_spec(unit, s);
    add_tile(unit, (TlRect){0, 0, long_side, short_side}, 0);
    add_tile(unit, (TlRect){long_side + gap, short_side - long_side, long_side + gap + short_side, short_side}, 0);
    unit->a = tl_vec2(short_side + gap, short_side + gap);
    unit->b = tl_vec2(long_side + gap, -(long_side + gap));
}

/* Модульные рисунки: плитки и шаги решётки — в клетках. */
typedef struct ModuleTile {
    int x, y, width, height;
} ModuleTile;

typedef struct Module {
    int ax, ay, bx, by;
    size_t count;
    ModuleTile tiles[TL_PATTERN_MAX_TILES];
} Module;

/* Хопскотч: большой квадрат 2×2 и маленький 1×1 по решётке (2, 1), (−1, 2) — без сквозных швов. */
static const Module HOPSCOTCH = {2, 1, -1, 2, 2, {{0, 0, 2, 2}, {2, 0, 1, 1}}};

/* Три формата: квадрат 3×3 из плиток 2×2, 1×2, 2×1, 1×1 и ещё 1×1 сбоку; решётка
   (3, 1), (−1, 3) — как у хопскотча, сквозных швов нет. */
static const Module THREE_FORMATS = {
    3, 1, -1, 3, 5, {{0, 0, 2, 2}, {2, 0, 1, 2}, {0, 2, 2, 1}, {2, 2, 1, 1}, {3, 0, 1, 1}}};

/* Версаль: квадрат 4×4 из плиток 3×2, 1×2, 2×2, 2×1, 1×1 ×2 и ещё 1×1 сбоку; решётка
   (4, 1), (−1, 4). На клетке 200×200 — 600×400, 400×400, 400×200, 200×200. */
static const Module VERSAILLES = {
    4, 1, -1, 4, 7,
    {{0, 0, 3, 2}, {3, 0, 1, 2}, {0, 2, 2, 2}, {2, 2, 2, 1}, {2, 3, 1, 1}, {3, 3, 1, 1}, {4, 0, 1, 1}}};

static void module_unit(const TlTileSettings *s, const Module *module, TlPatternUnit *unit)
{
    double gap = s->joint_width;
    double pitch_x = s->tile_width + gap, pitch_y = s->tile_height + gap;
    for (size_t i = 0; i < module->count; i++) {
        const ModuleTile *t = &module->tiles[i];
        double laid_width = t->width * pitch_x - gap, laid_height = t->height * pitch_y - gap;
        int spec = add_spec(unit, t->width * s->tile_width, t->height * s->tile_height, laid_width, laid_height,
                            s->color, true);
        add_tile(unit, (TlRect){t->x * pitch_x, t->y * pitch_y, t->x * pitch_x + laid_width, t->y * pitch_y + laid_height},
                 spec);
    }
    unit->a = tl_vec2(module->ax * pitch_x, module->ay * pitch_y);
    unit->b = tl_vec2(module->bx * pitch_x, module->by * pitch_y);
}

static void hopscotch_unit(const TlTileSettings *s, TlPatternUnit *unit) { module_unit(s, &HOPSCOTCH, unit); }
static void three_formats_unit(const TlTileSettings *s, TlPatternUnit *unit) { module_unit(s, &THREE_FORMATS, unit); }
static void versailles_unit(const TlTileSettings *s, TlPatternUnit *unit) { module_unit(s, &VERSAILLES, unit); }

/* ---- таблица рисунков ----------------------------------------------------------- */

typedef struct PatternEntry {
    TlPatternInfo info;
    void (*build)(const TlTileSettings *settings, TlPatternUnit *unit);
} PatternEntry;

static const PatternEntry PATTERNS[] = {
    {{"straight", "Прямая", true, false, false}, straight_unit},
    {{"checker", "Шахматка", false, true, false}, checker_unit},
    {{"herringbone", "Ёлочка", false, false, false}, herringbone_unit},
    {{"hopscotch", "Модульная: 2 формата", false, false, true}, hopscotch_unit},
    {{"modular3", "Модульная: 3 формата", false, false, true}, three_formats_unit},
    {{"versailles", "Версаль: 4 формата", false, false, true}, versailles_unit},
};

#define PATTERN_COUNT (sizeof PATTERNS / sizeof PATTERNS[0])

static const PatternEntry *entry_of(const TlTileSettings *settings)
{
    for (size_t i = 0; i < PATTERN_COUNT; i++)
        if (strcmp(PATTERNS[i].info.id, settings->pattern) == 0)
            return &PATTERNS[i];
    return &PATTERNS[0];
}

const TlPatternInfo *tl_patterns(size_t *count)
{
    static TlPatternInfo infos[PATTERN_COUNT];
    for (size_t i = 0; i < PATTERN_COUNT; i++)
        infos[i] = PATTERNS[i].info;
    *count = PATTERN_COUNT;
    return infos;
}

const TlPatternInfo *tl_pattern_of(const TlTileSettings *settings)
{
    size_t count;
    const TlPatternInfo *infos = tl_patterns(&count);
    return &infos[entry_of(settings) - PATTERNS];
}

bool tl_pattern_unit(const TlTileSettings *settings, TlPatternUnit *unit)
{
    *unit = (TlPatternUnit){0};
    if (!(settings->tile_width > 0 && settings->tile_height > 0) ||
        !(settings->tile_width + settings->joint_width > 0 && settings->tile_height + settings->joint_width > 0))
        return false;
    entry_of(settings)->build(settings, unit);
    double det = unit->a.x * unit->b.y - unit->a.y * unit->b.x;
    return fabs(det) > 1e-6;
}

/* ---- обход ---------------------------------------------------------------------- */

/* Доли шагов a и b точки p: p = s·a + t·b. */
static void lattice_coords(const TlPatternUnit *unit, TlVec2 p, double *s, double *t)
{
    double det = unit->a.x * unit->b.y - unit->a.y * unit->b.x;
    *s = (p.x * unit->b.y - p.y * unit->b.x) / det;
    *t = (unit->a.x * p.y - unit->a.y * p.x) / det;
}

static TlRect unit_extent(const TlPatternUnit *unit)
{
    TlRect extent = unit->tiles[0].rect;
    for (size_t i = 1; i < unit->tile_count; i++) {
        TlRect r = unit->tiles[i].rect;
        extent = (TlRect){fmin(extent.left, r.left), fmin(extent.top, r.top), fmax(extent.right, r.right),
                          fmax(extent.bottom, r.bottom)};
    }
    return extent;
}

/* Где может стоять начало модуля, чтобы его плитки задели bounds. */
static TlRect origin_box(const TlPatternUnit *unit, TlRect bounds)
{
    TlRect extent = unit_extent(unit);
    return (TlRect){bounds.left - extent.right, bounds.top - extent.bottom, bounds.right - extent.left,
                    bounds.bottom - extent.top};
}

static void index_range(const TlPatternUnit *unit, TlRect box, int *i0, int *i1, int *j0, int *j1)
{
    const TlVec2 corners[] = {{box.left, box.top}, {box.right, box.top}, {box.left, box.bottom},
                              {box.right, box.bottom}};
    double s_min = INFINITY, s_max = -INFINITY, t_min = INFINITY, t_max = -INFINITY;
    for (size_t k = 0; k < 4; k++) {
        double s, t;
        lattice_coords(unit, corners[k], &s, &t);
        s_min = fmin(s_min, s), s_max = fmax(s_max, s), t_min = fmin(t_min, t), t_max = fmax(t_max, t);
    }
    *i0 = (int)floor(s_min), *i1 = (int)ceil(s_max), *j0 = (int)floor(t_min), *j1 = (int)ceil(t_max);
}

static bool lies_lengthwise(double width, double height) { return width >= height; }

void tl_pattern_cover(const TlPatternUnit *unit, TlVec2 offset, TlRect bounds, TlPatternVisitor visit,
                      void *context)
{
    TlRect box = origin_box(unit, bounds);
    box = (TlRect){box.left - offset.x, box.top - offset.y, box.right - offset.x, box.bottom - offset.y};
    int i0, i1, j0, j1;
    index_range(unit, box, &i0, &i1, &j0, &j1);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            TlVec2 origin = {offset.x + i * unit->a.x + j * unit->b.x, offset.y + i * unit->a.y + j * unit->b.y};
            for (size_t k = 0; k < unit->tile_count; k++) {
                const TlPatternTile *tile = &unit->tiles[k];
                TlRect rect = {origin.x + tile->rect.left, origin.y + tile->rect.top, origin.x + tile->rect.right,
                               origin.y + tile->rect.bottom};
                if (!tl_rect_intersects(rect, bounds))
                    continue;
                const TlTileSpec *spec = &unit->specs[tile->spec];
                bool square = fabs(spec->laid_width - spec->laid_height) < 0.5;
                TlPlacedTile placed = {rect, {i, j, (int)k}, tile->spec,
                                       !square && lies_lengthwise(tl_rect_width(rect), tl_rect_height(rect)) !=
                                                      lies_lengthwise(spec->laid_width, spec->laid_height)};
                visit(&placed, context);
            }
        }
}

double tl_pattern_estimate(const TlPatternUnit *unit, TlRect bounds)
{
    TlRect box = origin_box(unit, bounds);
    double det = fabs(unit->a.x * unit->b.y - unit->a.y * unit->b.x);
    /* перекос решётки добавляет модули по краям — с запасом на них */
    double area = (tl_rect_width(box) + tl_vec2_length(unit->a) + tl_vec2_length(unit->b)) *
                  (tl_rect_height(box) + tl_vec2_length(unit->a) + tl_vec2_length(unit->b));
    return area / det * (double)unit->tile_count;
}

TlVec2 tl_pattern_lattice_point(const TlPatternUnit *unit, double s, double t)
{
    return tl_vec2(s * unit->a.x + t * unit->b.x, s * unit->a.y + t * unit->b.y);
}

TlVec2 tl_pattern_reduce(const TlPatternUnit *unit, TlVec2 offset)
{
    double s, t;
    lattice_coords(unit, offset, &s, &t);
    s -= floor(s + 1e-9);
    t -= floor(t + 1e-9);
    return tl_pattern_lattice_point(unit, s, t);
}

/* ---- виды плитки ------------------------------------------------------------------ */

void tl_tile_spec_name(const TlTileSpec *spec, char *text, size_t size)
{
    snprintf(text, size, "%g×%g мм", fmax(spec->width, spec->height), fmin(spec->width, spec->height));
}

double tl_tile_spec_area_m2(const TlTileSpec *spec) { return spec->width * spec->height / 1e6; }

bool tl_tile_specs_same(const TlTileSpec *a, const TlTileSpec *b)
{
    double a_long = fmax(a->width, a->height), a_short = fmin(a->width, a->height);
    double b_long = fmax(b->width, b->height), b_short = fmin(b->width, b->height);
    if (fabs(a_long - b_long) > 0.05 || fabs(a_short - b_short) > 0.05)
        return false;
    for (size_t i = 0; i < TL_COLOR_LENGTH && (a->color[i] || b->color[i]); i++)
        if (tolower((unsigned char)a->color[i]) != tolower((unsigned char)b->color[i]))
            return false;
    return true;
}

int tl_compare_cells(TlCell a, TlCell b)
{
    if (a.row != b.row)
        return a.row < b.row ? -1 : 1;
    if (a.column != b.column)
        return a.column < b.column ? -1 : 1;
    return a.slot < b.slot ? -1 : a.slot > b.slot;
}

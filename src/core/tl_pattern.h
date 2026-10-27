/*
 * Рисунок укладки — как плитки повторяются на поверхности.
 *
 * Любой рисунок — модуль из нескольких плиток, повторённый по решётке: модуль
 * (i, j) лежит в offset + i·a + j·b. Плитки модуля — прямоугольники в осях
 * раскладки; наклонные рисунки (ёлочка «по диагонали») получаются поворотом
 * раскладки. Ячейка плитки (TlCell) — (i, j, номер в модуле): по ней плитки
 * комнат с общей сеткой узнают друг друга, по ней же идут сквозные номера.
 *
 * Модуль строится из настроек плитки:
 *   прямая      — одна плитка, ряды со сдвигом row_shift;
 *   шахматка    — две плитки, вторая — цветом color2;
 *   ёлочка      — плитка вдоль и поперёк (длинная сторона — вдоль ряда);
 *   модульные   — плитки из целого числа клеток; клетка — размер плитки в
 *                 настройках, швы клеток сохраняются: плитка в две клетки —
 *                 2·клетка + шов (к покупке — 2·клетка).
 * Новый рисунок — функция модуля и строка в таблице PATTERNS (tl_pattern.c).
 */
#ifndef TL_PATTERN_H
#define TL_PATTERN_H

#include "tl_geometry.h"
#include "tl_tile_settings.h"

#define TL_PATTERN_MAX_TILES 8  /* плиток в модуле */
#define TL_PATTERN_MAX_SPECS 4  /* видов плитки в рисунке */

/* Место плитки в рисунке. */
typedef struct TlCell {
    int column, row; /* модуль (i, j) */
    int slot;        /* плитка модуля */
} TlCell;

/* Вид плитки в рисунке — позиция к покупке. */
typedef struct TlTileSpec {
    double width, height;           /* номинальный размер (так продаётся), мм */
    double laid_width, laid_height; /* как ложится в рисунке (у модульной — со швами клеток) */
    char color[TL_COLOR_LENGTH];
    bool textured;                  /* рисунок из каталога (texture настроек); иначе — только цвет */
} TlTileSpec;

typedef struct TlPatternTile {
    TlRect rect;      /* от начала модуля, в осях раскладки */
    int spec;
} TlPatternTile;

typedef struct TlPatternUnit {
    TlVec2 a, b;                                  /* шаги решётки модулей */
    TlPatternTile tiles[TL_PATTERN_MAX_TILES];
    size_t tile_count;
    TlTileSpec specs[TL_PATTERN_MAX_SPECS];
    size_t spec_count;
} TlPatternUnit;

typedef struct TlPatternInfo {
    const char *id;            /* в файле проекта */
    const char *name;          /* для людей */
    bool uses_row_shift;
    bool uses_second_color;
    bool modular;              /* размер плитки в настройках — клетка модуля */
} TlPatternInfo;

/* Плитка рисунка на поверхности. */
typedef struct TlPlacedTile {
    TlRect rect;               /* в осях раскладки */
    TlCell cell;
    int spec;
    bool turned;               /* лежит поперёк своего вида (у ёлочки — каждая вторая) */
} TlPlacedTile;

typedef void (*TlPatternVisitor)(const TlPlacedTile *tile, void *context);

const TlPatternInfo *tl_patterns(size_t *count);
/* Рисунок настроек; неизвестный id — прямая. */
const TlPatternInfo *tl_pattern_of(const TlTileSettings *settings);

/* Модуль по настройкам; false — размеры не годятся (плитка или шаг не больше нуля). */
bool tl_pattern_unit(const TlTileSettings *settings, TlPatternUnit *unit);

/* Плитки, которые могут задеть bounds, — по рядам модулей (j), в ряду — по i, затем по номеру в модуле. */
void tl_pattern_cover(const TlPatternUnit *unit, TlVec2 offset, TlRect bounds, TlPatternVisitor visit,
                      void *context);
/* Сколько плиток обойдёт tl_pattern_cover (оценка сверху — для защиты от слишком мелкой плитки). */
double tl_pattern_estimate(const TlPatternUnit *unit, TlRect bounds);

/* Смещение из ячейки решётки: s, t в [0, 1) — доли шагов a и b (для перебора смещений). */
TlVec2 tl_pattern_lattice_point(const TlPatternUnit *unit, double s, double t);
/* То же смещение в пределах одного модуля — рисунок не меняется. */
TlVec2 tl_pattern_reduce(const TlPatternUnit *unit, TlVec2 offset);

/* «600×300 мм» — размер вида плитки без учёта поворота. */
void tl_tile_spec_name(const TlTileSpec *spec, char *text, size_t size);
/* Площадь одной плитки к покупке, м². */
double tl_tile_spec_area_m2(const TlTileSpec *spec);
/* Одна позиция к покупке: размер (в любом повороте) и цвет. */
bool tl_tile_specs_same(const TlTileSpec *a, const TlTileSpec *b);

int tl_compare_cells(TlCell a, TlCell b);

#endif

/*
 * Сводный расчёт по проекту.
 *
 * Вид плитки — размер (без учёта поворота) и цвет; одинаковые плитки разных
 * комнат — одна позиция к покупке. У рисунка видов может быть несколько:
 * шахматка — два цвета, модульная — несколько размеров (tl_pattern.h). Комнаты с общим полом раскладываются по
 * одной сетке: плитка, разрезанная стеной между ними, — одна плитка на обе.
 * Остаток подрезки идёт в другую подрезку: две полосы, которые с запасом на
 * рез помещаются в одну плитку, режутся из одной плитки — в любых комнатах с
 * этой плиткой. Запас к покупке округляется один раз на вид плитки.
 */
#ifndef TL_REPORT_H
#define TL_REPORT_H

#include "tl_layout.h"
#include "tl_project.h"

typedef struct TlRoomSummary {
    TlSpace *space;
    TlLayout *layout;            /* основной пол; зоны — tl_space_layout_at(space, 1…) */
    bool tiled;
    double glue_kg;
    int clip_count;
    double grout_kg;
    double primer_l;
    double tiled_area;           /* мм² под плитку — основной пол и зоны */
    double wall_area;            /* мм² облицовки стен */
    double cut_length;           /* мм резов */
    int cross_count;             /* крестиков (если вместо СВП они) */
    double waterproof_area;      /* мм² гидроизоляции: пол и заход на стены */
    double waterproof_kg;
    double tape_length;          /* мм ленты по стыку пола и стен */
    int corner_count;            /* наружных углов стен (колонны, короба) */
    int shared_tiles;            /* плиток, вторая часть которых — в другой комнате */
} TlRoomSummary;

typedef struct TlTileTypeSummary {
    TlTileSpec spec;             /* размер и цвет к покупке */
    const TlTileSettings *tile;  /* настройки первой комнаты с этой плиткой: запас, цена, рисунок */
    TlSpaceList spaces;          /* комнаты с этой плиткой */
    int count;                   /* нужно плиток (без запаса) */
    int whole;
    int to_buy;                  /* с запасом */
    int packs;                   /* упаковок, если плитка продаётся упаковками (tile_pack), иначе 0 */
    double area;                 /* мм² пола */
    int shared_saving;           /* сэкономлено: одна плитка на две комнаты */
    int offcut_saving;           /* сэкономлено: остатки подрезок */
    struct TlPieceList *pieces;  /* подрезки этого вида (в его осях) — для карты раскроя */
} TlTileTypeSummary;

/* Строка «N мешков по M кг» / «N упаковок по M шт.»: по размеру упаковки и цене. */
typedef struct TlPackLine {
    double unit_size;
    double price;                /* ₽ за упаковку, 0 — без цены */
    double amount;
    int units;
} TlPackLine;

/* Два куска из одной плитки. */
typedef struct TlPieceRef {
    TlSpace *space;
    size_t layout;   /* номер раскладки комнаты: 0 — основной пол, дальше — зоны */
    TlCell cell;
} TlPieceRef;

typedef struct TlOffcutPair {
    TlPieceRef first, second;
    bool across_width;     /* куски бок о бок по ширине плитки (иначе — один над другим) */
} TlOffcutPair;

/* Подрезанный кусок: где лежит и его габарит по осям вида плитки. */
typedef struct TlPiece {
    TlPieceRef ref;
    double width, height;
    int spec;                    /* вид плитки в раскладке комнаты */
} TlPiece;

TL_ARRAY_DEFINE(TlRoomSummaryList, TlRoomSummary, tl_room_summary_list)
TL_ARRAY_DEFINE(TlTileTypeSummaryList, TlTileTypeSummary, tl_tile_type_summary_list)
TL_ARRAY_DEFINE(TlPackLineList, TlPackLine, tl_pack_line_list)
TL_ARRAY_DEFINE(TlOffcutPairList, TlOffcutPair, tl_offcut_pair_list)
TL_ARRAY_DEFINE(TlPieceList, TlPiece, tl_piece_list)

typedef struct TlSummary {
    TlRoomSummaryList rooms;
    TlTileTypeSummaryList types;
    TlPackLineList glue;         /* по массе мешка */
    double glue_total_kg;
    TlPackLineList clips;        /* по количеству в упаковке */
    int clip_total;
    TlPackLineList grout;        /* по массе упаковки */
    double grout_total_kg;
    TlPackLineList primer;       /* по объёму канистры */
    double primer_total_l;
    TlPackLineList crosses;      /* крестики — по количеству в упаковке */
    int cross_total;
    TlPackLineList waterproof;   /* гидроизоляция — по массе упаковки */
    double waterproof_total_kg;
    TlPackLineList tape;         /* лента гидроизоляции — метрами (unit_size 1) */
    double tape_total_m;
    TlPackLineList thresholds;   /* пороги в дверях — планками (unit_size — длина планки, мм) */
    double threshold_total_mm;
    TlPackLineList corners;      /* уголки на наружные углы — планками */
    double corner_total_mm;
    double cut_total_mm;         /* резов по проекту */
    double floor_area, tiled_area; /* пол с полом под дверьми; из него — под плитку */
    double room_area;              /* площадь комнат — без пола дверных проёмов */
    double wall_area;              /* облицовка стен */
    TlOffcutPairList offcut_pairs;
} TlSummary;

/* ---- карточки чертежей подрезок -------------------------------------------- */

typedef enum TlCutCardKind {
    TL_CARD_SINGLE,       /* одна подрезка */
    TL_CARD_SHARED_TILE,  /* плитка на две комнаты, между частями — стена */
    TL_CARD_OFFCUT_PAIR,  /* две подрезки из одной плитки, между ними — отход */
    TL_CARD_DRILLED       /* целая плитка с отверстиями */
} TlCutCardKind;

typedef struct TlCutPart {
    char room[128];
    int number;            /* 0 — номер общий, он в заголовке карточки */
    TlPolygonList shapes;  /* от угла плитки; первый — внешний контур */
    TlCircleList drills;   /* отверстия под трубы, от угла плитки */
    TlPolygonList miters;  /* стороны с запилом 45° (отрезки от угла плитки) */
} TlCutPart;

TL_ARRAY_DEFINE(TlCutPartList, TlCutPart, tl_cut_part_list)

typedef struct TlCutCard {
    TlCutCardKind kind;
    int number;            /* для сортировки */
    char title[300];
    char subtitle[160];
    const TlTileSettings *tile;   /* настройки раскладки: узкая подрезка, шов */
    double width, height;         /* плитка карточки — как лежит, в осях кусков */
    TlPolygon outline;            /* плитка-многоугольник от угла габарита; пусто — прямоугольник */
    char color[TL_COLOR_LENGTH];
    TlCutPartList parts;
    bool narrow;
} TlCutCard;

TL_ARRAY_DEFINE(TlCutCardList, TlCutCard, tl_cut_card_list)

/* ---- подбор «Минимум плиток» ----------------------------------------------- */

typedef struct TlTileCount {
    int tiles;     /* плиток без запаса */
    int narrow;    /* узких подрезок */
} TlTileCount;

typedef struct TlTileCountResult {
    TlTileCount before, after;
    double offset_x, offset_y;
} TlTileCountResult;

/* ---- функции ------------------------------------------------------------- */

/* Сквозные номера: комнаты с общим полом — одной нумерацией, плитка на
 * стыке — один номер. Пишет номера в раскладки комнат (TlTile.number). */
void tl_report_assign_numbers(TlProject *project);

TlSummary tl_report_summarize(TlProject *project);
void tl_summary_free(TlSummary *summary);
const TlRoomSummary *tl_summary_room(const TlSummary *summary, const TlSpace *space);

/* Пары полос, которые режутся из одной плитки width × height (с запасом на рез
 * allowance). Жадно: самая широкая полоса — с самой узкой; не подошла — идёт из
 * целой плитки. Куски — одного вида, по его осям. */
TlOffcutPairList tl_report_offcut_pairs(double width, double height, double allowance, const TlPieceList *pieces);

/* ---- карта раскроя ----------------------------------------------------------
 * По виду плитки: одинаковые подрезки — резать стопкой (размер куска и номера на плане),
 * и пары — две подрезки из одной плитки. */

typedef struct TlCutStack {
    double width, height;        /* кусок, мм (по осям вида плитки) */
    TlIntArray numbers;          /* номера на плане */
} TlCutStack;

typedef struct TlCutMapPair {
    int first, second;           /* номера на плане */
    double first_size, second_size; /* ширина полос, мм (поперёк реза) */
    bool across_width;
} TlCutMapPair;

TL_ARRAY_DEFINE(TlCutStackList, TlCutStack, tl_cut_stack_list)
TL_ARRAY_DEFINE(TlCutMapPairList, TlCutMapPair, tl_cut_map_pair_list)

typedef struct TlCutMapType {
    const TlTileTypeSummary *type;
    TlCutStackList stacks;       /* по убыванию числа кусков */
    TlCutMapPairList pairs;
} TlCutMapType;

TL_ARRAY_DEFINE(TlCutMapTypeList, TlCutMapType, tl_cut_map_type_list)

/* Карта раскроя по сводке (номера — tl_report_assign_numbers, его вызывает сводка). Виды без
   подрезок не входят. */
TlCutMapTypeList tl_report_cut_map(const TlSummary *summary);
void tl_cut_map_free(TlCutMapTypeList *map);

TlCutCardList tl_report_cut_cards(TlProject *project);
void tl_cut_card_list_free_all(TlCutCardList *cards);

/* Плиток на раскладку комнаты вместе с комнатами общей сетки. */
bool tl_report_group_count(TlProject *project, TlSpace *space, const TlLayoutOptions *options,
                           TlTileCount *count);
/* Смещение с минимумом плиток к покупке (при равенстве — узких подрезок). */
bool tl_report_optimize_count(TlProject *project, TlSpace *space, TlTileCountResult *result);

/* «600×300 мм» — размер плитки без учёта поворота. */
void tl_report_tile_name(const TlTileSettings *tile, char *text, size_t size);
/* Вид плитки куска раскладки комнаты. */
const TlTileSpec *tl_report_piece_spec(const TlPieceRef *ref);

#endif

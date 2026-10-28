/*
 * Настройки раскладки плитки комнаты: плитка, шов, рисунок укладки (tl_pattern.h),
 * смещение сетки, запас, клей, СВП, затирка, грунт, цены для сметы, использование
 * обрезков, отступ от стен; рисунок плитки из каталога (texture — ссылка
 * «каталог/плитка», пусто — только цвет).
 *
 * Настройки описывают одну поверхность под плитку — сейчас это пол комнаты; стена
 * (когда появится) получит такие же настройки.
 */
#ifndef TL_TILE_SETTINGS_H
#define TL_TILE_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#define TL_COLOR_LENGTH 8      /* "#rrggbb" и завершающий ноль */
#define TL_TEXTURE_LENGTH 260 /* «каталог/группа/плитка» и завершающий ноль */
#define TL_PATTERN_LENGTH 24  /* id рисунка укладки и завершающий ноль */

typedef struct TlTileSettings {
    bool enabled;
    double tile_width, tile_height;  /* номинальный размер (так продаётся), мм */
    double real_width, real_height;  /* фактический (калибр: 598×298 у 600×300), мм; 0 — как номинальный */
    double joint_width;              /* шов между плитками, мм */
    double offset_x, offset_y;       /* смещение сетки в осях раскладки, мм */
    double rotation_degrees;         /* поворот раскладки */
    double row_shift;                /* сдвиг каждого ряда в долях плитки (0, 1/2, 1/3…) */
    char pattern[TL_PATTERN_LENGTH]; /* рисунок укладки (tl_pattern.h); пусто — прямая */
    char color2[TL_COLOR_LENGTH];    /* вторая плитка рисунка (шахматка, вставка) */
    double pattern_angle;            /* угол среза у шеврона (французской ёлочки), ° */
    double insert_size;              /* сторона квадратной вставки (кабошона), мм */
    double min_cut_width;            /* подрезки уже этого — «узкие», мм */
    double reserve_percent;          /* запас на бой и подрезку */
    char color[TL_COLOR_LENGTH];     /* у плитки с рисунком — его средний цвет */
    char texture[TL_TEXTURE_LENGTH]; /* «каталог/группа/плитка» (см. tl_catalog.h); пусто — только цвет */
    double glue_kg_per_m2_mm;        /* расход клея на 1 м² при слое 1 мм */
    double glue_layer_mm;
    double glue_bag_kg;
    int clips_per_long_side;         /* СВП на длинную сторону плитки */
    int clips_per_short_side;
    int clips_per_pack;
    bool reuse_offcuts;              /* остаток подрезки идёт в другую подрезку */
    double cut_allowance;            /* теряется на рез, мм */
    double wall_gap;                 /* отступ плитки от стен, мм */

    /* затирка: расход = (длина + ширина) / (длина · ширина) · шов · глубина · плотность */
    double tile_thickness;           /* толщина плитки — глубина шва, мм */
    double grout_density;            /* плотность затирки, кг/дм³ (г/см³) */
    double grout_pack_kg;
    /* грунт под плитку */
    double primer_l_per_m2;
    double primer_can_l;
    /* фиксация шва: СВП или крестики */
    bool use_crosses;                /* крестики вместо СВП — по одному на пересечение швов */
    int crosses_per_pack;
    /* гидроизоляция: пол и заход на стены по периметру, лента по стыку пола и стен */
    bool waterproof;
    double waterproof_height;        /* заход на стены, мм */
    double waterproof_kg_per_m2;     /* расход на слой */
    int waterproof_layers;
    double waterproof_pack_kg;
    /* профили: порог в дверях и уголки на наружные углы стен (колонны, короба) */
    bool threshold_profile;
    double corner_profile_height;    /* высота уголка на наружный угол, мм; 0 — не нужны */
    double profile_length;           /* длина планки профиля, мм */
    /* плитка продаётся упаковками */
    int tile_pack;                   /* плиток в упаковке; 0 — поштучно */

    /* цены для сметы, ₽; 0 — не считать */
    double price_tile_m2;            /* за единицу price_tile_unit */
    int price_tile_unit;             /* TlPriceUnit: за м², за штуку, за упаковку */
    double price_glue_bag;
    double price_clip_pack;
    double price_grout_pack;
    double price_primer_can;
    double price_work_m2;            /* укладка, за м² */
    double price_cut_m;              /* подрезка, за погонный метр реза */
    double price_cross_pack;
    double price_waterproof_pack;
    double price_tape_m;             /* лента гидроизоляции, за метр */
    double price_threshold;          /* планка порога */
    double price_corner;             /* планка уголка */
} TlTileSettings;

/* За что цена плитки. */
typedef enum TlPriceUnit { TL_PRICE_PER_M2, TL_PRICE_PER_PIECE, TL_PRICE_PER_PACK } TlPriceUnit;

/* Заводские значения. */
TlTileSettings tl_tile_settings_factory(void);

/* Настройки для новой комнаты: заводские, переопределённые последними
 * использованными (кроме смещения сетки). */
TlTileSettings tl_tile_settings_for_new_room(void);
void tl_tile_settings_remember_as_default(const TlTileSettings *settings);
void tl_tile_settings_reset_defaults(void);

bool tl_tile_settings_equal(const TlTileSettings *a, const TlTileSettings *b);

/* Описание полей для сохранения: ключ (как в файлах Python-версии), тип,
 * смещение в структуре и признак «запоминать для новых комнат». */
typedef enum TlTileFieldType {
    TL_FIELD_BOOL,
    TL_FIELD_DOUBLE,
    TL_FIELD_INT,
    TL_FIELD_COLOR,
    TL_FIELD_TEXT /* строка до size байт */
} TlTileFieldType;

typedef struct TlTileField {
    const char *key;
    TlTileFieldType type;
    size_t offset;
    size_t size;                     /* байт в поле (у строки — с завершающим нулём) */
    bool remembered_for_new_rooms;
} TlTileField;

const TlTileField *tl_tile_settings_fields(size_t *count);
bool tl_tile_settings_is_square(const TlTileSettings *settings);

/* Размер, по которому кладут (фактический, если задан, иначе номинальный), мм. */
double tl_tile_laid_width(const TlTileSettings *tile);
double tl_tile_laid_height(const TlTileSettings *tile);

#endif

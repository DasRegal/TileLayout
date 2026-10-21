/*
 * Настройки раскладки плитки комнаты: плитка, шов, рисунок, смещение сетки,
 * запас, клей, СВП, использование обрезков, отступ от стен.
 */
#ifndef TL_TILE_SETTINGS_H
#define TL_TILE_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#define TL_COLOR_LENGTH 8 /* "#rrggbb" и завершающий ноль */

typedef struct TlTileSettings {
    bool enabled;
    double tile_width, tile_height;  /* мм */
    double joint_width;              /* шов между плитками, мм */
    double offset_x, offset_y;       /* смещение сетки в осях раскладки, мм */
    double rotation_degrees;         /* поворот раскладки */
    double row_shift;                /* сдвиг каждого ряда в долях плитки (0, 1/2, 1/3…) */
    double min_cut_width;            /* подрезки уже этого — «узкие», мм */
    double reserve_percent;          /* запас на бой и подрезку */
    char color[TL_COLOR_LENGTH];
    double glue_kg_per_m2_mm;        /* расход клея на 1 м² при слое 1 мм */
    double glue_layer_mm;
    double glue_bag_kg;
    int clips_per_long_side;         /* СВП на длинную сторону плитки */
    int clips_per_short_side;
    int clips_per_pack;
    bool reuse_offcuts;              /* остаток подрезки идёт в другую подрезку */
    double cut_allowance;            /* теряется на рез, мм */
    double wall_gap;                 /* отступ плитки от стен, мм */
} TlTileSettings;

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
typedef enum TlTileFieldType { TL_FIELD_BOOL, TL_FIELD_DOUBLE, TL_FIELD_INT, TL_FIELD_COLOR } TlTileFieldType;

typedef struct TlTileField {
    const char *key;
    TlTileFieldType type;
    size_t offset;
    bool remembered_for_new_rooms;
} TlTileField;

const TlTileField *tl_tile_settings_fields(size_t *count);
bool tl_tile_settings_is_square(const TlTileSettings *settings);

#endif

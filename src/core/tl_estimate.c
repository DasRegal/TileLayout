#include "tl_estimate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void add_line(TlEstimate *estimate, TlEstimateGroup group, const char *name, const char *color, double quantity,
                     const char *unit, double price)
{
    if (!(price > 0) || !(quantity > 0))
        return;
    TlEstimateLine line = {.group = group, .quantity = quantity, .unit = unit, .price = price, .cost = quantity * price};
    snprintf(line.name, sizeof line.name, "%s", name);
    snprintf(line.color, sizeof line.color, "%s", color ? color : "");
    tl_estimate_line_list_push(&estimate->lines, line);
    if (group == TL_ESTIMATE_WORK)
        estimate->work += line.cost;
    else
        estimate->materials += line.cost;
}

static void add_packs(TlEstimate *estimate, const TlPackLineList *packs, const char *format)
{
    for (size_t i = 0; i < packs->count; i++) {
        const TlPackLine *pack = &packs->items[i];
        char name[160];
        snprintf(name, sizeof name, format, pack->unit_size);
        add_line(estimate, TL_ESTIMATE_MATERIALS, name, NULL, pack->units, "шт.", pack->price);
    }
}

/* Работа одной цены — строкой: цены и объёмы по комнатам. */
typedef struct WorkPrices {
    double prices[64], amounts[64];
    size_t count;
} WorkPrices;

static void add_work_amount(WorkPrices *work, double price, double amount)
{
    if (!(price > 0) || !(amount > 0))
        return;
    size_t k = 0;
    while (k < work->count && work->prices[k] != price)
        k++;
    if (k == work->count) {
        if (work->count == sizeof work->prices / sizeof work->prices[0])
            return;
        work->prices[work->count] = price;
        work->amounts[work->count++] = 0;
    }
    work->amounts[k] += amount;
}

/* Укладка — по площади под плитку (основной пол, зоны, полосы стен — у каждой своя цена; пол и
   стены — отдельными строками), подрезка — по погонным метрам резов; строки — по цене. */
static void add_work(TlEstimate *estimate, const TlSummary *summary)
{
    WorkPrices laying = {0}, walls = {0}, cutting = {0};
    for (size_t i = 0; i < summary->rooms.count; i++) {
        const TlRoomSummary *room = &summary->rooms.items[i];
        if (!room->tiled)
            continue;
        for (size_t p = 0; p < room->space->pannos.count; p++) { /* панно — по цене укладки плитки вокруг */
            const TlPanno *panno = &room->space->pannos.items[p];
            bool on_wall = panno->wall != TL_PANNO_FLOOR;
            if (on_wall && !room->space->walls.enabled)
                continue;
            add_work_amount(on_wall ? &walls : &laying, tl_space_panno_tile(room->space, p).price_work_m2,
                            tl_panno_width(panno) * tl_panno_height(panno) / 1e6);
        }
        for (size_t l = 0; l < tl_space_layout_count(room->space); l++)
            if (tl_space_layout_counted(room->space, l))
                add_work_amount(tl_space_layout_on_wall(room->space, l) ? &walls : &laying,
                            tl_space_layout_tile(room->space, l)->price_work_m2,
                            tl_space_layout_at(room->space, l)->floor_area / 1e6);
        add_work_amount(&cutting, tl_space_effective_tile(room->space)->price_cut_m, room->cut_length / 1000);
    }
    for (size_t k = 0; k < laying.count; k++)
        add_line(estimate, TL_ESTIMATE_WORK, "Укладка плитки", NULL, laying.amounts[k], "м²", laying.prices[k]);
    for (size_t k = 0; k < walls.count; k++)
        add_line(estimate, TL_ESTIMATE_WORK, "Облицовка стен", NULL, walls.amounts[k], "м²", walls.prices[k]);
    for (size_t k = 0; k < cutting.count; k++)
        add_line(estimate, TL_ESTIMATE_WORK, "Подрезка плитки", NULL, cutting.amounts[k], "пог. м", cutting.prices[k]);
}

/* Плитка — по цене за м², штуку или упаковку; продаётся упаковками — к покупке целые упаковки. */
static void add_tiles(TlEstimate *estimate, const TlTileTypeSummary *type)
{
    char size[64], name[160];
    tl_tile_spec_name(&type->spec, size, sizeof size);
    const TlTileSettings *tile = type->tile;
    int pieces = type->packs > 0 ? type->packs * tile->tile_pack : type->to_buy;
    if (type->packs > 0)
        snprintf(name, sizeof name, "Плитка %s, %d уп. по %d шт.", size, type->packs, tile->tile_pack);
    else
        snprintf(name, sizeof name, "Плитка %s", size);
    switch (tile->price_tile_unit) {
    case TL_PRICE_PER_PIECE:
        add_line(estimate, TL_ESTIMATE_MATERIALS, name, type->spec.color, pieces, "шт.", tile->price_tile_m2);
        break;
    case TL_PRICE_PER_PACK:
        if (type->packs > 0) {
            add_line(estimate, TL_ESTIMATE_MATERIALS, name, type->spec.color, type->packs, "уп.", tile->price_tile_m2);
            break;
        }
        /* упаковка не задана — цена за штуку */
        add_line(estimate, TL_ESTIMATE_MATERIALS, name, type->spec.color, pieces, "шт.", tile->price_tile_m2);
        break;
    default:
        add_line(estimate, TL_ESTIMATE_MATERIALS, name, type->spec.color, pieces * tl_tile_spec_area_m2(&type->spec),
                 "м²", tile->price_tile_m2);
        break;
    }
}

/* Панно — комплектом, по цене за комплект; одинаковые — одной строкой. */
static void add_pannos(TlEstimate *estimate, const TlSummary *summary)
{
    for (size_t r = 0; r < summary->rooms.count; r++) {
        const TlSpace *space = summary->rooms.items[r].space;
        for (size_t i = 0; i < space->pannos.count; i++) {
            const TlPanno *panno = &space->pannos.items[i];
            if (panno->wall != TL_PANNO_FLOOR && !space->walls.enabled)
                continue;
            char name[160];
            tl_panno_name(panno, name, sizeof name);
            size_t k = 0;
            while (k < estimate->lines.count &&
                   !(estimate->lines.items[k].group == TL_ESTIMATE_MATERIALS && strcmp(estimate->lines.items[k].name, name) == 0 &&
                     estimate->lines.items[k].price == panno->price && strcmp(estimate->lines.items[k].color, panno->color) == 0))
                k++;
            if (k < estimate->lines.count) {
                estimate->lines.items[k].quantity += 1;
                estimate->lines.items[k].cost += panno->price;
                estimate->materials += panno->price;
            } else {
                add_line(estimate, TL_ESTIMATE_MATERIALS, name, panno->color, 1, "компл.", panno->price);
            }
        }
    }
}

TlEstimate tl_estimate_from_summary(const TlSummary *summary)
{
    TlEstimate estimate = {0};
    for (size_t i = 0; i < summary->types.count; i++)
        add_tiles(&estimate, &summary->types.items[i]);
    add_pannos(&estimate, summary);
    add_packs(&estimate, &summary->glue, "Клей, мешок %g кг");
    add_packs(&estimate, &summary->grout, "Затирка, упаковка %g кг");
    add_packs(&estimate, &summary->clips, "СВП, упаковка %g шт.");
    add_packs(&estimate, &summary->primer, "Грунт, канистра %g л");
    add_packs(&estimate, &summary->crosses, "Крестики, упаковка %g шт.");
    add_packs(&estimate, &summary->waterproof, "Гидроизоляция, упаковка %g кг");
    for (size_t i = 0; i < summary->tape.count; i++)
        add_line(&estimate, TL_ESTIMATE_MATERIALS, "Лента гидроизоляции", NULL, summary->tape.items[i].units, "м",
                 summary->tape.items[i].price);
    add_packs(&estimate, &summary->thresholds, "Порог-профиль, планка %g мм");
    add_packs(&estimate, &summary->corners, "Уголок на наружный угол, планка %g мм");
    add_work(&estimate, summary);
    estimate.total = estimate.materials + estimate.work;
    return estimate;
}

void tl_estimate_free(TlEstimate *estimate) { tl_estimate_line_list_free(&estimate->lines); }

void tl_format_rubles(double amount, char *text, size_t size)
{
    long long kopecks = llround(amount * 100);
    long long rubles = kopecks / 100;
    int rest = (int)(kopecks % 100);
    char digits[32], grouped[48];
    snprintf(digits, sizeof digits, "%lld", rubles);
    size_t length = strlen(digits), out = 0;
    for (size_t i = 0; i < length && out + 4 < sizeof grouped; i++) {
        if (i > 0 && (length - i) % 3 == 0) { /* узкий неразрывный пробел между разрядами */
            grouped[out++] = '\xe2';
            grouped[out++] = '\x80';
            grouped[out++] = '\xaf';
        }
        grouped[out++] = digits[i];
    }
    grouped[out] = '\0';
    if (rest)
        snprintf(text, size, "%s,%02d ₽", grouped, rest);
    else
        snprintf(text, size, "%s ₽", grouped);
}

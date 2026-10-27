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

/* Укладка — по площади под плитку (основной пол и зоны — у каждой своя цена), строки — по цене за м². */
static void add_work(TlEstimate *estimate, const TlSummary *summary)
{
    double prices[64];
    double areas[64];
    size_t count = 0;
    for (size_t i = 0; i < summary->rooms.count; i++) {
        const TlRoomSummary *room = &summary->rooms.items[i];
        for (size_t l = 0; room->tiled && l < tl_space_layout_count(room->space); l++) {
            double price = tl_space_layout_tile(room->space, l)->price_work_m2;
            if (!(price > 0))
                continue;
            size_t k = 0;
            while (k < count && prices[k] != price)
                k++;
            if (k == count) {
                if (count == sizeof prices / sizeof prices[0])
                    continue;
                prices[count] = price;
                areas[count++] = 0;
            }
            areas[k] += tl_space_layout_at(room->space, l)->floor_area / 1e6;
        }
    }
    for (size_t k = 0; k < count; k++)
        add_line(estimate, TL_ESTIMATE_WORK, "Укладка плитки", NULL, areas[k], "м²", prices[k]);
}

TlEstimate tl_estimate_from_summary(const TlSummary *summary)
{
    TlEstimate estimate = {0};
    for (size_t i = 0; i < summary->types.count; i++) {
        const TlTileTypeSummary *type = &summary->types.items[i];
        char size[64], name[160];
        tl_tile_spec_name(&type->spec, size, sizeof size);
        snprintf(name, sizeof name, "Плитка %s", size);
        add_line(&estimate, TL_ESTIMATE_MATERIALS, name, type->spec.color,
                 type->to_buy * tl_tile_spec_area_m2(&type->spec), "м²", type->tile->price_tile_m2);
    }
    add_packs(&estimate, &summary->glue, "Клей, мешок %g кг");
    add_packs(&estimate, &summary->grout, "Затирка, упаковка %g кг");
    add_packs(&estimate, &summary->clips, "СВП, упаковка %g шт.");
    add_packs(&estimate, &summary->primer, "Грунт, канистра %g л");
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

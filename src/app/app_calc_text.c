#include "app_calc_text.h"

#include <glib.h>
#include <math.h>

#include "tl_estimate.h"

/* Плиток каждого вида рисунка в комнате и их площадь к покупке с запасом, м². */
static double append_room_specs(GString *text, const TlLayout *layout, double reserve_percent)
{
    int counts[TL_PATTERN_MAX_SPECS] = {0};
    for (size_t i = 0; i < layout->tiles.count; i++)
        counts[layout->tiles.items[i].spec]++;
    double area = 0;
    for (size_t s = 0; s < layout->pattern.spec_count; s++) {
        const TlTileSpec *spec = &layout->pattern.specs[s];
        int with_reserve = (int)ceil(counts[s] * (1 + reserve_percent / 100.0) - 1e-9);
        area += with_reserve * tl_tile_spec_area_m2(spec);
        if (layout->pattern.spec_count > 1) {
            char name[64];
            tl_tile_spec_name(spec, name, sizeof name);
            g_string_append_printf(text, "    <span foreground=\"%s\">■</span> %s: %d шт., с запасом <b>%d</b>\n",
                                   spec->color, name, counts[s], with_reserve);
        }
    }
    return area;
}

static void append_packs(GString *text, const TlPackLineList *packs, const char *unit_format)
{
    for (size_t i = 0; i < packs->count; i++) {
        g_string_append_printf(text, "%s %d ", i ? " +" : "", packs->items[i].units);
        g_string_append_printf(text, unit_format, packs->items[i].unit_size);
    }
    g_string_append(text, "\n");
}

static void append_estimate(GString *text, const TlSummary *summary)
{
    TlEstimate estimate = tl_estimate_from_summary(summary);
    if (estimate.lines.count > 0) {
        char money[64];
        g_string_append(text, "\n<b>Смета</b>\n");
        for (size_t i = 0; i < estimate.lines.count; i++) {
            const TlEstimateLine *line = &estimate.lines.items[i];
            char *name = g_markup_escape_text(line->name, -1);
            tl_format_rubles(line->cost, money, sizeof money);
            bool whole = fabs(line->quantity - round(line->quantity)) < 1e-9; /* упаковки — целые */
            g_string_append_printf(text, "%s: %.*f %s × %g ₽ = <b>%s</b>\n", name, whole ? 0 : 2, line->quantity,
                                   line->unit, line->price, money);
            g_free(name);
        }
        tl_format_rubles(estimate.materials, money, sizeof money);
        g_string_append_printf(text, "Материалы: <b>%s</b>\n", money);
        if (estimate.work > 0) {
            tl_format_rubles(estimate.work, money, sizeof money);
            g_string_append_printf(text, "Работа: <b>%s</b>\n", money);
        }
        tl_format_rubles(estimate.total, money, sizeof money);
        g_string_append_printf(text, "<big>Итого: <b>%s</b></big>\n", money);
    }
    tl_estimate_free(&estimate);
}

static void append_room(GString *text, const TlSummary *summary, TlSpace *space)
{
    const TlTileSettings *tile = tl_space_effective_tile(space);
    if (!tile->enabled)
        return;
    TlLayout *layout = tl_space_layout(space);
    if (layout->too_many) {
        g_string_append(text, "<b>Слишком мелкая плитка для такой комнаты</b>\n");
        return;
    }
    int total = tl_layout_total(layout);
    int with_reserve = (int)ceil(total * (1 + tile->reserve_percent / 100.0) - 1e-9);
    double kg = tl_layout_glue_kg(space, layout);
    char *name = g_markup_escape_text(space->name, -1);
    g_string_append_printf(text, "<b>%s</b>\n", name);
    g_free(name);
    g_string_append_printf(text, "Целых плиток: <b>%d</b>\n", layout->whole_count);
    g_string_append_printf(text, "С подрезкой: <b>%d</b>\n", layout->cut_count);
    g_string_append_printf(text, "<span foreground=\"#c0392b\">Узких подрезок (&lt; %.0f мм): <b>%d</b></span>\n",
                           tile->min_cut_width, layout->narrow_count);
    g_string_append_printf(text, "Всего на укладку: <b>%d</b> шт.\n", total);
    double buy_area = append_room_specs(text, layout, tile->reserve_percent);
    g_string_append_printf(text, "С запасом %.0f%%: <b>%d</b> шт. = <b>%.2f м²</b>\n", tile->reserve_percent,
                           with_reserve, buy_area);
    g_string_append_printf(text, "Под плитку (с полом под дверью): %.2f м²\n", layout->floor_area / 1e6);
    g_string_append_printf(text, "Клей: %.2f м² × %.2f кг × %.1f мм = <b>%.1f кг</b> → <b>%d</b> мешк. по %g кг\n",
                           layout->floor_area / 1e6, tile->glue_kg_per_m2_mm, tile->glue_layer_mm, kg,
                           tl_layout_glue_bags(space, layout), tile->glue_bag_kg);
    g_string_append_printf(text, "СВП: <b>%zu</b> шт. (%d + %d на сторону) → <b>%d</b> уп. по %d\n",
                           layout->clip_points.count, tile->clips_per_long_side, tile->clips_per_short_side,
                           tl_layout_clip_packs(space, layout), tile->clips_per_pack);
    double grout = tl_layout_grout_kg(space, layout);
    g_string_append_printf(text, "Затирка: %.1f м швов × %g × %g мм = <b>%.2f кг</b>\n",
                           tl_layout_joint_length(layout) / 1000, tile->joint_width, tile->tile_thickness, grout);
    g_string_append_printf(text, "Грунт: %.2f м² × %g л = <b>%.1f л</b>\n", layout->floor_area / 1e6,
                           tile->primer_l_per_m2, tl_layout_primer_l(space, layout));
    const TlRoomSummary *room = tl_summary_room(summary, space);
    if (room && room->shared_tiles)
        g_string_append_printf(text, "<small>Из них <b>%d</b> плиток общие с соседней комнатой: "
                                     "одна плитка на обе (учтено в итоге по проекту).</small>\n",
                               room->shared_tiles);
}

static void append_rooms_of_type(GString *text, const TlTileTypeSummary *type)
{
    for (size_t i = 0; i < type->spaces.count; i++) {
        char *name = g_markup_escape_text(type->spaces.items[i]->name, -1);
        g_string_append_printf(text, "%s%s", i ? ", " : "", name);
        g_free(name);
    }
}

static void append_project(GString *text, const TlSummary *summary)
{
    g_string_append(text, "<b>По проекту</b>\n");
    g_string_append_printf(text, "Площадь комнат: %.2f м²\n", summary->room_area / 1e6);
    g_string_append_printf(text, "Под плитку (с полом под дверьми): <b>%.2f м²</b>\n", summary->tiled_area / 1e6);
    if (summary->floor_area - summary->tiled_area > 1000)
        g_string_append_printf(text, "Без плитки: %.2f м²\n", (summary->floor_area - summary->tiled_area) / 1e6);
    for (size_t i = 0; i < summary->types.count; i++) {
        const TlTileTypeSummary *type = &summary->types.items[i];
        char name[64];
        tl_tile_spec_name(&type->spec, name, sizeof name);
        g_string_append_printf(text, "%zu. <span foreground=\"%s\">■</span> %s — ", i + 1, type->spec.color, name);
        append_rooms_of_type(text, type);
        g_string_append_printf(text, ":\n    <b>%d</b> шт. с запасом = <b>%.2f м²</b>\n", type->to_buy,
                               type->to_buy * tl_tile_spec_area_m2(&type->spec));
        if (type->shared_saving > 0 || type->offcut_saving > 0) {
            g_string_append(text, "    <small>");
            if (type->shared_saving > 0)
                g_string_append_printf(text, "одна плитка на две комнаты: −%d", type->shared_saving);
            if (type->shared_saving > 0 && type->offcut_saving > 0)
                g_string_append(text, "; ");
            if (type->offcut_saving > 0)
                g_string_append_printf(text, "из остатков подрезок: −%d", type->offcut_saving);
            g_string_append(text, "</small>\n");
        }
    }
    if (summary->glue.count) {
        g_string_append_printf(text, "Клей: <b>%.1f кг</b> =", summary->glue_total_kg);
        append_packs(text, &summary->glue, "мешк. по %g кг");
    }
    if (summary->clips.count) {
        g_string_append_printf(text, "СВП: <b>%d шт.</b> =", summary->clip_total);
        append_packs(text, &summary->clips, "уп. по %g");
    }
    if (summary->grout.count) {
        g_string_append_printf(text, "Затирка: <b>%.2f кг</b> =", summary->grout_total_kg);
        append_packs(text, &summary->grout, "уп. по %g кг");
    }
    if (summary->primer.count) {
        g_string_append_printf(text, "Грунт: <b>%.1f л</b> =", summary->primer_total_l);
        append_packs(text, &summary->primer, "кан. по %g л");
    }
    append_estimate(text, summary);
}

char *app_calc_markup(TlProject *project, TlSpace *selected)
{
    TlSummary summary = tl_report_summarize(project);
    GString *text = g_string_new(NULL);
    if (selected) {
        append_room(text, &summary, selected);
        g_string_append(text, "\n");
    }
    append_project(text, &summary);
    tl_summary_free(&summary);
    return g_string_free(text, FALSE);
}

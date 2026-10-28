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

/* Плитки зоны (бордюр — все стены вместе). */
static void append_zone(GString *text, TlSpace *space, TlZoneKind kind)
{
    int whole = 0, cut = 0, narrow = 0;
    double area = 0;
    bool found = false;
    for (size_t l = 1; l < tl_space_layout_count(space); l++) {
        if (tl_space_layout_zone(space, l) != kind)
            continue;
        const TlLayout *layout = tl_space_layout_at(space, l);
        whole += layout->whole_count;
        cut += layout->cut_count;
        narrow += layout->narrow_count;
        area += layout->floor_area;
        found = true;
    }
    if (!found)
        return;
    char size[64];
    tl_report_tile_name(&space->zones[kind].tile, size, sizeof size);
    g_string_append_printf(text, "<span foreground=\"%s\">■</span> %s %s: <b>%d</b> шт. (целых %d, с подрезкой %d",
                           space->zones[kind].tile.color, tl_zone_name(kind), size, whole + cut + narrow, whole,
                           cut + narrow);
    if (narrow)
        g_string_append_printf(text, ", <span foreground=\"#c0392b\">узких %d</span>", narrow);
    g_string_append_printf(text, "), %.2f м²\n", area / 1e6);
}

/* Облицовка стен — по полосам. */
static void append_walls(GString *text, TlSpace *space)
{
    for (int band = 0; space->walls.enabled && band < TL_WALL_BAND_COUNT; band++) {
        int whole = 0, cut = 0;
        double area = 0;
        bool found = false;
        for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
            TlWallBand of = TL_WALL_BAND_COUNT;
            const TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, &of);
            if (of != (TlWallBand)band || tl_space_wall_layout_stairs(space, i, NULL, NULL) != SIZE_MAX)
                continue;
            whole += layout->whole_count;
            cut += layout->cut_count + layout->narrow_count;
            area += layout->floor_area;
            found = true;
        }
        if (!found)
            continue;
        const TlTileSettings *tile = tl_space_wall_tile(space, (TlWallBand)band);
        char size[64];
        tl_report_tile_name(tile, size, sizeof size);
        g_string_append_printf(text, "<span foreground=\"%s\">■</span> Стены, %s %s: <b>%d</b> шт. (целых %d, с подрезкой %d), %.2f м²\n",
                               tile->color, tl_wall_band_name((TlWallBand)band), size, whole + cut, whole, cut, area / 1e6);
    }
    int whole = 0, cut = 0;
    double area = 0;
    for (size_t i = 0; i < tl_space_wall_layout_count(space); i++) {
        if (tl_space_wall_layout_stairs(space, i, NULL, NULL) == SIZE_MAX)
            continue;
        const TlLayout *layout = tl_space_wall_layout_at(space, i, NULL, NULL);
        whole += layout->whole_count;
        cut += layout->cut_count + layout->narrow_count;
        area += layout->floor_area;
    }
    if (whole + cut > 0)
        g_string_append_printf(text, "Ступени: <b>%d</b> шт. (целых %d, с подрезкой %d), %.2f м²\n", whole + cut, whole,
                               cut, area / 1e6);
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
    const TlRoomSummary *room = tl_summary_room(summary, space);
    int total = tl_layout_total(layout);
    int with_reserve = (int)ceil(total * (1 + tile->reserve_percent / 100.0) - 1e-9);
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
    for (int kind = 0; kind < TL_ZONE_COUNT; kind++)
        append_zone(text, space, (TlZoneKind)kind);
    append_walls(text, space);
    for (size_t i = 0; i < space->pannos.count; i++) { /* панно — комплектом */
        const TlPanno *panno = &space->pannos.items[i];
        if (panno->wall != TL_PANNO_FLOOR && !space->walls.enabled)
            continue;
        char title[160];
        tl_panno_name(panno, title, sizeof title);
        char *escaped = g_markup_escape_text(title, -1);
        g_string_append_printf(text, "<span foreground=\"%s\">■</span> %s — %s, 1 компл.\n", panno->color, escaped,
                               panno->wall == TL_PANNO_FLOOR ? "на полу" : "на стене");
        g_free(escaped);
    }
    if (!room)
        return;
    /* клей, СВП, затирка, грунт — по основному полу и зонам */
    double area = room->tiled_area / 1e6;
    g_string_append_printf(text, "Под плитку (с полом под дверью): %.2f м²\n", area);
    if (room->wall_area > 0)
        g_string_append_printf(text, "Облицовка стен: %.2f м² (клей, затирка, грунт — вместе с полом)\n",
                               room->wall_area / 1e6);
    g_string_append_printf(text, "Клей: %.2f м² × %.2f кг × %.1f мм = <b>%.1f кг</b> → <b>%d</b> мешк. по %g кг\n",
                           area, tile->glue_kg_per_m2_mm, tile->glue_layer_mm, room->glue_kg,
                           tile->glue_bag_kg > 0 ? (int)ceil(room->glue_kg / tile->glue_bag_kg - 1e-9) : 0,
                           tile->glue_bag_kg);
    if (tile->use_crosses)
        g_string_append_printf(text, "Крестики: <b>%d</b> шт. (на пересечения швов) → <b>%d</b> уп. по %d\n",
                               room->cross_count,
                               tile->crosses_per_pack > 0 ? (room->cross_count + tile->crosses_per_pack - 1) / tile->crosses_per_pack : 0,
                               tile->crosses_per_pack);
    else
        g_string_append_printf(text, "СВП: <b>%d</b> шт. (%d + %d на сторону) → <b>%d</b> уп. по %d\n", room->clip_count,
                               tile->clips_per_long_side, tile->clips_per_short_side,
                               tile->clips_per_pack > 0 ? (room->clip_count + tile->clips_per_pack - 1) / tile->clips_per_pack : 0,
                               tile->clips_per_pack);
    g_string_append_printf(text, "Затирка: %.1f м швов × %g × %g мм = <b>%.2f кг</b>\n",
                           tl_layout_joint_length(layout) / 1000, tile->joint_width, tile->tile_thickness, room->grout_kg);
    g_string_append_printf(text, "Грунт: %.2f м² × %g л = <b>%.1f л</b>\n", area, tile->primer_l_per_m2,
                           room->primer_l);
    g_string_append_printf(text, "Резов (подрезка): <b>%.1f пог. м</b>\n", room->cut_length / 1000);
    if (tile->waterproof)
        g_string_append_printf(text, "Гидроизоляция: %.2f м² (с заходом %g мм) × %g кг × %d сл. = <b>%.1f кг</b>; "
                                     "лента <b>%.1f м</b>\n",
                               room->waterproof_area / 1e6, tile->waterproof_height, tile->waterproof_kg_per_m2,
                               tile->waterproof_layers, room->waterproof_kg, room->tape_length / 1000);
    if (tile->corner_profile_height > 0)
        g_string_append_printf(text, "Наружных углов: <b>%d</b> × %g мм уголка\n", room->corner_count,
                               tile->corner_profile_height);
    if (room->shared_tiles)
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
    if (summary->wall_area > 0)
        g_string_append_printf(text, "Облицовка стен: <b>%.2f м²</b>\n", summary->wall_area / 1e6);
    for (size_t i = 0; i < summary->types.count; i++) {
        const TlTileTypeSummary *type = &summary->types.items[i];
        char name[64];
        tl_tile_spec_name(&type->spec, name, sizeof name);
        g_string_append_printf(text, "%zu. <span foreground=\"%s\">■</span> %s — ", i + 1, type->spec.color, name);
        append_rooms_of_type(text, type);
        g_string_append_printf(text, ":\n    <b>%d</b> шт. с запасом = <b>%.2f м²</b>", type->to_buy,
                               type->to_buy * tl_tile_spec_area_m2(&type->spec));
        if (type->packs > 0)
            g_string_append_printf(text, " → <b>%d</b> уп. по %d шт.", type->packs, type->tile->tile_pack);
        g_string_append(text, "\n");
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
    if (summary->crosses.count) {
        g_string_append_printf(text, "Крестики: <b>%d шт.</b> =", summary->cross_total);
        append_packs(text, &summary->crosses, "уп. по %g");
    }
    if (summary->waterproof.count) {
        g_string_append_printf(text, "Гидроизоляция: <b>%.1f кг</b> =", summary->waterproof_total_kg);
        append_packs(text, &summary->waterproof, "уп. по %g кг");
        g_string_append_printf(text, "Лента гидроизоляции: <b>%.1f м</b>\n", summary->tape_total_m);
    }
    if (summary->thresholds.count) {
        g_string_append_printf(text, "Пороги: <b>%.2f м</b> =", summary->threshold_total_mm / 1000);
        append_packs(text, &summary->thresholds, "планк. по %g мм");
    }
    if (summary->corners.count) {
        g_string_append_printf(text, "Уголки: <b>%.2f м</b> =", summary->corner_total_mm / 1000);
        append_packs(text, &summary->corners, "планк. по %g мм");
    }
    g_string_append_printf(text, "Подрезка: <b>%.1f пог. м</b> резов\n", summary->cut_total_mm / 1000);
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

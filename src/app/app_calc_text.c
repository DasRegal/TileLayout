#include "app_calc_text.h"

#include <glib.h>
#include <math.h>

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
    g_string_append_printf(text, "С запасом %.0f%%: <b>%d</b> шт. = <b>%.2f м²</b>\n", tile->reserve_percent,
                           with_reserve, with_reserve * tile->tile_width * tile->tile_height / 1e6);
    g_string_append_printf(text, "Под плитку (с полом под дверью): %.2f м²\n", layout->floor_area / 1e6);
    g_string_append_printf(text, "Клей: %.2f м² × %.2f кг × %.1f мм = <b>%.1f кг</b> → <b>%d</b> мешк. по %g кг\n",
                           layout->floor_area / 1e6, tile->glue_kg_per_m2_mm, tile->glue_layer_mm, kg,
                           tl_layout_glue_bags(space, layout), tile->glue_bag_kg);
    g_string_append_printf(text, "СВП: <b>%zu</b> шт. (%d + %d на сторону) → <b>%d</b> уп. по %d\n",
                           layout->clip_points.count, tile->clips_per_long_side, tile->clips_per_short_side,
                           tl_layout_clip_packs(space, layout), tile->clips_per_pack);
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
        tl_report_tile_name(type->tile, name, sizeof name);
        g_string_append_printf(text, "%zu. <span foreground=\"%s\">■</span> %s — ", i + 1, type->tile->color, name);
        append_rooms_of_type(text, type);
        g_string_append_printf(text, ":\n    <b>%d</b> шт. с запасом = <b>%.2f м²</b>\n", type->to_buy,
                               type->to_buy * type->tile->tile_width * type->tile->tile_height / 1e6);
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
        for (size_t i = 0; i < summary->glue.count; i++)
            g_string_append_printf(text, "%s %d мешк. по %g кг", i ? " +" : "", summary->glue.items[i].units,
                                   summary->glue.items[i].unit_size);
        g_string_append(text, "\n");
    }
    if (summary->clips.count) {
        g_string_append_printf(text, "СВП: <b>%d шт.</b> =", summary->clip_total);
        for (size_t i = 0; i < summary->clips.count; i++)
            g_string_append_printf(text, "%s %d уп. по %g", i ? " +" : "", summary->clips.items[i].units,
                                   summary->clips.items[i].unit_size);
        g_string_append(text, "\n");
    }
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

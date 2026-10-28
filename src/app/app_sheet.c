#include "app_sheet.h"

#include <glib.h>
#include <math.h>
#include <stdarg.h>
#include <pango/pangocairo.h>
#include <time.h>

#include "tl_estimate.h"
#include "tl_report.h"

#define CELL_PADDING 4.0
#define BLOCK_GAP 6.0

typedef enum BlockKind { BLOCK_TEXT, BLOCK_TABLE_ROW } BlockKind;

/* Блок листа: абзац или строка таблицы. */
typedef struct Block {
    BlockKind kind;
    char **cells;            /* разметка Pango: одна ячейка у абзаца */
    int column_count;
    const double *columns;   /* доли ширины колонок */
    const int *alignments;   /* -1 / 0 / 1 */
    double font_px;
    bool header;             /* строка-заголовок таблицы */
    bool spans_row;          /* одна ячейка на всю строку (после первой колонки) */
    int table_id;
    double height;           /* вычисляется при раскладке */
} Block;

struct AppSheet {
    GPtrArray *blocks;
    double width;
    bool mono;
    GArray *page_starts;     /* индекс первого блока каждой страницы */
    double paginated_for;
};

/* ---- построение ------------------------------------------------------------- */

static Block *add_text(AppSheet *sheet, const char *markup, double font_px)
{
    Block *block = g_new0(Block, 1);
    block->kind = BLOCK_TEXT;
    block->cells = g_new0(char *, 2);
    block->cells[0] = g_strdup(markup);
    block->column_count = 1;
    block->font_px = font_px;
    g_ptr_array_add(sheet->blocks, block);
    return block;
}

static Block *add_row(AppSheet *sheet, int table_id, const double *columns, const int *alignments, int column_count,
                      bool header, char **cells)
{
    Block *block = g_new0(Block, 1);
    block->kind = BLOCK_TABLE_ROW;
    block->cells = cells;
    block->column_count = column_count;
    block->columns = columns;
    block->alignments = alignments;
    block->font_px = 12.5;
    block->header = header;
    block->table_id = table_id;
    g_ptr_array_add(sheet->blocks, block);
    return block;
}

static char **cells_from(const char *first, ...)
{
    GPtrArray *cells = g_ptr_array_new();
    va_list arguments;
    va_start(arguments, first);
    for (const char *cell = first; cell; cell = va_arg(arguments, const char *))
        g_ptr_array_add(cells, g_strdup(cell));
    va_end(arguments);
    g_ptr_array_add(cells, NULL);
    return (char **)g_ptr_array_free(cells, FALSE);
}

static void free_block(gpointer data)
{
    Block *block = data;
    g_strfreev(block->cells);
    g_free(block);
}

/* ---- содержимое листа расчёта ------------------------------------------------ */

static const double TYPE_COLUMNS[] = {0.04, 0.2, 0.24, 0.1, 0.1, 0.08, 0.12, 0.12};
static const int TYPE_ALIGN[] = {-1, -1, -1, 1, 1, 1, 1, 1};
static const double ROOM_COLUMNS[] = {0.18, 0.1, 0.13, 0.29, 0.1, 0.1, 0.1};
static const int ROOM_ALIGN[] = {-1, 1, 1, -1, 1, 1, 1};
static const double ESTIMATE_COLUMNS[] = {0.46, 0.18, 0.16, 0.2};
static const int ESTIMATE_ALIGN[] = {-1, 1, 1, 1};

static char *swatch(const char *color, bool mono)
{
    return mono ? g_strdup("") : g_strdup_printf("<span foreground=\"%s\">■</span> ", color);
}

static char *escaped(const char *text) { return g_markup_escape_text(text, -1); }

static char *rooms_of(const TlTileTypeSummary *type)
{
    GString *text = g_string_new(NULL);
    for (size_t i = 0; i < type->spaces.count; i++) {
        char *name = escaped(type->spaces.items[i]->name);
        g_string_append_printf(text, "%s%s", i ? ", " : "", name);
        g_free(name);
    }
    if (type->shared_saving > 0 || type->offcut_saving > 0) {
        g_string_append(text, "\n<small>");
        if (type->shared_saving > 0)
            g_string_append_printf(text, "одна плитка на две комнаты: −%d", type->shared_saving);
        if (type->shared_saving > 0 && type->offcut_saving > 0)
            g_string_append(text, "; ");
        if (type->offcut_saving > 0)
            g_string_append_printf(text, "из остатков подрезок: −%d", type->offcut_saving);
        g_string_append(text, "</small>");
    }
    return g_string_free(text, FALSE);
}

static void add_tile_types(AppSheet *sheet, const TlSummary *summary, bool mono)
{
    add_text(sheet, "<b>Плитка к покупке</b>", 16);
    add_row(sheet, 1, TYPE_COLUMNS, TYPE_ALIGN, 8, true,
            cells_from("<b>№</b>", "<b>Плитка</b>", "<b>Комнаты</b>", "<b>Площадь, м²</b>", "<b>Нужно, шт.</b>",
                       "<b>Запас</b>", "<b>Купить, шт.</b>", "<b>Купить, м²</b>", NULL));
    for (size_t i = 0; i < summary->types.count; i++) {
        const TlTileTypeSummary *type = &summary->types.items[i];
        const TlTileSettings *tile = type->tile;
        char name[64];
        tl_tile_spec_name(&type->spec, name, sizeof name);
        char *mark = swatch(type->spec.color, mono);
        char *rooms = rooms_of(type);
        char *number = g_strdup_printf("%zu", i + 1);
        char *tile_text = g_strdup_printf("%s%s\n<small>%s, шов %g мм</small>", mark, name, type->spec.color,
                                          tile->joint_width);
        char *area = g_strdup_printf("%.2f", type->area / 1e6);
        char *count = g_strdup_printf("%d", type->count);
        char *reserve = g_strdup_printf("%g%%", tile->reserve_percent);
        char *buy = type->packs > 0 ? g_strdup_printf("<b>%d</b>\n<small>%d уп. по %d</small>", type->to_buy, type->packs,
                                                      tile->tile_pack)
                                    : g_strdup_printf("<b>%d</b>", type->to_buy);
        char *buy_area = g_strdup_printf("<b>%.2f</b>", type->to_buy * tl_tile_spec_area_m2(&type->spec));
        add_row(sheet, 1, TYPE_COLUMNS, TYPE_ALIGN, 8, false,
                cells_from(number, tile_text, rooms, area, count, reserve, buy, buy_area, NULL));
        g_free(mark);
        g_free(rooms);
        g_free(number);
        g_free(tile_text);
        g_free(area);
        g_free(count);
        g_free(reserve);
        g_free(buy);
        g_free(buy_area);
    }
}

/* «Клей: 12,3 кг = 1 мешк. по 25 кг» — строка материала по упаковкам. */
static void append_packs(GString *text, const char *title, double total, int decimals, const char *total_unit,
                         const TlPackLineList *packs, const char *pack_format)
{
    if (!packs->count)
        return;
    g_string_append_printf(text, "%s%s: <b>%.*f %s</b> =", text->len ? "\n" : "", title, decimals, total, total_unit);
    for (size_t i = 0; i < packs->count; i++) {
        g_string_append_printf(text, "%s <b>%d</b> ", i ? " +" : "", packs->items[i].units);
        g_string_append_printf(text, pack_format, packs->items[i].unit_size);
    }
}

static void add_materials(AppSheet *sheet, const TlSummary *summary)
{
    add_text(sheet, "<b>Клей, СВП, затирка, грунт и прочее</b>", 16);
    GString *text = g_string_new(NULL);
    append_packs(text, "Клей", summary->glue_total_kg, 1, "кг", &summary->glue, "мешк. по %g кг");
    append_packs(text, "СВП", summary->clip_total, 0, "шт.", &summary->clips, "уп. по %g шт.");
    append_packs(text, "Затирка", summary->grout_total_kg, 2, "кг", &summary->grout, "уп. по %g кг");
    append_packs(text, "Грунт", summary->primer_total_l, 1, "л", &summary->primer, "кан. по %g л");
    append_packs(text, "Крестики", summary->cross_total, 0, "шт.", &summary->crosses, "уп. по %g шт.");
    append_packs(text, "Гидроизоляция", summary->waterproof_total_kg, 1, "кг", &summary->waterproof, "уп. по %g кг");
    if (summary->tape_total_m > 0)
        g_string_append_printf(text, "%sЛента гидроизоляции: <b>%.1f м</b>", text->len ? "\n" : "", summary->tape_total_m);
    append_packs(text, "Пороги-профили", summary->threshold_total_mm / 1000, 2, "м", &summary->thresholds,
                 "планк. по %g мм");
    append_packs(text, "Уголки на наружные углы", summary->corner_total_mm / 1000, 2, "м", &summary->corners,
                 "планк. по %g мм");
    if (summary->cut_total_mm > 0)
        g_string_append_printf(text, "%sПодрезка: <b>%.1f пог. м</b> резов", text->len ? "\n" : "",
                               summary->cut_total_mm / 1000);
    add_text(sheet, text->str, 13);
    g_string_free(text, TRUE);
}

static void add_estimate(AppSheet *sheet, const TlSummary *summary, bool mono)
{
    TlEstimate estimate = tl_estimate_from_summary(summary);
    if (estimate.lines.count == 0) {
        tl_estimate_free(&estimate);
        return;
    }
    add_text(sheet, "<b>Смета</b>", 16);
    add_row(sheet, 3, ESTIMATE_COLUMNS, ESTIMATE_ALIGN, 4, true,
            cells_from("<b>Позиция</b>", "<b>Количество</b>", "<b>Цена, ₽</b>", "<b>Сумма</b>", NULL));
    char money[64];
    for (size_t i = 0; i < estimate.lines.count; i++) {
        const TlEstimateLine *line = &estimate.lines.items[i];
        char *mark = line->color[0] ? swatch(line->color, mono) : g_strdup("");
        char *name = escaped(line->name);
        char *title = g_strdup_printf("%s%s", mark, name);
        bool whole = fabs(line->quantity - round(line->quantity)) < 1e-9;
        char *quantity = g_strdup_printf("%.*f %s", whole ? 0 : 2, line->quantity, line->unit);
        char *price = g_strdup_printf("%g", line->price);
        tl_format_rubles(line->cost, money, sizeof money);
        add_row(sheet, 3, ESTIMATE_COLUMNS, ESTIMATE_ALIGN, 4, false, cells_from(title, quantity, price, money, NULL));
        g_free(mark);
        g_free(name);
        g_free(title);
        g_free(quantity);
        g_free(price);
    }
    GString *totals = g_string_new(NULL);
    tl_format_rubles(estimate.materials, money, sizeof money);
    g_string_append_printf(totals, "Материалы: <b>%s</b>", money);
    if (estimate.work > 0) {
        tl_format_rubles(estimate.work, money, sizeof money);
        g_string_append_printf(totals, "; работа: <b>%s</b>", money);
    }
    tl_format_rubles(estimate.total, money, sizeof money);
    g_string_append_printf(totals, "\n<big>Итого: <b>%s</b></big>", money);
    add_text(sheet, totals->str, 13);
    g_string_free(totals, TRUE);
    tl_estimate_free(&estimate);
}

static void add_rooms(AppSheet *sheet, const TlSummary *summary, bool mono)
{
    add_text(sheet, "<b>По комнатам</b>", 16);
    add_row(sheet, 2, ROOM_COLUMNS, ROOM_ALIGN, 7, true,
            cells_from("<b>Комната</b>", "<b>Площадь, м²</b>", "<b>Под плитку, м²</b>", "<b>Плитка / раскладка</b>",
                       "<b>Плиток</b>", "<b>Клей, кг</b>", "<b>СВП</b>", NULL));
    for (size_t i = 0; i < summary->rooms.count; i++) {
        const TlRoomSummary *room = &summary->rooms.items[i];
        TlSpace *space = room->space;
        char *name = escaped(space->name);
        char *floor = g_strdup_printf("%.2f", tl_space_room_area(space) / 1e6);
        if (!room->tiled) {
            add_row(sheet, 2, ROOM_COLUMNS, ROOM_ALIGN, 7, false,
                    cells_from(name, floor, "—", "без плитки", "", "", "", NULL));
            g_free(name);
            g_free(floor);
            continue;
        }
        const TlTileSettings *tile = tl_space_effective_tile(space);
        char tile_name[64];
        tl_report_tile_name(tile, tile_name, sizeof tile_name);
        char *mark = swatch(tile->color, mono);
        char *source = tl_space_has_shared_floor(space) ? g_strdup_printf("как в «%s»", space->layout_source->name)
                                                        : g_strdup("своя");
        char *pattern = g_utf8_strdown(tl_pattern_of(tile)->name, -1);
        char *how = g_strdup_printf("%s%s, %s, %s%s", mark, tile_name, pattern, source,
                                    tile->rotation_degrees != 0 ? ", повёрнута" : "");
        g_free(pattern);
        char *tiled = g_strdup_printf("%.2f", room->layout->floor_area / 1e6);
        char *count = g_strdup_printf("%d", tl_space_tile_total(space));
        char *glue = g_strdup_printf("%.1f", room->glue_kg);
        char *clips = g_strdup_printf("%d", room->clip_count);
        add_row(sheet, 2, ROOM_COLUMNS, ROOM_ALIGN, 7, false, cells_from(name, floor, tiled, how, count, glue, clips, NULL));
        if (room->shared_tiles) {
            char *note = g_strdup_printf("<small>из них %d плиток общие с соседней комнатой — одна плитка на обе</small>",
                                         room->shared_tiles);
            Block *block = add_row(sheet, 2, ROOM_COLUMNS, ROOM_ALIGN, 7, false, cells_from("", note, NULL));
            block->spans_row = true;
            g_free(note);
        }
        g_free(name);
        g_free(floor);
        g_free(mark);
        g_free(source);
        g_free(how);
        g_free(tiled);
        g_free(count);
        g_free(glue);
        g_free(clips);
    }
}

AppSheet *app_sheet_new_calculation(TlProject *project, const char *title, bool mono, double width)
{
    AppSheet *sheet = g_new0(AppSheet, 1);
    sheet->blocks = g_ptr_array_new_with_free_func(free_block);
    sheet->width = width;
    sheet->mono = mono;
    TlSummary summary = tl_report_summarize(project);

    char *heading = g_markup_printf_escaped("<b>%s</b>", title);
    add_text(sheet, heading, 20);
    g_free(heading);
    char date[32];
    time_t now = time(NULL);
    strftime(date, sizeof date, "%d.%m.%Y", localtime(&now));
    GString *intro = g_string_new(NULL);
    g_string_append_printf(intro, "Дата: %s\nПлощадь комнат: <b>%.2f м²</b>; под плитку: <b>%.2f м²</b> (с полом под дверьми)",
                           date, summary.room_area / 1e6, summary.tiled_area / 1e6);
    if (summary.floor_area - summary.tiled_area > 1000)
        g_string_append_printf(intro, ", без плитки: %.2f м²", (summary.floor_area - summary.tiled_area) / 1e6);
    if (summary.wall_area > 0)
        g_string_append_printf(intro, "; стены: <b>%.2f м²</b>", summary.wall_area / 1e6);
    add_text(sheet, intro->str, 13);
    g_string_free(intro, TRUE);

    add_tile_types(sheet, &summary, mono);
    add_materials(sheet, &summary);
    add_estimate(sheet, &summary, mono);
    add_rooms(sheet, &summary, mono);
    add_text(sheet, "<small>«Нужно» — плиток на укладку с учётом общих плиток на стыке комнат с общей раскладкой "
                    "и использования остатков подрезок. В таблице по комнатам — плитки каждой комнаты отдельно.</small>",
             12);
    tl_summary_free(&summary);
    return sheet;
}

/* ---- карта раскроя ---------------------------------------------------------- */

static const double STACK_COLUMNS[] = {0.22, 0.12, 0.66};
static const int STACK_ALIGN[] = {-1, 1, -1};
static const double PAIR_COLUMNS[] = {0.34, 0.66};
static const int PAIR_ALIGN[] = {-1, -1};

/* «3, 7, 12, 15» — номера на плане. */
static char *numbers_text(const TlIntArray *numbers)
{
    GString *text = g_string_new(NULL);
    for (size_t i = 0; i < numbers->count; i++)
        g_string_append_printf(text, "%s%d", i ? ", " : "", numbers->items[i]);
    return g_string_free(text, FALSE);
}

AppSheet *app_sheet_new_cut_map(TlProject *project, const char *title, bool mono, double width)
{
    AppSheet *sheet = g_new0(AppSheet, 1);
    sheet->blocks = g_ptr_array_new_with_free_func(free_block);
    sheet->width = width;
    sheet->mono = mono;
    TlSummary summary = tl_report_summarize(project);
    TlCutMapTypeList map = tl_report_cut_map(&summary);
    char *heading = g_markup_printf_escaped("<b>Карта раскроя: %s</b>", title);
    add_text(sheet, heading, 20);
    g_free(heading);
    add_text(sheet, "Номера — как на плане с номерами и в чертежах подрезок. Одинаковые куски режьте стопкой: "
                    "целые плитки — одним размером за раз.", 13);
    if (map.count == 0)
        add_text(sheet, "Подрезок нет — вся плитка целая.", 13);
    for (size_t t = 0; t < map.count; t++) {
        const TlCutMapType *entry = &map.items[t];
        char name[64];
        tl_tile_spec_name(&entry->type->spec, name, sizeof name);
        char *swatch_text = swatch(entry->type->spec.color, mono);
        char *title_text = g_strdup_printf("%s <b>Плитка %s</b>", swatch_text, name);
        add_text(sheet, title_text, 15);
        g_free(swatch_text);
        g_free(title_text);
        int table = (int)t * 2 + 1;
        if (entry->stacks.count) {
            add_row(sheet, table, STACK_COLUMNS, STACK_ALIGN, 3, true, cells_from("<b>Кусок, мм</b>", "<b>Плиток</b>", "<b>Номера на плане</b>", NULL));
            for (size_t k = 0; k < entry->stacks.count; k++) {
                const TlCutStack *stack = &entry->stacks.items[k];
                char size[48], count[16];
                g_snprintf(size, sizeof size, "%.0f × %.0f", stack->width, stack->height);
                g_snprintf(count, sizeof count, "%zu", stack->numbers.count);
                char *numbers = numbers_text(&stack->numbers);
                add_row(sheet, table, STACK_COLUMNS, STACK_ALIGN, 3, false, cells_from(size, count, numbers, NULL));
                g_free(numbers);
            }
        }
        if (entry->pairs.count) {
            add_text(sheet, "<b>Две подрезки из одной плитки</b> (полосы — по ширине, остальное — в отход)", 13);
            add_row(sheet, table + 1, PAIR_COLUMNS, PAIR_ALIGN, 2, true, cells_from("<b>Куски</b>", "<b>Полосы, мм</b>", NULL));
            for (size_t k = 0; k < entry->pairs.count; k++) {
                const TlCutMapPair *pair = &entry->pairs.items[k];
                char pieces[48], sizes[64];
                g_snprintf(pieces, sizeof pieces, "№%d + №%d", pair->first, pair->second);
                g_snprintf(sizes, sizeof sizes, "%.0f + %.0f", pair->first_size, pair->second_size);
                add_row(sheet, table + 1, PAIR_COLUMNS, PAIR_ALIGN, 2, false, cells_from(pieces, sizes, NULL));
            }
        }
    }
    tl_cut_map_free(&map);
    tl_summary_free(&summary);
    return sheet;
}

/* ---- смета для заказчика ------------------------------------------------------ */

static const double CUSTOMER_COLUMNS[] = {0.06, 0.48, 0.16, 0.13, 0.17};
static const int CUSTOMER_ALIGN[] = {-1, -1, 1, 1, 1};

static void add_customer_section(AppSheet *sheet, const TlEstimate *estimate, TlEstimateGroup group, const char *title,
                                 int table_id, bool mono)
{
    bool any = false;
    for (size_t i = 0; i < estimate->lines.count; i++)
        any = any || estimate->lines.items[i].group == group;
    if (!any)
        return;
    add_text(sheet, title, 16);
    add_row(sheet, table_id, CUSTOMER_COLUMNS, CUSTOMER_ALIGN, 5, true,
            cells_from("<b>№</b>", "<b>Наименование</b>", "<b>Количество</b>", "<b>Цена, ₽</b>", "<b>Сумма</b>", NULL));
    int number = 0;
    char money[64];
    for (size_t i = 0; i < estimate->lines.count; i++) {
        const TlEstimateLine *line = &estimate->lines.items[i];
        if (line->group != group)
            continue;
        char *index = g_strdup_printf("%d", ++number);
        char *mark = line->color[0] ? swatch(line->color, mono) : g_strdup("");
        char *name = escaped(line->name);
        char *title_cell = g_strdup_printf("%s%s", mark, name);
        bool whole = fabs(line->quantity - round(line->quantity)) < 1e-9;
        char *quantity = g_strdup_printf("%.*f %s", whole ? 0 : 2, line->quantity, line->unit);
        g_strdelimit(quantity, ".", ','); /* по-русски — запятая */
        char *price = g_strdup_printf("%g", line->price);
        g_strdelimit(price, ".", ',');
        tl_format_rubles(line->cost, money, sizeof money);
        add_row(sheet, table_id, CUSTOMER_COLUMNS, CUSTOMER_ALIGN, 5, false,
                cells_from(index, title_cell, quantity, price, money, NULL));
        g_free(index);
        g_free(mark);
        g_free(name);
        g_free(title_cell);
        g_free(quantity);
        g_free(price);
    }
    tl_format_rubles(group == TL_ESTIMATE_WORK ? estimate->work : estimate->materials, money, sizeof money);
    char *total = g_strdup_printf("<b>%s: %s</b>", group == TL_ESTIMATE_WORK ? "Итого работы" : "Итого материалы", money);
    add_text(sheet, total, 13);
    g_free(total);
}

AppSheet *app_sheet_new_estimate(TlProject *project, const char *title, bool mono, double width)
{
    AppSheet *sheet = g_new0(AppSheet, 1);
    sheet->blocks = g_ptr_array_new_with_free_func(free_block);
    sheet->width = width;
    sheet->mono = mono;
    TlSummary summary = tl_report_summarize(project);
    TlEstimate estimate = tl_estimate_from_summary(&summary);

    char *heading = g_markup_printf_escaped("<b>Смета: %s</b>", title);
    add_text(sheet, heading, 20);
    g_free(heading);
    char date[32];
    time_t now = time(NULL);
    strftime(date, sizeof date, "%d.%m.%Y", localtime(&now));
    char *intro = summary.wall_area > 0
                      ? g_strdup_printf("Дата: %s\nУкладка плитки: пол <b>%.2f м²</b>, стены <b>%.2f м²</b> (площадь комнат %.2f м²)",
                                        date, summary.tiled_area / 1e6, summary.wall_area / 1e6, summary.room_area / 1e6)
                      : g_strdup_printf("Дата: %s\nУкладка плитки: <b>%.2f м²</b> (площадь комнат %.2f м²)", date,
                                        summary.tiled_area / 1e6, summary.room_area / 1e6);
    add_text(sheet, intro, 13);
    g_free(intro);
    if (estimate.lines.count == 0) {
        add_text(sheet, "Цены не заданы: задайте их в разделе «Цены для сметы» у комнат.", 13);
    } else {
        add_customer_section(sheet, &estimate, TL_ESTIMATE_MATERIALS, "<b>Материалы</b>", 4, mono);
        add_customer_section(sheet, &estimate, TL_ESTIMATE_WORK, "<b>Работы</b>", 5, mono);
        char money[64];
        tl_format_rubles(estimate.total, money, sizeof money);
        char *total = g_strdup_printf("<big><b>Всего к оплате: %s</b></big>", money);
        add_text(sheet, total, 15);
        g_free(total);
        add_text(sheet, "<small>Количество плитки — с запасом на бой и подрезку; материалы — целыми упаковками.</small>",
                 12);
    }
    add_text(sheet, "\n\nИсполнитель: ______________________        Заказчик: ______________________", 13);
    tl_estimate_free(&estimate);
    tl_summary_free(&summary);
    return sheet;
}

void app_sheet_free(AppSheet *sheet)
{
    if (!sheet)
        return;
    g_ptr_array_free(sheet->blocks, TRUE);
    if (sheet->page_starts)
        g_array_free(sheet->page_starts, TRUE);
    g_free(sheet);
}

/* ---- раскладка и отрисовка ----------------------------------------------------- */

static PangoLayout *cell_layout(cairo_t *cr, const char *markup, double font_px, double width, int alignment)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(font, font_px * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_markup(layout, markup, -1);
    pango_layout_set_width(layout, (int)(fmax(width, 1) * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout, alignment < 0 ? PANGO_ALIGN_LEFT : alignment > 0 ? PANGO_ALIGN_RIGHT
                                                                                        : PANGO_ALIGN_CENTER);
    return layout;
}

static double column_x(const Block *block, double width, int column)
{
    double x = 0;
    for (int i = 0; i < column; i++)
        x += block->columns[i] * width;
    return x;
}

static double cell_width(const Block *block, double width, int column)
{
    if (block->spans_row && column == 1)
        return width - column_x(block, width, 1);
    return block->columns[column] * width;
}

static int cell_count(const Block *block) { return block->spans_row ? 2 : block->column_count; }

static double measure_block(cairo_t *cr, const AppSheet *sheet, Block *block)
{
    if (block->kind == BLOCK_TEXT) {
        PangoLayout *layout = cell_layout(cr, block->cells[0], block->font_px, sheet->width, -1);
        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);
        g_object_unref(layout);
        return h + BLOCK_GAP;
    }
    double tallest = 0;
    for (int c = 0; c < cell_count(block); c++) {
        PangoLayout *layout = cell_layout(cr, block->cells[c], block->font_px,
                                          cell_width(block, sheet->width, c) - 2 * CELL_PADDING, block->alignments[c]);
        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);
        tallest = fmax(tallest, h);
        g_object_unref(layout);
    }
    return tallest + 2 * CELL_PADDING;
}

static void paginate(AppSheet *sheet, double page_height)
{
    if (sheet->page_starts && sheet->paginated_for == page_height)
        return;
    if (sheet->page_starts)
        g_array_free(sheet->page_starts, TRUE);
    sheet->page_starts = g_array_new(FALSE, FALSE, sizeof(guint));
    sheet->paginated_for = page_height;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(surface);
    guint first = 0;
    g_array_append_val(sheet->page_starts, first);
    double y = 0;
    for (guint i = 0; i < sheet->blocks->len; i++) {
        Block *block = g_ptr_array_index(sheet->blocks, i);
        block->height = measure_block(cr, sheet, block);
        /* заголовок раздела не остаётся последним на странице */
        Block *next = i + 1 < sheet->blocks->len ? g_ptr_array_index(sheet->blocks, i + 1) : NULL;
        double keep = block->kind == BLOCK_TEXT && next && next->kind == BLOCK_TABLE_ROW ? 60 : 0;
        if (y > 0 && y + block->height + keep > page_height) {
            g_array_append_val(sheet->page_starts, i);
            y = 0;
        }
        y += block->height;
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

int app_sheet_page_count(AppSheet *sheet, double page_height)
{
    paginate(sheet, page_height);
    return (int)sheet->page_starts->len;
}

static Block *table_header(AppSheet *sheet, int table_id)
{
    for (guint i = 0; i < sheet->blocks->len; i++) {
        Block *block = g_ptr_array_index(sheet->blocks, i);
        if (block->kind == BLOCK_TABLE_ROW && block->header && block->table_id == table_id)
            return block;
    }
    return NULL;
}

static void set_ink(cairo_t *cr) { cairo_set_source_rgb(cr, 0, 0, 0); }

static double draw_block(cairo_t *cr, AppSheet *sheet, Block *block, double y)
{
    if (block->kind == BLOCK_TEXT) {
        PangoLayout *layout = cell_layout(cr, block->cells[0], block->font_px, sheet->width, -1);
        set_ink(cr);
        cairo_move_to(cr, 0, y);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        return block->height;
    }
    if (block->header && !sheet->mono) {
        cairo_rectangle(cr, 0, y, sheet->width, block->height);
        cairo_set_source_rgb(cr, 0.91, 0.918, 0.929);
        cairo_fill(cr);
    }
    for (int c = 0; c < cell_count(block); c++) {
        double x = column_x(block, sheet->width, c), w = cell_width(block, sheet->width, c);
        PangoLayout *layout = cell_layout(cr, block->cells[c], block->font_px, w - 2 * CELL_PADDING, block->alignments[c]);
        set_ink(cr);
        cairo_move_to(cr, x + CELL_PADDING, y + CELL_PADDING);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_rectangle(cr, x, y, w, block->height);
    }
    if (sheet->mono)
        set_ink(cr);
    else
        cairo_set_source_rgb(cr, 0.6, 0.63, 0.65);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
    return block->height;
}

void app_sheet_render_page(AppSheet *sheet, cairo_t *cr, int page, double page_height)
{
    paginate(sheet, page_height);
    if (page < 0 || (guint)page >= sheet->page_starts->len)
        return;
    guint first = g_array_index(sheet->page_starts, guint, page);
    guint end = (guint)page + 1 < sheet->page_starts->len ? g_array_index(sheet->page_starts, guint, page + 1)
                                                         : sheet->blocks->len;
    double y = 0;
    Block *first_block = g_ptr_array_index(sheet->blocks, first);
    if (page > 0 && first_block->kind == BLOCK_TABLE_ROW && !first_block->header) {
        Block *header = table_header(sheet, first_block->table_id); /* шапка таблицы на новой странице */
        if (header)
            y += draw_block(cr, sheet, header, y);
    }
    for (guint i = first; i < end; i++)
        y += draw_block(cr, sheet, g_ptr_array_index(sheet->blocks, i), y);
}

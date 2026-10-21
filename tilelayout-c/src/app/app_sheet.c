#include "app_sheet.h"

#include <glib.h>
#include <math.h>
#include <stdarg.h>
#include <pango/pangocairo.h>
#include <time.h>

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
        tl_report_tile_name(tile, name, sizeof name);
        char *mark = swatch(tile->color, mono);
        char *rooms = rooms_of(type);
        char *number = g_strdup_printf("%zu", i + 1);
        char *tile_text = g_strdup_printf("%s%s\n<small>%s, шов %g мм</small>", mark, name, tile->color, tile->joint_width);
        char *area = g_strdup_printf("%.2f", type->area / 1e6);
        char *count = g_strdup_printf("%d", type->count);
        char *reserve = g_strdup_printf("%g%%", tile->reserve_percent);
        char *buy = g_strdup_printf("<b>%d</b>", type->to_buy);
        char *buy_area = g_strdup_printf("<b>%.2f</b>", type->to_buy * tile->tile_width * tile->tile_height / 1e6);
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

static void add_glue_and_clips(AppSheet *sheet, const TlSummary *summary)
{
    add_text(sheet, "<b>Клей и СВП</b>", 16);
    GString *text = g_string_new(NULL);
    if (summary->glue.count) {
        g_string_append_printf(text, "Клей: <b>%.1f кг</b> =", summary->glue_total_kg);
        for (size_t i = 0; i < summary->glue.count; i++)
            g_string_append_printf(text, "%s <b>%d</b> мешк. по %g кг", i ? " +" : "",
                                   summary->glue.items[i].units, summary->glue.items[i].unit_size);
        g_string_append(text, "\n");
    }
    if (summary->clips.count) {
        g_string_append_printf(text, "СВП: <b>%d шт.</b> =", summary->clip_total);
        for (size_t i = 0; i < summary->clips.count; i++)
            g_string_append_printf(text, "%s <b>%d</b> уп. по %g шт.", i ? " +" : "",
                                   summary->clips.items[i].units, summary->clips.items[i].unit_size);
    }
    add_text(sheet, text->str, 13);
    g_string_free(text, TRUE);
}

static void add_rooms(AppSheet *sheet, const TlSummary *summary, bool mono)
{
    add_text(sheet, "<b>По комнатам</b>", 16);
    add_row(sheet, 2, ROOM_COLUMNS, ROOM_ALIGN, 7, true,
            cells_from("<b>Комната</b>", "<b>Пол, м²</b>", "<b>Под плитку, м²</b>", "<b>Плитка / раскладка</b>",
                       "<b>Плиток</b>", "<b>Клей, кг</b>", "<b>СВП</b>", NULL));
    for (size_t i = 0; i < summary->rooms.count; i++) {
        const TlRoomSummary *room = &summary->rooms.items[i];
        TlSpace *space = room->space;
        char *name = escaped(space->name);
        char *floor = g_strdup_printf("%.2f", tl_space_area(space) / 1e6);
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
        char *how = g_strdup_printf("%s%s, %s%s", mark, tile_name, source, tile->rotation_degrees != 0 ? ", повёрнута" : "");
        char *tiled = g_strdup_printf("%.2f", room->layout->floor_area / 1e6);
        char *count = g_strdup_printf("%d", tl_layout_total(room->layout));
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
    g_string_append_printf(intro, "Дата: %s\nПлощадь пола: <b>%.2f м²</b>, под плитку: <b>%.2f м²</b>", date,
                           summary.floor_area / 1e6, summary.tiled_area / 1e6);
    if (summary.floor_area - summary.tiled_area > 1000)
        g_string_append_printf(intro, ", без плитки: %.2f м²", (summary.floor_area - summary.tiled_area) / 1e6);
    add_text(sheet, intro->str, 13);
    g_string_free(intro, TRUE);

    add_tile_types(sheet, &summary, mono);
    add_glue_and_clips(sheet, &summary);
    add_rooms(sheet, &summary, mono);
    add_text(sheet, "<small>«Нужно» — плиток на укладку с учётом общих плиток на стыке комнат с общей раскладкой "
                    "и использования остатков подрезок. В таблице по комнатам — плитки каждой комнаты отдельно.</small>",
             12);
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

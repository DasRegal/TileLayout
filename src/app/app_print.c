#include "app_print.h"

#include <cairo-pdf.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>

#include "app_draw.h"
#include "app_render.h"
#include "app_sheet.h"
#include "tl_report.h"

#define HEADER_PX 26.0
#define DIMENSION_MARGIN_PX 55.0
#define CARD_WIDTH_PX (63 * APP_PX_PER_MM)
#define CARD_HEIGHT_PX (56 * APP_PX_PER_MM)

const int APP_PRINT_SCALES[] = {10, 20, 25, 50, 75, 100, 150, 200, 250, 500};
const int APP_PRINT_SCALE_COUNT = sizeof APP_PRINT_SCALES / sizeof APP_PRINT_SCALES[0];

/* Масштабы, из которых выбирается «по размеру листа». */
static const int FIT_SCALES[] = {5, 10, 15, 20, 25, 30, 40, 50, 60, 75, 100, 125, 150, 200, 250, 300, 400, 500, 750, 1000};

typedef enum PageKind { PAGE_PLAN, PAGE_PLAN_NUMBERS, PAGE_CARDS, PAGE_SHEET } PageKind;

typedef struct Page {
    PageKind kind;
    int index;        /* лист плана / страница карточек / страница расчёта */
} Page;

/* Как план разложен на листы. */
typedef struct PlanLayout {
    bool empty;
    int scale;              /* 1:scale */
    double pixels_per_mm;   /* логических пикселей на мм плана */
    int columns, rows;
    double origin_x, origin_y;  /* левый верхний угол габарита плана на общем поле листов */
    TlRect bounds;
} PlanLayout;

struct AppPrintJob {
    TlProject *project;
    char title[256];
    AppPrintPreferences options;
    double offset_x_mm, offset_y_mm;
    double paper_width_mm, paper_height_mm;
    double area_width, area_height;   /* область печати, логические пиксели */
    PlanLayout plan;
    TlCutCardList cards;
    int card_columns, card_rows;
    AppSheet *sheet;
    GArray *pages;
};

/* ---- компоновка ------------------------------------------------------------- */

static PlanLayout layout_plan(AppPrintJob *job)
{
    PlanLayout plan = {0};
    if (!app_render_plan_bounds(job->project, &plan.bounds)) {
        plan.empty = true;
        return plan;
    }
    double width = fmax(tl_rect_width(plan.bounds), 1), height = fmax(tl_rect_height(plan.bounds), 1);
    double area_width = job->area_width, area_height = job->area_height - HEADER_PX;
    int scale = job->options.scale;
    if (scale <= 0) { /* наименьший стандартный масштаб, при котором план влезает */
        double fit = fmin((area_width - 2 * DIMENSION_MARGIN_PX) / width,
                          (area_height - 2 * DIMENSION_MARGIN_PX) / height);
        double wanted = APP_PX_PER_MM / fit;
        scale = (int)ceil(wanted);
        for (size_t i = 0; i < G_N_ELEMENTS(FIT_SCALES); i++)
            if (FIT_SCALES[i] >= wanted - 1e-6) {
                scale = FIT_SCALES[i];
                break;
            }
    }
    plan.scale = scale;
    plan.pixels_per_mm = APP_PX_PER_MM / scale;
    double plan_width = width * plan.pixels_per_mm, plan_height = height * plan.pixels_per_mm;
    plan.columns = (int)fmax(1, ceil((plan_width + 2 * DIMENSION_MARGIN_PX) / area_width - 1e-6));
    plan.rows = (int)fmax(1, ceil((plan_height + 2 * DIMENSION_MARGIN_PX) / area_height - 1e-6));
    plan.origin_x = (plan.columns * area_width - plan_width) / 2 + job->offset_x_mm * APP_PX_PER_MM;
    plan.origin_y = (plan.rows * area_height - plan_height) / 2 + job->offset_y_mm * APP_PX_PER_MM;
    return plan;
}

static void add_pages(GArray *pages, PageKind kind, int count)
{
    for (int i = 0; i < count; i++) {
        Page page = {kind, i};
        g_array_append_val(pages, page);
    }
}

AppPrintJob *app_print_job_new(TlProject *project, const char *title, const AppPrintPreferences *options,
                               double offset_x_mm, double offset_y_mm)
{
    AppPrintJob *job = g_new0(AppPrintJob, 1);
    job->project = project;
    g_strlcpy(job->title, title, sizeof job->title);
    job->options = *options;
    job->offset_x_mm = offset_x_mm;
    job->offset_y_mm = offset_y_mm;
    job->paper_width_mm = options->landscape ? 297 : 210;
    job->paper_height_mm = options->landscape ? 210 : 297;
    job->area_width = (job->paper_width_mm - 2 * APP_PAPER_MARGIN_MM) * APP_PX_PER_MM;
    job->area_height = (job->paper_height_mm - 2 * APP_PAPER_MARGIN_MM) * APP_PX_PER_MM;
    job->plan = layout_plan(job);
    job->pages = g_array_new(FALSE, FALSE, sizeof(Page));

    int plan_pages = job->plan.empty ? 0 : job->plan.columns * job->plan.rows;
    add_pages(job->pages, PAGE_PLAN, plan_pages);
    if (options->cuts) {
        add_pages(job->pages, PAGE_PLAN_NUMBERS, plan_pages);
        job->cards = tl_report_cut_cards(project);
        job->card_columns = (int)fmax(1, floor(job->area_width / CARD_WIDTH_PX));
        job->card_rows = (int)fmax(1, floor((job->area_height - HEADER_PX) / CARD_HEIGHT_PX));
        int per_page = job->card_columns * job->card_rows;
        add_pages(job->pages, PAGE_CARDS, (int)((job->cards.count + per_page - 1) / per_page));
    }
    if (options->calc) {
        job->sheet = app_sheet_new_calculation(project, job->title, options->mono, job->area_width);
        add_pages(job->pages, PAGE_SHEET, app_sheet_page_count(job->sheet, job->area_height));
    }
    return job;
}

void app_print_job_free(AppPrintJob *job)
{
    if (!job)
        return;
    tl_cut_card_list_free_all(&job->cards);
    app_sheet_free(job->sheet);
    g_array_free(job->pages, TRUE);
    g_free(job);
}

int app_print_job_page_count(AppPrintJob *job) { return (int)job->pages->len; }

void app_print_job_paper_mm(const AppPrintJob *job, double *width, double *height)
{
    *width = job->paper_width_mm;
    *height = job->paper_height_mm;
}

void app_print_job_plan_info(const AppPrintJob *job, int *scale, int *columns, int *rows)
{
    *scale = job->plan.scale;
    *columns = job->plan.empty ? 0 : job->plan.columns;
    *rows = job->plan.empty ? 0 : job->plan.rows;
}

/* ---- страницы ----------------------------------------------------------------- */

static void draw_header(AppPrintJob *job, cairo_t *cr, const char *subject, const char *right)
{
    char *left = g_strdup_printf("%s — %s", job->title, subject);
    AppTextStyle bold = {14, true, {0, 0, 0, 1}, APP_TRANSPARENT};
    AppTextStyle normal = {12, false, {0, 0, 0, 1}, APP_TRANSPARENT};
    app_text_in_rect(cr, left, &bold, 0, 0, job->area_width * 0.65, HEADER_PX - 6, -1);
    app_text_in_rect(cr, right, &normal, job->area_width * 0.4, 0, job->area_width * 0.6, HEADER_PX - 6, 1);
    cairo_move_to(cr, 0, HEADER_PX - 4);
    cairo_line_to(cr, job->area_width, HEADER_PX - 4);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
    g_free(left);
}

static void render_plan_page(AppPrintJob *job, cairo_t *cr, int index, bool numbers)
{
    const PlanLayout *plan = &job->plan;
    int total = plan->columns * plan->rows;
    int column = index % plan->columns, row = index / plan->columns;
    char right[96];
    g_snprintf(right, sizeof right, "Масштаб 1:%d    Лист %d из %d", plan->scale, index + 1, total);
    draw_header(job, cr, numbers ? "план с номерами плиток" : "план", right);

    double area_height = job->area_height - HEADER_PX;
    AppView view = {plan->pixels_per_mm,
                    plan->origin_x - column * job->area_width - plan->bounds.left * plan->pixels_per_mm,
                    plan->origin_y - row * area_height - plan->bounds.top * plan->pixels_per_mm,
                    job->area_width, area_height};
    AppRenderOptions options = {
        .show_tiles = job->options.tiles || numbers,  /* номера без плиток бессмысленны */
        .show_clips = job->options.clips,
        .show_dims = job->options.dims && !numbers,   /* на плане с номерами — без размеров */
        .show_numbers = numbers,
        .show_room_labels = !numbers,                 /* плашки закрывали бы номера */
        .printing = true,
        .mono = job->options.mono,
    };
    cairo_save(cr);
    cairo_translate(cr, 0, HEADER_PX);
    cairo_rectangle(cr, 0, 0, job->area_width, area_height);
    cairo_clip(cr);
    app_render_plan(cr, job->project, &view, &options);
    cairo_restore(cr);
}

/* ---- карточки подрезок ----------------------------------------------------------- */

static void piece_edges(cairo_t *cr, const TlPolygon *shape, const TlTileSettings *tile, double scale,
                        double ox, double oy, bool mono)
{
    bool rectangle = shape->count == 4;
    for (size_t i = 0; i < shape->count && rectangle; i++) {
        TlVec2 a = shape->items[i], b = shape->items[(i + 1) % 4];
        rectangle = fabs(a.x - b.x) < 0.5 || fabs(a.y - b.y) < 0.5;
    }
    bool dimensioned_horizontal = false, dimensioned_vertical = false;
    for (size_t i = 0; i < shape->count; i++) {
        TlVec2 a = shape->items[i], b = shape->items[(i + 1) % shape->count];
        bool horizontal = fabs(a.y - b.y) < 0.5, vertical = fabs(a.x - b.x) < 0.5;
        bool on_tile_edge = (horizontal && (fabs(a.y) < 0.5 || fabs(a.y - tile->tile_height) < 0.5)) ||
                            (vertical && (fabs(a.x) < 0.5 || fabs(a.x - tile->tile_width) < 0.5));
        TlVec2 sa = tl_vec2(ox + a.x * scale, oy + a.y * scale), sb = tl_vec2(ox + b.x * scale, oy + b.y * scale);
        cairo_move_to(cr, sa.x, sa.y);
        cairo_line_to(cr, sb.x, sb.y);
        if (on_tile_edge) { /* заводская кромка — тонко, линия реза — толсто */
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, 0.9);
        } else {
            if (mono)
                cairo_set_source_rgb(cr, 0, 0, 0);
            else
                cairo_set_source_rgb(cr, 0.851, 0.188, 0.145);
            cairo_set_line_width(cr, 2.2);
        }
        cairo_stroke(cr);
        double length = tl_distance(a, b);
        if (length < 0.5)
            continue;
        if (rectangle) { /* у прямоугольника — одна ширина и одна высота */
            bool *done = horizontal ? &dimensioned_horizontal : &dimensioned_vertical;
            if (*done)
                continue;
            *done = true;
        }
        TlVec2 normal;
        tl_polygon_edge_frame(shape, i, NULL, &normal);
        char text[16];
        g_snprintf(text, sizeof text, "%.0f", length);
        AppTextStyle style = {9, false, {0, 0, 0, 1}, {1, 1, 1, 1}};
        app_text_along(cr, text, &style, sa, sb, normal, 8);
    }
}

/* Свободная часть плитки между двумя частями (стена или отход), в мм плитки. */
static bool free_strip_between_parts(const TlCutCard *card, TlRect *rest)
{
    if (card->parts.count != 2 || !card->parts.items[0].shapes.count || !card->parts.items[1].shapes.count)
        return false;
    TlRect a = tl_polygon_bounds(&card->parts.items[0].shapes.items[0]);
    TlRect b = tl_polygon_bounds(&card->parts.items[1].shapes.items[0]);
    const TlTileSettings *tile = card->tile;
    if (a.left > b.left) {
        TlRect swap = a;
        a = b;
        b = swap;
    }
    if (b.left - a.right > 0.5) {
        *rest = (TlRect){a.right, 0, b.left, tile->tile_height};
        return true;
    }
    if (a.top > b.top) {
        TlRect swap = a;
        a = b;
        b = swap;
    }
    if (b.top - a.bottom > 0.5) {
        *rest = (TlRect){0, a.bottom, tile->tile_width, b.top};
        return true;
    }
    return false;
}

static void hatch_rest(cairo_t *cr, const TlCutCard *card, double scale, double ox, double oy, TlRect tile_rect,
                       const char *label, bool mono)
{
    cairo_save(cr);
    cairo_rectangle(cr, tile_rect.left, tile_rect.top, tl_rect_width(tile_rect), tl_rect_height(tile_rect));
    for (size_t p = 0; p < card->parts.count; p++)
        for (size_t s = 0; s < card->parts.items[p].shapes.count; s++) {
            const TlPolygon *shape = &card->parts.items[p].shapes.items[s];
            for (size_t i = 0; i < shape->count; i++)
                (i ? cairo_line_to : cairo_move_to)(cr, ox + shape->items[i].x * scale, oy + shape->items[i].y * scale);
            cairo_close_path(cr);
        }
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_clip(cr);
    double height = tl_rect_height(tile_rect);
    for (double x = tile_rect.left - height; x < tile_rect.right; x += 4) {
        cairo_move_to(cr, x, tile_rect.bottom);
        cairo_line_to(cr, x + height, tile_rect.top);
    }
    if (mono)
        cairo_set_source_rgb(cr, 0, 0, 0);
    else
        cairo_set_source_rgb(cr, 0.6, 0.63, 0.65);
    cairo_set_line_width(cr, 0.5);
    cairo_stroke(cr);
    cairo_restore(cr);

    TlRect rest;
    if (!free_strip_between_parts(card, &rest))
        return;
    TlRect screen = {ox + rest.left * scale, oy + rest.top * scale, ox + rest.right * scale, oy + rest.bottom * scale};
    double text_width;
    AppTextStyle style = {8, false, {0, 0, 0, 1}, {1, 1, 1, 1}};
    app_text_size(label, &style, &text_width, NULL);
    bool tall = tl_rect_height(screen) > tl_rect_width(screen);
    double along = tall ? tl_rect_height(screen) : tl_rect_width(screen);
    double across = tall ? tl_rect_width(screen) : tl_rect_height(screen);
    if (along > text_width + 4 && across > 9)
        app_text_centered(cr, label, &style, tl_rect_center(screen), tall ? -90 : 0);
}

static void draw_card(AppPrintJob *job, cairo_t *cr, const TlCutCard *card, double x, double y, double w, double h)
{
    bool mono = job->options.mono;
    const TlTileSettings *tile = card->tile;
    cairo_rectangle(cr, x, y, w, h);
    if (mono)
        cairo_set_source_rgb(cr, 0, 0, 0);
    else
        cairo_set_source_rgb(cr, 0.6, 0.63, 0.65);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);

    AppTextStyle title = {11, true, {0, 0, 0, 1}, APP_TRANSPARENT};
    AppTextStyle small = {9, false, {0, 0, 0, 1}, APP_TRANSPARENT};
    app_text_in_rect(cr, card->title, &title, x + 5, y + 2, w - 10, 14, -1);
    char subtitle[200];
    if (card->narrow)
        g_snprintf(subtitle, sizeof subtitle, "%s;  узкая (< %.0f мм)", card->subtitle, tile->min_cut_width);
    else
        g_strlcpy(subtitle, card->subtitle, sizeof subtitle);
    app_text_in_rect(cr, subtitle, &small, x + 5, y + 16, w - 10, 12, -1);

    const double margin = 17;
    double area_x = x + margin, area_y = y + 28 + margin - 4;
    double area_w = w - 2 * margin, area_h = h - 28 - 2 * margin + 2;
    double scale = fmin(area_w / tile->tile_width, area_h / tile->tile_height);
    double ox = area_x + (area_w - tile->tile_width * scale) / 2;
    double oy = area_y + (area_h - tile->tile_height * scale) / 2;
    TlRect tile_rect = {ox, oy, ox + tile->tile_width * scale, oy + tile->tile_height * scale};

    if (card->kind != TL_CARD_SINGLE) {
        const char *label = card->kind == TL_CARD_SHARED_TILE ? "стена" : "отход";
        hatch_rest(cr, card, scale, ox, oy, tile_rect, label, mono);
    }
    cairo_rectangle(cr, tile_rect.left, tile_rect.top, tl_rect_width(tile_rect), tl_rect_height(tile_rect));
    double dashes[2] = {3, 3};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_set_source_rgb(cr, mono ? 0 : 0.48, mono ? 0 : 0.5, mono ? 0 : 0.53);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    AppColor fill = mono ? (AppColor){1, 1, 1, 1} : app_color_hex(tile->color);
    for (size_t p = 0; p < card->parts.count; p++) {
        const TlPolygonList *shapes = &card->parts.items[p].shapes;
        for (size_t s = 0; s < shapes->count; s++) {
            const TlPolygon *shape = &shapes->items[s];
            for (size_t i = 0; i < shape->count; i++)
                (i ? cairo_line_to : cairo_move_to)(cr, ox + shape->items[i].x * scale, oy + shape->items[i].y * scale);
            cairo_close_path(cr);
        }
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_set_source_rgba(cr, fill.red, fill.green, fill.blue, 1);
        cairo_fill(cr);
    }
    for (size_t p = 0; p < card->parts.count; p++)
        for (size_t s = 0; s < card->parts.items[p].shapes.count; s++)
            piece_edges(cr, &card->parts.items[p].shapes.items[s], tile, scale, ox, oy, mono);
    if (card->parts.count > 1) /* подписи частей: комната и номер */
        for (size_t p = 0; p < card->parts.count; p++) {
            const TlCutPart *part = &card->parts.items[p];
            if (!part->shapes.count)
                continue;
            TlRect bounds = tl_polygon_bounds(&part->shapes.items[0]);
            char text[160];
            if (part->number)
                g_snprintf(text, sizeof text, "%s №%d", part->room, part->number);
            else
                g_strlcpy(text, part->room, sizeof text);
            AppTextStyle label = {8, true, {0, 0, 0, 1}, {1, 1, 1, 1}};
            app_text_centered(cr, text, &label,
                              tl_vec2(ox + (bounds.left + bounds.right) / 2 * scale, oy + (bounds.top + bounds.bottom) / 2 * scale), 0);
        }
}

static void render_cards_page(AppPrintJob *job, cairo_t *cr, int index)
{
    int per_page = job->card_columns * job->card_rows;
    int pages = (int)((job->cards.count + per_page - 1) / per_page);
    char right[96];
    g_snprintf(right, sizeof right, "Чертежей: %zu    Лист %d из %d", job->cards.count, index + 1, pages);
    draw_header(job, cr, "чертежи подрезок", right);
    double x_offset = (job->area_width - job->card_columns * CARD_WIDTH_PX) / 2;
    for (int n = 0; n < per_page; n++) {
        size_t card = (size_t)(index * per_page + n);
        if (card >= job->cards.count)
            break;
        double x = x_offset + (n % job->card_columns) * CARD_WIDTH_PX;
        double y = HEADER_PX + (n / job->card_columns) * CARD_HEIGHT_PX;
        draw_card(job, cr, &job->cards.items[card], x + 3, y + 3, CARD_WIDTH_PX - 6, CARD_HEIGHT_PX - 6);
    }
}

void app_print_job_render(AppPrintJob *job, cairo_t *cr, int page)
{
    if (page < 0 || (guint)page >= job->pages->len)
        return;
    Page p = g_array_index(job->pages, Page, page);
    cairo_save(cr);
    cairo_translate(cr, APP_PAPER_MARGIN_MM * APP_PX_PER_MM, APP_PAPER_MARGIN_MM * APP_PX_PER_MM);
    switch (p.kind) {
    case PAGE_PLAN: render_plan_page(job, cr, p.index, false); break;
    case PAGE_PLAN_NUMBERS: render_plan_page(job, cr, p.index, true); break;
    case PAGE_CARDS: render_cards_page(job, cr, p.index); break;
    case PAGE_SHEET: app_sheet_render_page(job->sheet, cr, p.index, job->area_height); break;
    }
    cairo_restore(cr);
}

bool app_print_job_save_pdf(AppPrintJob *job, const char *path)
{
    const double points_per_mm = 72.0 / 25.4;
    cairo_surface_t *surface = cairo_pdf_surface_create(path, job->paper_width_mm * points_per_mm,
                                                        job->paper_height_mm * points_per_mm);
    cairo_t *cr = cairo_create(surface);
    for (int page = 0; page < app_print_job_page_count(job); page++) {
        cairo_save(cr);
        cairo_scale(cr, 72.0 / 96.0, 72.0 / 96.0);
        app_print_job_render(job, cr, page);
        cairo_restore(cr);
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    bool ok = cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surface);
    return ok;
}

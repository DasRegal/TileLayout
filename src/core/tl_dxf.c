#include "tl_dxf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tl_layout.h"
#include "tl_report.h"
#include "tl_strbuf.h"

#define CARD_GAP 150.0  /* мм между чертежами подрезок */
#define CARD_COLUMNS 8
#define CARD_TITLE 40.0 /* высота подписи над плиткой */

typedef struct Layer {
    const char *name;
    int color; /* цвет AutoCAD (ACI) */
} Layer;

enum { WALLS, OPENINGS, FLOOR, WHOLE, CUT, NUMBERS, ROOMS, FIXTURES, DRAINS, PIPES, MITERS, TITLES, LAYER_COUNT };

static const Layer LAYERS[LAYER_COUNT] = {
    [WALLS] = {"WALLS", 7},       [OPENINGS] = {"OPENINGS", 30}, [FLOOR] = {"FLOOR", 8},
    [WHOLE] = {"TILES_WHOLE", 9}, [CUT] = {"TILES_CUT", 5},      [NUMBERS] = {"NUMBERS", 3},
    [ROOMS] = {"ROOMS", 7},       [FIXTURES] = {"FIXTURES", 4},  [DRAINS] = {"DRAINS", 4},
    [PIPES] = {"PIPES", 6},       [MITERS] = {"MITER_45", 1},    [TITLES] = {"TITLES", 7},
};

typedef struct Dxf {
    TlStrBuf text;
    TlRect extent; /* габарит в осях DXF */
    bool empty;
} Dxf;

static void extend(Dxf *dxf, double x, double y)
{
    if (dxf->empty) {
        dxf->extent = (TlRect){x, y, x, y};
        dxf->empty = false;
        return;
    }
    dxf->extent.left = fmin(dxf->extent.left, x);
    dxf->extent.right = fmax(dxf->extent.right, x);
    dxf->extent.top = fmin(dxf->extent.top, y);
    dxf->extent.bottom = fmax(dxf->extent.bottom, y);
}

static void point(Dxf *dxf, int code, double x, double y)
{
    tl_strbuf_appendf(&dxf->text, "%d\n%.3f\n%d\n%.3f\n%d\n0.0\n", code, x, code + 10, y, code + 20);
    extend(dxf, x, y);
}

/* Строка UTF-8 — в DXF: не ASCII — кодами \U+XXXX. */
static void string(TlStrBuf *out, const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    while (*s) {
        uint32_t code = *s;
        int extra = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;
        if (extra)
            code &= 0x3F >> extra;
        s++;
        for (int i = 0; i < extra && (*s & 0xC0) == 0x80; i++, s++)
            code = (code << 6) | (*s & 0x3F);
        if (code < 0x80 && code >= 0x20)
            tl_strbuf_append_char(out, (char)code);
        else if (code >= 0x80)
            tl_strbuf_appendf(out, "\\U+%04X", (unsigned)code);
    }
    tl_strbuf_append_char(out, '\n');
}

static void polygon(Dxf *dxf, int layer, const TlPolygon *polygon, const TlAffine *m, TlVec2 shift, bool closed)
{
    if (polygon->count < 2)
        return;
    tl_strbuf_appendf(&dxf->text, "0\nPOLYLINE\n8\n%s\n66\n1\n70\n%d\n10\n0.0\n20\n0.0\n30\n0.0\n", LAYERS[layer].name,
                      closed ? 1 : 0);
    for (size_t i = 0; i < polygon->count; i++) {
        TlVec2 p = tl_vec2(polygon->items[i].x + shift.x, polygon->items[i].y + shift.y);
        if (m)
            p = tl_affine_apply(m, p);
        tl_strbuf_appendf(&dxf->text, "0\nVERTEX\n8\n%s\n", LAYERS[layer].name);
        point(dxf, 10, p.x, -p.y);
    }
    tl_strbuf_appendf(&dxf->text, "0\nSEQEND\n8\n%s\n", LAYERS[layer].name);
}

static void circle(Dxf *dxf, int layer, TlVec2 center, double radius)
{
    tl_strbuf_appendf(&dxf->text, "0\nCIRCLE\n8\n%s\n", LAYERS[layer].name);
    point(dxf, 10, center.x, -center.y);
    tl_strbuf_appendf(&dxf->text, "40\n%.3f\n", radius);
    extend(dxf, center.x - radius, -center.y - radius);
    extend(dxf, center.x + radius, -center.y + radius);
}

/* Текст по центру в точке at (оси плана). */
static void text(Dxf *dxf, int layer, TlVec2 at, double height, const char *value)
{
    tl_strbuf_appendf(&dxf->text, "0\nTEXT\n8\n%s\n", LAYERS[layer].name);
    point(dxf, 10, at.x, -at.y);
    tl_strbuf_appendf(&dxf->text, "40\n%.3f\n1\n", height);
    string(&dxf->text, value);
    tl_strbuf_append(&dxf->text, "72\n1\n73\n2\n");
    point(dxf, 11, at.x, -at.y);
}

/* Заголовок, слои и начало чертежа — перед сущностями; габарит дописывается в конце. */
static char *finish(Dxf *dxf)
{
    TlStrBuf out = {0};
    if (dxf->empty)
        dxf->extent = (TlRect){0, 0, 0, 0};
    tl_strbuf_appendf(&out,
                      "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n9\n$INSUNITS\n70\n4\n"
                      "9\n$EXTMIN\n10\n%.3f\n20\n%.3f\n30\n0.0\n9\n$EXTMAX\n10\n%.3f\n20\n%.3f\n30\n0.0\n0\nENDSEC\n",
                      dxf->extent.left, dxf->extent.top, dxf->extent.right, dxf->extent.bottom);
    tl_strbuf_append(&out, "0\nSECTION\n2\nTABLES\n"
                           "0\nTABLE\n2\nLTYPE\n70\n1\n0\nLTYPE\n2\nCONTINUOUS\n70\n0\n3\nSolid line\n72\n65\n73\n0\n40\n0.0\n"
                           "0\nENDTAB\n");
    tl_strbuf_appendf(&out, "0\nTABLE\n2\nLAYER\n70\n%d\n", LAYER_COUNT);
    for (int i = 0; i < LAYER_COUNT; i++)
        tl_strbuf_appendf(&out, "0\nLAYER\n2\n%s\n70\n0\n62\n%d\n6\nCONTINUOUS\n", LAYERS[i].name, LAYERS[i].color);
    tl_strbuf_append(&out, "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n");
    if (dxf->text.length)
        tl_strbuf_append_length(&out, dxf->text.text, dxf->text.length);
    tl_strbuf_append(&out, "0\nENDSEC\n0\nEOF\n");
    tl_strbuf_free(&dxf->text);
    return tl_strbuf_take(&out);
}

static void polygons(Dxf *dxf, int layer, const TlPolygonList *list, const TlAffine *m, TlVec2 shift, bool closed)
{
    for (size_t i = 0; i < list->count; i++)
        polygon(dxf, layer, &list->items[i], m, shift, closed);
}

/* ---- план ------------------------------------------------------------------------- */

static void plan_tiles(Dxf *dxf, const TlLayout *layout)
{
    const TlAffine *m = &layout->local_to_world;
    for (size_t t = 0; t < layout->tiles.count; t++) {
        const TlTile *tile = &layout->tiles.items[t];
        TlVec2 corner = {tile->rect.left, tile->rect.top};
        if (tile->kind != TL_TILE_WHOLE && tile->shapes.count) {
            polygons(dxf, CUT, &tile->shapes, m, corner, true);
            for (size_t i = 0; i < tile->miters.count; i++)
                polygon(dxf, MITERS, &tile->miters.items[i], m, corner, false);
        } else if (tile->outline.count) {
            polygon(dxf, WHOLE, &tile->outline, m, corner, true);
        } else {
            const TlVec2 corners[] = {{tile->rect.left, tile->rect.top},
                                      {tile->rect.right, tile->rect.top},
                                      {tile->rect.right, tile->rect.bottom},
                                      {tile->rect.left, tile->rect.bottom}};
            TlPolygon rect = tl_polygon_from_points(corners, 4);
            polygon(dxf, tile->kind == TL_TILE_WHOLE ? WHOLE : CUT, &rect, m, tl_vec2(0, 0), true);
            tl_polygon_free(&rect);
        }
        for (size_t i = 0; i < tile->drills.count; i++) {
            const TlCircle *drill = &tile->drills.items[i];
            TlVec2 center = tl_affine_apply(m, tl_vec2(corner.x + drill->center.x, corner.y + drill->center.y));
            circle(dxf, PIPES, center, drill->diameter / 2);
        }
        if (tile->number > 0) {
            char number[16];
            snprintf(number, sizeof number, "%d", tile->number);
            double size = tile->kind == TL_TILE_WHOLE ? fmin(tl_rect_width(tile->rect), tl_rect_height(tile->rect))
                                                      : fmin(tile->piece_width, tile->piece_height);
            text(dxf, NUMBERS, tl_affine_apply(m, tile->center), fmax(8, fmin(60, size / 4)), number);
        }
    }
}

char *tl_dxf_plan(TlProject *project)
{
    Dxf dxf = {.empty = true};
    tl_project_update_spaces(project);
    tl_report_assign_numbers(project);
    for (size_t i = 0; i < project->outlines.count; i++) {
        const TlOutline *outline = project->outlines.items[i];
        TlPolygon outer = tl_outline_outer(outline);
        polygon(&dxf, WALLS, &outer, NULL, tl_vec2(0, 0), true);
        polygon(&dxf, WALLS, &outline->inner, NULL, tl_vec2(0, 0), true);
        tl_polygon_free(&outer);
    }
    TlPolygonList partitions = tl_project_partition_bodies(project, 0);
    polygons(&dxf, WALLS, &partitions, NULL, tl_vec2(0, 0), true);
    tl_polygon_list_free_all(&partitions);
    for (size_t i = 0; i < project->openings.count; i++) {
        TlPolygon cut;
        if (tl_project_opening_cut(project, project->openings.items[i], &cut)) {
            polygon(&dxf, OPENINGS, &cut, NULL, tl_vec2(0, 0), true);
            tl_polygon_free(&cut);
        }
    }
    for (size_t s = 0; s < project->spaces.count; s++) {
        TlSpace *space = project->spaces.items[s];
        polygon(&dxf, FLOOR, &space->outline, NULL, tl_vec2(0, 0), true);
        for (size_t l = 0; l < tl_space_floor_layout_count(space); l++) {
            const TlLayout *layout = tl_space_layout_at(space, l);
            if (layout && !layout->too_many && tl_space_layout_tile(space, l)->enabled)
                plan_tiles(&dxf, layout);
        }
        TlRect bounds = tl_polygon_bounds(&space->outline);
        TlVec2 middle = {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
        if (tl_polygon_contains(&space->outline, middle))
            text(&dxf, ROOMS, middle, 120, space->name);
    }
    for (size_t i = 0; i < project->fixtures.count; i++) {
        TlPolygon outline = tl_fixture_outline(&project->fixtures.items[i]);
        polygon(&dxf, FIXTURES, &outline, NULL, tl_vec2(0, 0), true);
        tl_polygon_free(&outline);
    }
    for (size_t i = 0; i < project->drains.count; i++) {
        TlPolygon outline = tl_drain_outline(&project->drains.items[i]);
        polygon(&dxf, DRAINS, &outline, NULL, tl_vec2(0, 0), true);
        tl_polygon_free(&outline);
    }
    for (size_t s = 0; s < project->spaces.count; s++)
        polygons(&dxf, DRAINS, &project->spaces.items[s]->slope_lines, NULL, tl_vec2(0, 0), false);
    for (size_t i = 0; i < project->pipes.count; i++)
        circle(&dxf, PIPES, project->pipes.items[i]->center, project->pipes.items[i]->diameter / 2);
    return finish(&dxf);
}

/* ---- подрезки -------------------------------------------------------------------- */

char *tl_dxf_cuts(TlProject *project)
{
    Dxf dxf = {.empty = true};
    tl_project_update_spaces(project);
    TlCutCardList cards = tl_report_cut_cards(project);
    double cell_w = 0, cell_h = 0;
    for (size_t i = 0; i < cards.count; i++) {
        cell_w = fmax(cell_w, cards.items[i].width);
        cell_h = fmax(cell_h, cards.items[i].height);
    }
    cell_w += CARD_GAP;
    cell_h += CARD_GAP + 2 * CARD_TITLE;
    for (size_t i = 0; i < cards.count; i++) {
        const TlCutCard *card = &cards.items[i];
        TlVec2 corner = {(double)(i % CARD_COLUMNS) * cell_w, (double)(i / CARD_COLUMNS) * cell_h + 2 * CARD_TITLE};
        if (card->outline.count) {
            polygon(&dxf, WHOLE, &card->outline, NULL, corner, true);
        } else {
            const TlVec2 corners[] = {{0, 0}, {card->width, 0}, {card->width, card->height}, {0, card->height}};
            TlPolygon rect = tl_polygon_from_points(corners, 4);
            polygon(&dxf, WHOLE, &rect, NULL, corner, true);
            tl_polygon_free(&rect);
        }
        for (size_t p = 0; p < card->parts.count; p++) {
            const TlCutPart *part = &card->parts.items[p];
            polygons(&dxf, CUT, &part->shapes, NULL, corner, true);
            polygons(&dxf, MITERS, &part->miters, NULL, corner, false);
            for (size_t d = 0; d < part->drills.count; d++) {
                const TlCircle *drill = &part->drills.items[d];
                circle(&dxf, PIPES, tl_vec2(corner.x + drill->center.x, corner.y + drill->center.y), drill->diameter / 2);
            }
            if (part->number > 0 && part->shapes.count) {
                TlRect bounds = tl_polygon_bounds(&part->shapes.items[0]);
                char number[16];
                snprintf(number, sizeof number, "%d", part->number);
                TlVec2 at = {corner.x + (bounds.left + bounds.right) / 2, corner.y + (bounds.top + bounds.bottom) / 2};
                text(&dxf, NUMBERS, at, fmax(8, fmin(40, fmin(tl_rect_width(bounds), tl_rect_height(bounds)) / 4)), number);
            }
        }
        text(&dxf, TITLES, tl_vec2(corner.x + card->width / 2, corner.y - CARD_TITLE), CARD_TITLE * 0.6, card->title);
    }
    tl_cut_card_list_free_all(&cards);
    return finish(&dxf);
}

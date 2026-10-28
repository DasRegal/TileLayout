/* Сквозная нумерация, пары обрезков и сводный расчёт по проекту. */
#include "tl_report_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_memory.h"

/* ---- нумерация ------------------------------------------------------------- */

void tl_report_assign_numbers(TlProject *project)
{
    TlLayoutGroupList groups = tl_report_build_groups(project, NULL, NULL);
    int number = 0;
    for (size_t g = 0; g < groups.count; g++) {
        TlCellOwnerList *owners = &groups.items[g].owners; /* уже по рядам и столбцам */
        for (size_t i = 0; i < owners->count; i++) {
            bool new_cell = i == 0 || tl_compare_cells(owners->items[i].cell, owners->items[i - 1].cell) != 0;
            if (new_cell)
                number++;
            owners->items[i].tile->number = number;
        }
    }
    tl_report_free_groups(&groups);
}

/* ---- пары обрезков ----------------------------------------------------------
 *
 * Из одной плитки — два куска, каждый у своего края плитки: так у каждого
 * остаются заводские кромки к швам, а рез идёт к стене. Полоса на всю высоту
 * плитки встаёт в пару по ширине, полоса на всю ширину — по высоте; угловой
 * кусок (обрезан с двух сторон) — у угла плитки, ему подходит и то и другое.
 * Пары — жадно: самая широкая полоса с самой узкой, если вместе с запасом на
 * рез они помещаются в плитку. */

typedef struct Strip {
    double size;
    size_t piece;  /* номер в списке кусков */
} Strip;

TL_ARRAY_DEFINE(StripList, Strip, strip_list)

/* Список кусков, по которому сортируются полосы (для одинакового порядка на всех платформах). */
static const TlPieceList *sorting_pieces;

static int compare_strips(const void *left, const void *right)
{
    const Strip *a = left, *b = right;
    if (a->size != b->size)
        return a->size < b->size ? -1 : 1;
    const TlPieceRef *ra = &sorting_pieces->items[a->piece].ref, *rb = &sorting_pieces->items[b->piece].ref;
    if (ra->space->id != rb->space->id)
        return ra->space->id < rb->space->id ? -1 : 1;
    return tl_compare_cells(ra->cell, rb->cell);
}

static void pair_strips(const TlPieceList *pieces, StripList *strips, double full_length, double allowance,
                        bool across_width, bool *used, TlOffcutPairList *pairs)
{
    if (strips->count < 2)
        return;
    sorting_pieces = pieces;
    qsort(strips->items, strips->count, sizeof(Strip), compare_strips);
    size_t narrowest = 0, widest = strips->count - 1;
    while (narrowest < widest) {
        if (strips->items[narrowest].size + strips->items[widest].size + allowance <= full_length + 1e-6) {
            size_t a = strips->items[narrowest].piece, b = strips->items[widest].piece;
            TlOffcutPair pair = {pieces->items[a].ref, pieces->items[b].ref, across_width};
            tl_offcut_pair_list_push(pairs, pair);
            used[a] = used[b] = true;
            narrowest++;
        }
        widest--;
    }
}

typedef enum PieceShape { PIECE_WHOLE, PIECE_ACROSS_WIDTH, PIECE_ACROSS_HEIGHT, PIECE_CORNER } PieceShape;

static PieceShape piece_shape(const TlPiece *piece, double tile_width, double tile_height)
{
    bool full_height = piece->height >= tile_height - 0.5, full_width = piece->width >= tile_width - 0.5;
    if (full_height && full_width)
        return PIECE_WHOLE;      /* фигурный кусок во всю плитку — из остатка не выйдет */
    if (full_height)
        return PIECE_ACROSS_WIDTH;
    if (full_width)
        return PIECE_ACROSS_HEIGHT;
    return PIECE_CORNER;
}

/* Пары: сначала по одному направлению (угловые куски — туда), потом по другому
   (оставшиеся угловые). width_first — сначала по ширине плитки. */
static TlOffcutPairList pair_in_order(const TlPieceList *pieces, double tile_width, double tile_height,
                                      double allowance, bool square, bool width_first)
{
    TlOffcutPairList pairs = {0};
    bool *used = tl_calloc(pieces->count + 1, sizeof *used);
    for (int pass = 0; pass < 2; pass++) {
        bool across_width = (pass == 0) == width_first;
        StripList strips = {0};
        for (size_t i = 0; i < pieces->count; i++) {
            const TlPiece *piece = &pieces->items[i];
            PieceShape shape = piece_shape(piece, tile_width, tile_height);
            if (used[i] || shape == PIECE_WHOLE)
                continue;
            if (square) { /* у квадрата куски поворачиваются: всё — по одному направлению */
                if (pass == 0)
                    strip_list_push(&strips, (Strip){fmin(piece->width, piece->height), i});
                continue;
            }
            bool fits = shape == PIECE_CORNER || shape == (across_width ? PIECE_ACROSS_WIDTH : PIECE_ACROSS_HEIGHT);
            if (fits)
                strip_list_push(&strips, (Strip){across_width ? piece->width : piece->height, i});
        }
        pair_strips(pieces, &strips, across_width || square ? tile_width : tile_height, allowance,
                    across_width || square, used, &pairs);
        strip_list_free(&strips);
    }
    free(used);
    return pairs;
}

static TlOffcutPairList pair_pieces(const TlPieceList *pieces, double tile_width, double tile_height,
                                    double allowance, bool square)
{
    TlOffcutPairList width_first = pair_in_order(pieces, tile_width, tile_height, allowance, square, true);
    TlOffcutPairList height_first = pair_in_order(pieces, tile_width, tile_height, allowance, square, false);
    if (height_first.count > width_first.count) {
        tl_offcut_pair_list_free(&width_first);
        return height_first;
    }
    tl_offcut_pair_list_free(&height_first);
    return width_first;
}

TlOffcutPairList tl_report_offcut_pairs(double width, double height, double allowance, const TlPieceList *pieces)
{
    return pair_pieces(pieces, width, height, allowance, fabs(width - height) < 0.5);
}

/* ---- сводка ---------------------------------------------------------------- */

void tl_report_tile_name(const TlTileSettings *tile, char *text, size_t size)
{
    double longer = fmax(tile->tile_width, tile->tile_height);
    double shorter = fmin(tile->tile_width, tile->tile_height);
    snprintf(text, size, "%g×%g мм", longer, shorter);
}

const TlTileSpec *tl_report_piece_spec(const TlPieceRef *ref)
{
    TlLayout *layout = tl_space_layout_at(ref->space, ref->layout);
    const TlTile *tile = layout ? tl_layout_tile_at(layout, ref->cell) : NULL;
    return tile ? tl_layout_spec(layout, tile) : NULL;
}

static TlTileTypeSummary *type_for(TlTileTypeSummaryList *types, const TlTileSpec *spec, const TlTileSettings *tile)
{
    for (size_t i = 0; i < types->count; i++)
        if (tl_tile_specs_same(&types->items[i].spec, spec))
            return &types->items[i];
    TlTileTypeSummary type = {.spec = *spec, .tile = tile};
    tl_tile_type_summary_list_push(types, type);
    return tl_tile_type_summary_list_last(types);
}

static void add_to_pack_line(TlPackLineList *lines, double unit_size, double price, double amount)
{
    for (size_t i = 0; i < lines->count; i++)
        if (lines->items[i].unit_size == unit_size && lines->items[i].price == price) {
            lines->items[i].amount += amount;
            return;
        }
    TlPackLine line = {unit_size, price, amount, 0};
    tl_pack_line_list_push(lines, line);
}

static int compare_pack_lines(const void *left, const void *right)
{
    const TlPackLine *a = left, *b = right;
    if (a->unit_size != b->unit_size)
        return a->unit_size < b->unit_size ? -1 : 1;
    return a->price < b->price ? -1 : a->price > b->price;
}

static void finish_pack_lines(TlPackLineList *lines)
{
    if (lines->count > 1)
        qsort(lines->items, lines->count, sizeof(TlPackLine), compare_pack_lines);
    for (size_t i = 0; i < lines->count; i++) {
        TlPackLine *line = &lines->items[i];
        line->units = line->unit_size > 0 ? (int)ceil(line->amount / line->unit_size - 1e-9) : 0;
    }
}

static int compare_types_by_area(const void *left, const void *right)
{
    const TlTileTypeSummary *a = left, *b = right;
    return a->area > b->area ? -1 : a->area < b->area;
}

/* Что копится по виду плитки, пока обходятся группы: куски для пар обрезков
 * из всех комнат с этим видом и плитки с весом запаса каждой комнаты. */
typedef struct TypeTally {
    TlPieceList pieces;          /* в осях вида (type->spec) */
    double allowance;            /* запас на рез — наибольший из комнат */
    double reserved;             /* Σ плиток × (1 + запас) */
} TypeTally;

TL_ARRAY_DEFINE(TypeTallyList, TypeTally, type_tally_list)

static bool lies_lengthwise(const TlTileSpec *spec) { return spec->laid_width >= spec->laid_height; }

static void add_space_once(TlSpaceList *spaces, TlSpace *space)
{
    for (size_t i = 0; i < spaces->count; i++)
        if (spaces->items[i] == space)
            return;
    tl_space_list_push(spaces, space);
}

/* Плитки вида spec в группе: сколько ячеек (плитка на стыке комнат — одна), из них целых, и
   сколько было бы, считая каждую комнату отдельно. */
typedef struct SpecCount {
    int unique, whole, naive;
} SpecCount;

static SpecCount count_spec(const TlLayoutGroup *group, int spec)
{
    SpecCount count = {0};
    for (size_t i = 0; i < group->owners.count; i++) {
        const TlCellOwner *owner = &group->owners.items[i];
        if (owner->tile->spec != spec)
            continue;
        count.naive++;
        size_t first, owners = tl_group_cell_owners(group, owner->cell, &first);
        if (first != i)
            continue;
        count.unique++;
        if (owners == 1 && owner->tile->kind == TL_TILE_WHOLE)
            count.whole++;
    }
    return count;
}

static void summarize_spec(TlSummary *summary, TypeTallyList *tallies, const TlLayoutGroup *group, int spec_index,
                           SpecCount count, double area)
{
    const TlTileSettings *tile = group->tile;
    const TlTileSpec *spec = &group->layouts.items[0]->pattern.specs[spec_index];
    TlTileTypeSummary *type = type_for(&summary->types, spec, tile);
    size_t index = (size_t)(type - summary->types.items);
    while (tallies->count <= index)
        type_tally_list_push(tallies, (TypeTally){0});
    TypeTally *tally = &tallies->items[index];
    for (size_t i = 0; i < group->members.count; i++)
        add_space_once(&type->spaces, group->members.items[i]);
    type->count += count.unique;
    type->whole += count.whole;
    type->area += area;
    type->shared_saving += count.naive - count.unique;
    tally->reserved += count.unique * (1 + tile->reserve_percent / 100.0);

    /* Остатки подрезок идут в подрезки любой комнаты с той же плиткой. Плитка
     * 600×300 в одной комнате и 300×600 в другой — одна: куски поворачиваются. */
    bool turned = lies_lengthwise(spec) != lies_lengthwise(&type->spec);
    bool reuse = tile->reuse_offcuts && spec->rectangular; /* куски шеврона и вставки — не полосы */
    TlPieceList pieces = tl_group_unshared_pieces(group);
    if (!type->pieces)
        type->pieces = TL_NEW(TlPieceList);
    for (size_t i = 0; i < pieces.count; i++) {
        TlPiece piece = pieces.items[i];
        if (piece.spec != spec_index)
            continue;
        if (turned)
            piece = (TlPiece){piece.ref, piece.height, piece.width, piece.spec};
        tl_piece_list_push(type->pieces, piece); /* для карты раскроя — все подрезки */
        if (reuse)
            tl_piece_list_push(&tally->pieces, piece);
    }
    tl_piece_list_free(&pieces);
    if (reuse && tile->cut_allowance > tally->allowance)
        tally->allowance = tile->cut_allowance;
}

static void summarize_group(TlSummary *summary, TypeTallyList *tallies, const TlLayoutGroup *group)
{
    const TlPatternUnit *pattern = &group->layouts.items[0]->pattern;
    double floor_area = 0.0, weight_total = 0.0;
    for (size_t i = 0; i < group->layouts.count; i++)
        floor_area += group->layouts.items[i]->floor_area;
    SpecCount counts[TL_PATTERN_MAX_SPECS];
    double weights[TL_PATTERN_MAX_SPECS];
    for (size_t s = 0; s < pattern->spec_count; s++) {
        counts[s] = count_spec(group, (int)s);
        weights[s] = counts[s].unique * pattern->specs[s].laid_width * pattern->specs[s].laid_height;
        weight_total += weights[s];
    }
    /* площадь пола делится между видами по площади их плиток */
    for (size_t s = 0; s < pattern->spec_count; s++)
        if (counts[s].naive > 0)
            summarize_spec(summary, tallies, group, (int)s, counts[s],
                           weight_total > 0 ? floor_area * weights[s] / weight_total : 0);

    for (size_t m = 0; m < group->members.count; m++) {
        TlRoomSummary *room = (TlRoomSummary *)tl_summary_room(summary, group->members.items[m]);
        TlLayout *layout = group->layouts.items[m];
        for (size_t t = 0; t < layout->tiles.count; t++)
            if (tl_group_cell_owners(group, layout->tiles.items[t].cell, NULL) > 1)
                room->shared_tiles++;
    }
}

/* Пары обрезков и запас — по виду плитки целиком: запас округляется один раз,
 * а не в каждой комнате. */
static void finish_type(TlSummary *summary, TlTileTypeSummary *type, TypeTally *tally)
{
    TlOffcutPairList pairs =
        tl_report_offcut_pairs(type->spec.laid_width, type->spec.laid_height, tally->allowance, &tally->pieces);
    double reserve = type->count > 0 ? tally->reserved / type->count : 1.0;
    type->offcut_saving = (int)pairs.count;
    type->count -= (int)pairs.count;
    type->to_buy = (int)ceil(type->count * reserve - 1e-9);
    if (type->tile->tile_pack > 0) /* упаковками: к покупке — целые упаковки */
        type->packs = (type->to_buy + type->tile->tile_pack - 1) / type->tile->tile_pack;
    for (size_t i = 0; i < pairs.count; i++)
        tl_offcut_pair_list_push(&summary->offcut_pairs, pairs.items[i]);
    tl_offcut_pair_list_free(&pairs);
    tl_piece_list_free(&tally->pieces);
}

/* Длина стен комнаты без дверей, мм — по ней заход гидроизоляции и лента. */
static double walls_without_doors(TlProject *project, TlSpace *space)
{
    double length = tl_space_room_perimeter(space);
    TlOpeningList doors = tl_project_space_doors(project, space);
    for (size_t i = 0; i < doors.count; i++) {
        TlVec2 from, to;
        if (tl_project_space_door_edge(project, space, doors.items[i], &from, &to))
            length -= tl_distance(from, to);
    }
    tl_opening_list_free(&doors);
    return fmax(0, length);
}

/* Поворот контура в вершине: знак векторного произведения соседних сторон. */
static double turn_at(const TlPolygon *polygon, size_t v)
{
    TlVec2 a = polygon->items[(v + polygon->count - 1) % polygon->count], b = polygon->items[v];
    TlVec2 c = polygon->items[(v + 1) % polygon->count];
    TlVec2 d1 = tl_vec2_normalized(tl_vec2_sub(b, a)), d2 = tl_vec2_normalized(tl_vec2_sub(c, b));
    return tl_vec2_cross(d1, d2);
}

/* Наружные углы стен в комнате: углы, выступающие внутрь неё (колонна, короб, выступ), и все
   выпуклые углы отдельно стоящих перегородок. */
static int external_corners(const TlSpace *space)
{
    const TlPolygon *room = tl_space_room_outline(space);
    double orientation = tl_polygon_signed_area(room) > 0 ? 1 : -1;
    int count = 0;
    for (size_t v = 0; v < room->count; v++)
        count += turn_at(room, v) * orientation < -0.05; /* поворот против обхода — угол внутрь комнаты */
    for (size_t h = 0; h < space->holes.count; h++) {
        const TlPolygon *hole = &space->holes.items[h];
        double hole_orientation = tl_polygon_signed_area(hole) > 0 ? 1 : -1;
        for (size_t v = 0; v < hole->count; v++)
            count += turn_at(hole, v) * hole_orientation > 0.05;
    }
    return count;
}

/* Гидроизоляция и уголки — по настройкам комнаты (основного пола). */
static void summarize_room_extras(TlSummary *summary, TlProject *project, TlRoomSummary *room)
{
    const TlTileSettings *tile = tl_space_effective_tile(room->space);
    if (tile->waterproof) {
        double walls = walls_without_doors(project, room->space);
        room->waterproof_area = tl_space_room_area(room->space) + walls * tile->waterproof_height;
        room->waterproof_kg = room->waterproof_area / 1e6 * tile->waterproof_kg_per_m2 * tile->waterproof_layers;
        room->tape_length = walls;
        add_to_pack_line(&summary->waterproof, tile->waterproof_pack_kg, tile->price_waterproof_pack, room->waterproof_kg);
        add_to_pack_line(&summary->tape, 1, tile->price_tape_m, walls / 1000);
        summary->waterproof_total_kg += room->waterproof_kg;
        summary->tape_total_m += walls / 1000;
    }
    if (tile->corner_profile_height > 0) {
        room->corner_count = external_corners(room->space);
        double length = room->corner_count * tile->corner_profile_height;
        if (length > 0)
            add_to_pack_line(&summary->corners, tile->profile_length, tile->price_corner, length);
        summary->corner_total_mm += length;
    }
}

/* Пороги — по проёму один раз: если у одной из его комнат включён порог-профиль. */
static void summarize_thresholds(TlSummary *summary, TlProject *project)
{
    for (size_t i = 0; i < project->openings.count; i++) {
        const TlOpening *opening = project->openings.items[i];
        for (size_t s = 0; s < project->spaces.count; s++) {
            TlSpace *space = project->spaces.items[s];
            const TlTileSettings *tile = tl_space_effective_tile(space);
            TlVec2 from, to;
            if (!tile->enabled || !tile->threshold_profile ||
                !tl_project_space_door_edge(project, space, opening, &from, &to))
                continue;
            add_to_pack_line(&summary->thresholds, tile->profile_length, tile->price_threshold, tl_distance(from, to));
            summary->threshold_total_mm += tl_distance(from, to);
            break;
        }
    }
}

/* Клей, СВП, затирка, грунт одной раскладки (on_wall — облицовка стен). */
static void add_layout_materials(TlSummary *summary, TlRoomSummary *room, const TlTileSettings *tile,
                                 const TlLayout *layout, bool on_wall)
{
    double glue = tl_layout_glue_kg(tile, layout), grout = tl_layout_grout_kg(tile, layout);
    double primer = tl_layout_primer_l(tile, layout);
    int clips = tile->use_crosses ? 0 : (int)layout->clip_points.count;
    int crosses = tile->use_crosses ? tl_layout_cross_count(layout) : 0;
    room->cut_length += tl_layout_cut_length(layout);
    room->cross_count += crosses;
    if (tile->use_crosses)
        add_to_pack_line(&summary->crosses, tile->crosses_per_pack, tile->price_cross_pack, crosses);
    room->glue_kg += glue;
    room->clip_count += clips;
    room->grout_kg += grout;
    room->primer_l += primer;
    if (on_wall) {
        room->wall_area += layout->floor_area;
        summary->wall_area += layout->floor_area;
    } else {
        room->tiled_area += layout->floor_area;
        summary->tiled_area += layout->floor_area;
    }
    add_to_pack_line(&summary->glue, tile->glue_bag_kg, tile->price_glue_bag, glue);
    if (!tile->use_crosses)
        add_to_pack_line(&summary->clips, tile->clips_per_pack, tile->price_clip_pack, clips);
    add_to_pack_line(&summary->grout, tile->grout_pack_kg, tile->price_grout_pack, grout);
    add_to_pack_line(&summary->primer, tile->primer_can_l, tile->price_primer_can, primer);
}

/* Клей, СВП, затирка, грунт комнаты — по всем её раскладкам (основной пол, зоны, стены) и панно. */
static void summarize_room_materials(TlSummary *summary, TlRoomSummary *room)
{
    for (size_t i = 0; i < tl_space_layout_count(room->space); i++)
        if (tl_space_layout_counted(room->space, i))
            add_layout_materials(summary, room, tl_space_layout_tile(room->space, i), tl_space_layout_at(room->space, i),
                                 tl_space_layout_on_wall(room->space, i));
    for (size_t i = 0; i < room->space->pannos.count; i++) {
        bool on_wall = room->space->pannos.items[i].wall != TL_PANNO_FLOOR;
        if (on_wall && !room->space->walls.enabled)
            continue;
        TlLayout *layout = tl_space_panno_layout(room->space, i);
        TlTileSettings tile = tl_space_panno_tile(room->space, i);
        if (layout && !layout->too_many)
            add_layout_materials(summary, room, &tile, layout, on_wall);
        if (layout)
            tl_layout_free(layout);
    }
    summary->glue_total_kg += room->glue_kg;
    summary->clip_total += room->clip_count;
    summary->grout_total_kg += room->grout_kg;
    summary->primer_total_l += room->primer_l;
    summary->cross_total += room->cross_count;
    summary->cut_total_mm += room->cut_length;
}

TlSummary tl_report_summarize(TlProject *project)
{
    TlSummary summary = {0};
    tl_report_assign_numbers(project);
    for (size_t i = 0; i < project->spaces.count; i++) {
        TlSpace *space = project->spaces.items[i];
        TlLayout *layout = tl_space_layout(space);
        TlRoomSummary room = {.space = space, .layout = layout, .floor_tiled = tl_space_layout_counted(space, 0)};
        for (size_t l = 0; l < tl_space_layout_count(space) && !room.tiled; l++)
            room.tiled = tl_space_layout_counted(space, l);
        for (size_t p = 0; p < space->pannos.count && !room.tiled; p++)
            room.tiled = space->pannos.items[p].wall == TL_PANNO_FLOOR ? room.floor_tiled : space->walls.enabled;
        summary.floor_area += tl_space_area(space);
        summary.room_area += tl_space_room_area(space);
        if (room.tiled)
            summarize_room_materials(&summary, &room);
        if (room.floor_tiled)
            summarize_room_extras(&summary, project, &room);
        tl_room_summary_list_push(&summary.rooms, room);
    }
    finish_pack_lines(&summary.glue);
    finish_pack_lines(&summary.clips);
    finish_pack_lines(&summary.grout);
    finish_pack_lines(&summary.primer);
    summarize_thresholds(&summary, project);
    TlPackLineList *more[] = {&summary.crosses, &summary.waterproof, &summary.tape, &summary.thresholds, &summary.corners};
    for (size_t i = 0; i < sizeof more / sizeof more[0]; i++)
        finish_pack_lines(more[i]);

    TlLayoutGroupList groups = tl_report_build_groups(project, NULL, NULL);
    TypeTallyList tallies = {0};
    for (size_t g = 0; g < groups.count; g++)
        summarize_group(&summary, &tallies, &groups.items[g]);
    for (size_t i = 0; i < summary.types.count; i++)
        finish_type(&summary, &summary.types.items[i], &tallies.items[i]);
    type_tally_list_free(&tallies);
    tl_report_free_groups(&groups);
    if (summary.types.count > 1) /* пустой проект: массива нет */
        qsort(summary.types.items, summary.types.count, sizeof(TlTileTypeSummary), compare_types_by_area);
    return summary;
}

const TlRoomSummary *tl_summary_room(const TlSummary *summary, const TlSpace *space)
{
    for (size_t i = 0; i < summary->rooms.count; i++)
        if (summary->rooms.items[i].space == space)
            return &summary->rooms.items[i];
    return NULL;
}

void tl_summary_free(TlSummary *summary)
{
    for (size_t i = 0; i < summary->types.count; i++) {
        tl_space_list_free(&summary->types.items[i].spaces);
        if (summary->types.items[i].pieces) {
            tl_piece_list_free(summary->types.items[i].pieces);
            free(summary->types.items[i].pieces);
        }
    }
    tl_room_summary_list_free(&summary->rooms);
    tl_tile_type_summary_list_free(&summary->types);
    tl_pack_line_list_free(&summary->glue);
    tl_pack_line_list_free(&summary->clips);
    tl_pack_line_list_free(&summary->grout);
    tl_pack_line_list_free(&summary->primer);
    tl_pack_line_list_free(&summary->crosses);
    tl_pack_line_list_free(&summary->waterproof);
    tl_pack_line_list_free(&summary->tape);
    tl_pack_line_list_free(&summary->thresholds);
    tl_pack_line_list_free(&summary->corners);
    tl_offcut_pair_list_free(&summary->offcut_pairs);
}

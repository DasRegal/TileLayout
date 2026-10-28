#include "app_commands.h"

#include <math.h>
#include <stdint.h>

#include "app_catalogs.h"
#include "tl_layout.h"

static const char *LOCKED_MESSAGE = "Заблокировано от перемещения — снимите блокировку на панели";

static bool partition_inside(const TlPartition *partition, const TlOutline *outline)
{
    return tl_outline_contains_or_touches(outline, partition->start) &&
           tl_outline_contains_or_touches(outline, partition->end);
}

bool app_selection_is_locked(TlProject *project, const AppSelection *s)
{
    switch (s->kind) {
    case APP_SELECT_VERTEX: return tl_outline_vertex_locked(s->outline, (size_t)s->index);
    case APP_SELECT_WALL: return s->outline->locked.items[s->index];
    case APP_SELECT_PARTITION: return s->partition->locked;
    case APP_SELECT_OPENING: return s->opening->locked;
    case APP_SELECT_SPACE: {
        TlOutline *house = s->space->house;
        if (!house)
            return false;
        if (tl_outline_any_locked(house))
            return true;
        for (size_t i = 0; i < project->partitions.count; i++)
            if (project->partitions.items[i]->locked && partition_inside(project->partitions.items[i], house))
                return true;
        for (size_t i = 0; i < project->openings.count; i++) {
            TlBand band;
            if (project->openings.items[i]->locked &&
                tl_project_opening_host(project, project->openings.items[i], &band) &&
                band.host.kind == TL_HOST_WALL && band.host.outline == house)
                return true;
        }
        return false;
    }
    default: return false;
    }
}

/* ---- добавление -------------------------------------------------------------- */

static void select_space_at(AppDocument *document, TlVec2 point)
{
    TlSpace *space = tl_project_space_at(document->project, point);
    document->selection = space ? (AppSelection){.kind = APP_SELECT_SPACE, .space = space} : (AppSelection){0};
}

void app_command_add_contour(AppDocument *document, const TlVec2 *corners, size_t corner_count)
{
    app_document_checkpoint(document, "");
    TlPolygon contour = tl_polygon_from_points(corners, corner_count);
    TlVec2 inside_point = tl_polygon_label_point(&contour);
    tl_polygon_free(&contour);
    TlContourResult result = tl_project_add_contour(document->project, corners, corner_count,
                                                    document->preferences.house_thickness,
                                                    document->preferences.partition_thickness);
    select_space_at(document, inside_point);
    if (result == TL_CONTOUR_PARTITIONS)
        app_document_status(document, "Перегородки добавлены, но замкнутой комнаты не получилось");
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_house(AppDocument *document, TlVec2 origin, double width, double height,
                           double thickness)
{
    const TlVec2 corners[] = {origin, {origin.x + width, origin.y}, {origin.x + width, origin.y + height},
                              {origin.x, origin.y + height}};
    app_document_checkpoint(document, "");
    tl_project_add_outline(document->project, tl_outline_new(corners, 4, thickness));
    tl_project_update_spaces(document->project);
    select_space_at(document, tl_vec2(origin.x + width / 2, origin.y + height / 2));
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_partition(AppDocument *document, TlVec2 start, TlVec2 end)
{
    if (tl_distance(start, end) < 1)
        return;
    app_document_checkpoint(document, "");
    TlPartition *partition = tl_project_draw_partition(document->project, start, end,
                                                       document->preferences.partition_thickness);
    document->selection = partition ? (AppSelection){.kind = APP_SELECT_PARTITION, .partition = partition}
                                    : (AppSelection){0};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_opening(AppDocument *document, const TlBand *band, double along)
{
    if (band->length < document->preferences.opening_width) {
        app_document_status(document, "Стена короче ширины проёма");
        return;
    }
    app_document_checkpoint(document, "");
    TlOpening *opening = tl_project_place_opening(document->project, band, along,
                                                  document->preferences.opening_width);
    document->selection = (AppSelection){.kind = APP_SELECT_OPENING, .opening = opening};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_ruler(AppDocument *document, TlVec2 start, TlVec2 end)
{
    if (tl_distance(start, end) < 1)
        return;
    app_document_checkpoint(document, "");
    TlRuler *ruler = tl_ruler_new(start, end);
    tl_project_add_ruler(document->project, ruler);
    document->selection = (AppSelection){.kind = APP_SELECT_RULER, .ruler = ruler};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_divider(AppDocument *document, TlVec2 start, TlVec2 end)
{
    if (tl_distance(start, end) < 1)
        return;
    app_document_checkpoint(document, "");
    TlExpansionJoint *line = tl_expansion_joint_new(start, end, document->preferences.divider_gap);
    line->kind = TL_LINE_DIVIDER;
    tl_project_add_expansion_joint(document->project, line);
    tl_project_update_spaces(document->project);
    document->selection = (AppSelection){.kind = APP_SELECT_EXPANSION_JOINT, .expansion_joint = line};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
    app_document_status(document, "Линия раздела. Щёлкните часть комнаты, чтобы задать её покрытие");
}

void app_command_add_expansion_joint(AppDocument *document, TlVec2 start, TlVec2 end)
{
    if (tl_distance(start, end) < 1)
        return;
    app_document_checkpoint(document, "");
    TlExpansionJoint *joint = tl_expansion_joint_new(start, end, document->preferences.expansion_joint_gap);
    tl_project_add_expansion_joint(document->project, joint);
    tl_project_update_spaces(document->project);
    document->selection = (AppSelection){.kind = APP_SELECT_EXPANSION_JOINT, .expansion_joint = joint};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_drain(AppDocument *document, TlDrain drain)
{
    app_document_checkpoint(document, "");
    tl_drain_list_push(&document->project->drains, drain);
    tl_project_update_spaces(document->project);
    document->selection = (AppSelection){.kind = APP_SELECT_DRAIN, .index = (int)document->project->drains.count - 1};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_fixture(AppDocument *document, TlFixture fixture)
{
    app_document_checkpoint(document, "");
    tl_fixture_list_push(&document->project->fixtures, fixture);
    tl_project_update_spaces(document->project);
    document->selection =
        (AppSelection){.kind = APP_SELECT_FIXTURE, .index = (int)document->project->fixtures.count - 1};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
    app_document_status(document, "%s: под ней %s, за ней на стенах %s — поменять можно на панели",
                        tl_fixture_kind_name(fixture.kind), fixture.under ? "плитка" : "без плитки",
                        fixture.behind ? "плитка" : "без плитки");
}

void app_command_add_pipe(AppDocument *document, TlVec2 center)
{
    app_document_checkpoint(document, "");
    TlPipe *pipe = tl_pipe_new(center, document->preferences.pipe_diameter);
    tl_project_add_pipe(document->project, pipe);
    tl_project_update_spaces(document->project);
    document->selection = (AppSelection){.kind = APP_SELECT_PIPE, .pipe = pipe};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_delete_all_rulers(AppDocument *document)
{
    if (document->project->rulers.count == 0)
        return;
    app_document_checkpoint(document, "");
    tl_project_delete_all_rulers(document->project);
    app_document_commit(document);
}

void app_command_insert_vertex(AppDocument *document, TlOutline *outline, size_t wall, TlVec2 point)
{
    if (outline->locked.items[wall]) {
        app_document_status(document, "Стена заблокирована");
        return;
    }
    app_document_checkpoint(document, "");
    tl_outline_insert_vertex(outline, wall, point);
    tl_project_update_spaces(document->project);
    document->selection = (AppSelection){.kind = APP_SELECT_VERTEX, .outline = outline, .index = (int)wall + 1};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_set_vertex(AppDocument *document, TlOutline *outline, size_t index, TlVec2 point)
{
    app_document_checkpoint(document, "");
    if (!tl_project_set_vertex(document->project, outline, index, point)) {
        app_document_status(document, "Стена у этого угла заблокирована");
        return;
    }
    app_document_commit(document);
}

void app_command_set_node_point(AppDocument *document, const AppSelection *selection, TlVec2 point)
{
    TlNode node;
    if (!app_selection_node(selection, document->project, &node))
        return;
    app_document_checkpoint(document, "");
    if (!tl_project_set_node_point(document->project, &node, selection->index, point)) {
        app_document_status(document, "Перегородка в узле заблокирована");
        return;
    }
    app_document_commit(document);
}

void app_command_set_tee_point(AppDocument *document, const AppSelection *selection, TlVec2 point)
{
    AppSelection kept = *selection;
    bool became_node = false;
    app_document_checkpoint(document, "");
    if (!tl_project_set_tee_point(document->project, selection->partition, selection->end, point, &became_node)) {
        app_document_status(document, "Перегородка заблокирована");
        return;
    }
    if (became_node)
        kept.kind = APP_SELECT_NODE; /* ушла за грань — теперь это узел */
    app_document_commit(document);
    app_document_select(document, kept);
}

void app_command_set_tile_texture(AppDocument *document, const TlCatalog *catalog, const TlCatalogTile *tile)
{
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = app_selection_spaces(&document->selection, rooms);
    if (count == 0)
        return;
    app_document_checkpoint(document, "");
    for (size_t i = 0; i < count; i++) {
        TlTileSettings *settings = tl_space_effective_tile(rooms[i]);
        if (catalog && tile)
            app_catalogs_apply(catalog, tile, settings);
        else
            settings->texture[0] = '\0';
        tl_tile_settings_remember_as_default(settings);
    }
    app_document_commit(document);
}

TlZone *app_space_zone(TlSpace *space, int zone)
{
    if (zone >= APP_ZONE_TOTAL)
        return &space->areas.items[zone - APP_ZONE_TOTAL].zone;
    return zone < TL_ZONE_COUNT ? &space->zones[zone] : &space->walls.bands[zone - APP_ZONE_WALL(0)];
}

void app_command_set_zone_texture(AppDocument *document, int zone, const TlCatalog *catalog,
                                  const TlCatalogTile *tile)
{
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = app_selection_spaces(&document->selection, rooms);
    if (count == 0)
        return;
    app_document_checkpoint(document, "");
    for (size_t i = 0; i < count; i++) {
        TlTileSettings *settings = &app_space_zone(rooms[i], zone)->tile;
        if (catalog && tile)
            app_catalogs_apply(catalog, tile, settings);
        else
            settings->texture[0] = '\0';
    }
    app_document_commit(document);
}

/* ---- удаление ---------------------------------------------------------------- */

void app_command_delete_selection(AppDocument *document)
{
    AppSelection s = document->selection;
    TlProject *project = document->project;
    if (s.kind == APP_SELECT_NONE)
        return;
    if ((s.kind == APP_SELECT_OPENING || s.kind == APP_SELECT_PARTITION || s.kind == APP_SELECT_VERTEX) &&
        app_selection_is_locked(project, &s)) {
        app_document_status(document, "Заблокировано — снимите блокировку, чтобы удалить");
        return;
    }
    switch (s.kind) {
    case APP_SELECT_WALL_PIPE:
    case APP_SELECT_WALL_RULER:
    case APP_SELECT_PANNO:
        app_document_checkpoint(document, "");
        if (s.kind == APP_SELECT_WALL_PIPE)
            tl_wall_pipe_list_remove_at(&s.space->wall_pipes, (size_t)s.index);
        else if (s.kind == APP_SELECT_PANNO)
            tl_panno_list_remove_at(&s.space->pannos, (size_t)s.index);
        else
            tl_wall_ruler_list_remove_at(&s.space->wall_rulers, (size_t)s.index);
        document->selection = (AppSelection){.kind = APP_SELECT_SPACE, .space = s.space};
        app_document_commit(document);
        app_document_notify(document, APP_CHANGED_SELECTION);
        return;
    case APP_SELECT_ROOM_WALL:
        app_document_status(document, "Стены образуют план: правьте их на вкладке «План»");
        return;
    case APP_SELECT_DRAIN:
        app_document_checkpoint(document, "");
        tl_drain_list_remove_at(&project->drains, (size_t)s.index);
        tl_project_update_spaces(project);
        break;
    case APP_SELECT_FIXTURE:
        app_document_checkpoint(document, "");
        tl_fixture_list_remove_at(&project->fixtures, (size_t)s.index);
        tl_project_update_spaces(project);
        break;
    case APP_SELECT_RULER:
        app_document_checkpoint(document, "");
        tl_project_delete_ruler(project, s.ruler);
        break;
    case APP_SELECT_EXPANSION_JOINT:
        app_document_checkpoint(document, "");
        tl_project_delete_expansion_joint(project, s.expansion_joint);
        tl_project_update_spaces(project);
        break;
    case APP_SELECT_PIPE:
        app_document_checkpoint(document, "");
        tl_project_delete_pipe(project, s.pipe);
        tl_project_update_spaces(project);
        break;
    case APP_SELECT_OPENING:
        app_document_checkpoint(document, "");
        tl_project_delete_opening(project, s.opening);
        tl_project_update_spaces(project); /* пол проёма уходит из комнат */
        break;
    case APP_SELECT_NODE: { /* узел убирается: сломанная перегородка выпрямляется */
        TlNode node;
        if (!app_selection_node(&s, project, &node))
            return;
        if (tl_node_locked(&node)) {
            app_document_status(document, "Перегородка в узле заблокирована");
            return;
        }
        app_document_checkpoint(document, "");
        tl_project_dissolve_node(project, &node);
        tl_project_update_spaces(project);
        break;
    }
    case APP_SELECT_PARTITION:
        app_document_checkpoint(document, "");
        tl_project_delete_partition(project, s.partition);
        tl_project_remove_orphan_openings(project);
        tl_project_update_spaces(project);
        break;
    case APP_SELECT_VERTEX: {
        if (tl_outline_wall_count(s.outline) <= 3) {
            app_document_status(document, "У контура должно быть минимум 3 угла");
            return;
        }
        TlEditToken *token = app_document_begin_geometry(document, "", NULL);
        tl_outline_remove_vertex(s.outline, (size_t)s.index);
        tl_project_end_edit(project, token, NULL);
        tl_project_remove_orphan_openings(project);
        break;
    }
    case APP_SELECT_WALL:
        app_document_status(document, "Наружную стену удалить нельзя: удалите угол, чтобы убрать излом, "
                                      "или весь контур кнопкой на панели");
        return;
    default:
        app_document_status(document, "Комната образована стенами: удалите перегородку "
                                      "или весь контур кнопкой «Удалить контур целиком»");
        return;
    }
    document->selection = (AppSelection){0};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

/* ---- стены комнаты ------------------------------------------------------------ */

void app_command_add_wall_pipe(AppDocument *document, TlSpace *space, size_t wall, TlVec2 at, double diameter)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    if (!room_wall)
        return;
    app_document_checkpoint(document, "");
    tl_wall_pipe_list_push(&space->wall_pipes, (TlWallPipe){room_wall->edge, at, diameter});
    document->selection =
        (AppSelection){.kind = APP_SELECT_WALL_PIPE, .space = space, .index = (int)space->wall_pipes.count - 1};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_wall_ruler(AppDocument *document, TlSpace *space, size_t wall, TlVec2 start, TlVec2 end)
{
    const TlRoomWall *room_wall = tl_space_wall_at(space, wall);
    if (!room_wall || tl_distance(start, end) < 1)
        return;
    app_document_checkpoint(document, "");
    tl_wall_ruler_list_push(&space->wall_rulers, (TlWallRuler){room_wall->edge, start, end});
    document->selection =
        (AppSelection){.kind = APP_SELECT_WALL_RULER, .space = space, .index = (int)space->wall_rulers.count - 1};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_panno(AppDocument *document, TlSpace *space, long wall)
{
    const TlRoomWall *room_wall = wall >= 0 ? tl_space_wall_at(space, (size_t)wall) : NULL;
    if (wall >= 0 && !room_wall)
        return;
    TlPanno panno = tl_panno_default(room_wall ? room_wall->edge : TL_PANNO_FLOOR);
    if (room_wall) /* посередине стены */
        panno.at.x = fmax(0, (room_wall->length - tl_panno_width(&panno)) / 2);
    app_document_checkpoint(document, "");
    tl_panno_list_push(&space->pannos, panno);
    document->selection = (AppSelection){.kind = APP_SELECT_PANNO, .space = space, .index = (int)space->pannos.count - 1};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_set_panno_texture(AppDocument *document, const TlCatalog *catalog, const TlCatalogTile *tile)
{
    const AppSelection *s = &document->selection;
    if (s->kind != APP_SELECT_PANNO)
        return;
    TlPanno *panno = &s->space->pannos.items[s->index];
    app_document_checkpoint(document, "");
    if (catalog && tile) { /* картинка и её цвет — как у плитки из каталога */
        TlTileSettings settings = tl_panno_tile(panno, tl_space_effective_tile(s->space));
        app_catalogs_apply(catalog, tile, &settings);
        snprintf(panno->texture, sizeof panno->texture, "%s", settings.texture);
        snprintf(panno->color, sizeof panno->color, "%s", settings.color);
    } else {
        panno->texture[0] = '\0';
    }
    app_document_commit(document);
}

void app_command_set_area_covering(AppDocument *document, TlSpace *space, size_t region, int covering)
{
    size_t area = tl_space_area_of_region(space, region);
    if (covering == 0 && area == SIZE_MAX)
        return;
    app_document_checkpoint(document, "");
    if (covering == 0)
        tl_space_remove_area(space, area);
    else
        tl_space_add_area(space, region, covering == 1);
    app_document_commit(document);
}

void app_command_align_walls_to_floor(AppDocument *document, TlSpace *space, long wall)
{
    app_document_checkpoint(document, "");
    size_t aligned = 0, count = tl_space_wall_count(space);
    for (size_t w = 0; w < count; w++)
        if ((wall < 0 || (size_t)wall == w) && tl_space_wall_align_floor(space, w))
            aligned++;
    app_document_commit(document);
    if (aligned == 0)
        app_document_status(document, "Швы пола у стены не вдоль неё (или нет облицовки) — совмещать не с чем");
    else if (wall < 0)
        app_document_status(document, "Швы стен совмещены со швами пола: %zu из %zu", aligned, count);
    else
        app_document_status(document, "Шов стены продолжает шов пола");
}

void app_command_delete_outline(AppDocument *document)
{
    TlOutline *outline = app_selection_outline(&document->selection);
    if (!outline)
        return;
    if (tl_outline_any_locked(outline)) {
        app_document_status(document, "В контуре есть заблокированные стены");
        return;
    }
    app_document_checkpoint(document, "");
    tl_project_delete_outline(document->project, outline);
    document->selection = (AppSelection){0};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

/* ---- сдвиг стрелками ------------------------------------------------------------ */

static void nudge_opening(TlProject *project, TlOpening *opening, TlVec2 offset)
{
    TlBand band;
    if (!tl_project_opening_host(project, opening, &band))
        return;
    double along;
    tl_band_local(&band, opening->center, &along, NULL);
    opening->center = tl_band_point(&band, along + tl_vec2_dot(offset, band.direction), tl_band_middle(&band));
}

void app_command_nudge(AppDocument *document, double dx, double dy, bool move_tiles)
{
    AppSelection s = document->selection;
    TlProject *project = document->project;
    TlVec2 offset = tl_vec2(dx, dy);
    bool tiles_mode = move_tiles && s.kind == APP_SELECT_SPACE;
    if (s.kind == APP_SELECT_NONE)
        return;
    if (!tiles_mode && app_selection_is_locked(project, &s)) {
        app_document_status(document, "%s", LOCKED_MESSAGE);
        return;
    }
    if (tiles_mode) {
        app_document_checkpoint(document, "nudge");
        TlTileSettings *tile = tl_space_effective_tile(s.space);
        tile->offset_x += dx;
        tile->offset_y += dy;
    } else if (s.kind == APP_SELECT_RULER) {
        app_document_checkpoint(document, "nudge");
        tl_ruler_translate(s.ruler, offset);
    } else if (s.kind == APP_SELECT_EXPANSION_JOINT) {
        app_document_checkpoint(document, "nudge");
        tl_expansion_joint_translate(s.expansion_joint, offset);
        tl_project_update_spaces(project);
    } else if (s.kind == APP_SELECT_PIPE) {
        app_document_checkpoint(document, "nudge");
        s.pipe->center = tl_vec2_add(s.pipe->center, offset);
        tl_project_update_spaces(project);
    } else if (s.kind == APP_SELECT_OPENING) {
        app_document_checkpoint(document, "nudge");
        nudge_opening(project, s.opening, offset);
        tl_project_update_spaces(project);
    } else if (s.kind == APP_SELECT_SPACE) {
        TlOutline *house = s.space->house;
        TlEditToken *token = app_document_begin_geometry(document, "nudge", house);
        tl_outline_translate(house, offset);
        TlEditOptions options = {.translate = offset, .has_translate = true};
        tl_project_end_edit(project, token, &options);
    } else {
        TlEditToken *token = app_document_begin_geometry(document, "nudge", NULL);
        TlEditOptions options = {0};
        if (s.kind == APP_SELECT_PARTITION) {
            TlVec2 axis = tl_partition_direction(s.partition);
            options.master = s.partition;
            if (s.index == 0) { /* тело — только поперёк перегородки */
                TlVec2 across = tl_vec2_perpendicular(axis);
                tl_partition_translate(s.partition, tl_vec2_scale(across, tl_vec2_dot(offset, across)));
            } else {           /* конец — только вдоль оси */
                TlVec2 *end = tl_partition_end_point(s.partition, (TlPartitionEnd)s.index);
                *end = tl_vec2_add(*end, tl_vec2_scale(axis, tl_vec2_dot(offset, axis)));
                options.frozen = s.partition;
            }
        } else if (s.kind == APP_SELECT_VERTEX) {
            s.outline->inner.items[s.index] = tl_vec2_add(s.outline->inner.items[s.index], offset);
        } else if (s.kind == APP_SELECT_WALL) {
            TlVec2 normal;
            tl_outline_wall_frame(s.outline, (size_t)s.index, NULL, &normal);
            tl_project_shift_wall(project, s.outline, (size_t)s.index,
                                  tl_vec2_scale(normal, tl_vec2_dot(offset, normal)),
                                  document->preferences.link_walls);
        }
        tl_project_end_edit(project, token, &options);
    }
    app_document_commit(document);
}

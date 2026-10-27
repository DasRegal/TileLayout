#include "app_commands.h"

#include <math.h>

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

#include "app_commands.h"

#include <math.h>

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

static TlOutline *outline_containing(TlProject *project, const TlVec2 *corners, size_t count)
{
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        bool all_inside = true;
        for (size_t i = 0; i < count && all_inside; i++)
            all_inside = tl_outline_contains_or_touches(outline, corners[i]);
        if (all_inside)
            return outline;
    }
    return NULL;
}

static bool edge_lies_on_face(const TlFaceList *faces, TlVec2 a, TlVec2 b)
{
    for (size_t i = 0; i < faces->count; i++)
        if (tl_point_segment_distance(a, faces->items[i].start, faces->items[i].end, NULL) < 1 &&
            tl_point_segment_distance(b, faces->items[i].start, faces->items[i].end, NULL) < 1)
            return true;
    return false;
}

/* Перегородки по сторонам нарисованной комнаты: её внутренние размеры равны
 * нарисованным; стороны, легшие на существующие стены, пропускаются, у
 * соседних с ними перегородок конец — на этой стене. */
static void add_partitions_around(AppDocument *document, const TlVec2 *corners, size_t n)
{
    TlProject *project = document->project;
    double thickness = document->preferences.partition_thickness;
    TlPolygon contour = tl_polygon_from_points(corners, n);
    TlFaceList faces = tl_project_faces(project);
    bool *on_wall = g_new0(bool, n);
    TlVec2 *line_points = g_new(TlVec2, n), *directions = g_new(TlVec2, n), *normals = g_new(TlVec2, n);
    for (size_t i = 0; i < n; i++) {
        on_wall[i] = edge_lies_on_face(&faces, corners[i], corners[(i + 1) % n]);
        tl_polygon_edge_frame(&contour, i, &directions[i], &normals[i]);
        line_points[i] = tl_vec2_add(corners[i], tl_vec2_scale(normals[i], thickness / 2));
    }
    for (size_t i = 0; i < n; i++) {
        if (on_wall[i])
            continue;
        TlVec2 ends[2];
        for (int side = 0; side < 2; side++) {
            size_t neighbour = side == 0 ? (i + n - 1) % n : (i + 1) % n;
            double sign = side == 0 ? -1 : 1;
            TlVec2 hit;
            bool found;
            if (on_wall[neighbour]) {
                found = tl_line_intersection(line_points[i], directions[i], corners[neighbour],
                                             directions[neighbour], &hit);
            } else {
                found = tl_line_intersection(line_points[i], directions[i], line_points[neighbour],
                                             directions[neighbour], &hit);
                if (found) /* заходим на соседнюю перегородку, чтобы угол замкнулся */
                    hit = tl_vec2_add(hit, tl_vec2_scale(directions[i], sign * thickness / 2));
            }
            if (!found) {
                TlVec2 corner = side == 0 ? corners[i] : corners[(i + 1) % n];
                hit = tl_vec2_add(corner, tl_vec2_scale(normals[i], thickness / 2));
            }
            ends[side] = hit;
        }
        tl_project_add_partition(project, tl_partition_new(ends[0], ends[1], thickness));
    }
    g_free(on_wall);
    g_free(line_points);
    g_free(directions);
    g_free(normals);
    tl_face_list_free(&faces);
    tl_polygon_free(&contour);
}

static void select_space_at(AppDocument *document, TlVec2 point)
{
    TlSpace *space = tl_project_space_at(document->project, point);
    document->selection = space ? (AppSelection){.kind = APP_SELECT_SPACE, .space = space} : (AppSelection){0};
}

void app_command_add_contour(AppDocument *document, const TlVec2 *corners, size_t corner_count)
{
    TlProject *project = document->project;
    app_document_checkpoint(document, "");
    TlPolygon contour = tl_polygon_from_points(corners, corner_count);
    TlVec2 inside_point = tl_polygon_label_point(&contour);
    tl_polygon_free(&contour);
    if (outline_containing(project, corners, corner_count)) {
        size_t rooms_before = project->spaces.count;
        add_partitions_around(document, corners, corner_count);
        tl_project_update_spaces(project);
        select_space_at(document, inside_point);
        if (project->spaces.count <= rooms_before)
            app_document_status(document, "Перегородки добавлены, но замкнутой комнаты не получилось");
    } else {
        tl_project_add_outline(project, tl_outline_new(corners, corner_count,
                                                       document->preferences.house_thickness));
        tl_project_update_spaces(project);
        select_space_at(document, inside_point);
    }
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
    TlPartition *partition = tl_partition_new(start, end, document->preferences.partition_thickness);
    tl_project_add_partition(document->project, partition);
    tl_project_update_spaces(document->project);
    document->selection = (AppSelection){.kind = APP_SELECT_PARTITION, .partition = partition};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_command_add_opening(AppDocument *document, const TlBand *band, double along)
{
    double width = document->preferences.opening_width;
    if (band->length < width) {
        app_document_status(document, "Стена короче ширины проёма");
        return;
    }
    app_document_checkpoint(document, "");
    TlOpening *opening = tl_opening_new(tl_band_point(band, along, tl_band_middle(band)), width);
    tl_project_add_opening(document->project, opening);
    document->selection = (AppSelection){.kind = APP_SELECT_OPENING, .opening = opening};
    app_document_commit(document);
    app_document_notify(document, APP_CHANGED_SELECTION);
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
    case APP_SELECT_OPENING:
        app_document_checkpoint(document, "");
        tl_project_delete_opening(project, s.opening);
        break;
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
    } else if (s.kind == APP_SELECT_OPENING) {
        app_document_checkpoint(document, "nudge");
        nudge_opening(project, s.opening, offset);
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

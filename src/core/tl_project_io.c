#include "tl_project_io.h"

#include <stdio.h>
#include <string.h>

#define FORMAT_VERSION 3

/* ---- настройки плитки (таблица полей — в tl_tile_settings) ------------- */

static TlJson *tile_to_json(const TlTileSettings *tile)
{
    TlJson *object = tl_json_object();
    const char *base = (const char *)tile;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++) {
        const TlTileField *field = &fields[i];
        const void *member = base + field->offset;
        TlJson *value = field->type == TL_FIELD_BOOL     ? tl_json_bool(*(const bool *)member)
                        : field->type == TL_FIELD_DOUBLE ? tl_json_number(*(const double *)member)
                        : field->type == TL_FIELD_INT    ? tl_json_number(*(const int *)member)
                                                         : tl_json_string((const char *)member);
        tl_json_object_set(object, field->key, value);
    }
    return object;
}

/* Отсутствующие в файле поля — из настроек для новой комнаты. */
static TlTileSettings tile_from_json(const TlJson *object)
{
    TlTileSettings tile = tl_tile_settings_for_new_room();
    char *base = (char *)&tile;
    size_t count;
    const TlTileField *fields = tl_tile_settings_fields(&count);
    for (size_t i = 0; i < count; i++) {
        const TlTileField *field = &fields[i];
        const TlJson *value = tl_json_get(object, field->key);
        if (!value)
            continue;
        void *member = base + field->offset;
        switch (field->type) {
        case TL_FIELD_BOOL: *(bool *)member = tl_json_bool_or(value, *(bool *)member); break;
        case TL_FIELD_DOUBLE: *(double *)member = tl_json_number_or(value, *(double *)member); break;
        case TL_FIELD_INT: *(int *)member = (int)tl_json_number_or(value, *(int *)member); break;
        case TL_FIELD_COLOR:
            snprintf((char *)member, TL_COLOR_LENGTH, "%s", tl_json_string_or(value, (char *)member));
            break;
        }
    }
    return tile;
}

/* ---- точки и списки ---------------------------------------------------- */

static TlJson *point_to_json(TlVec2 point)
{
    TlJson *pair = tl_json_array();
    tl_json_array_push(pair, tl_json_number(point.x));
    tl_json_array_push(pair, tl_json_number(point.y));
    return pair;
}

static TlVec2 point_from_json(const TlJson *pair)
{
    return tl_vec2(tl_json_number_or(tl_json_at(pair, 0), 0), tl_json_number_or(tl_json_at(pair, 1), 0));
}

static TlJson *polygon_to_json(const TlPolygon *polygon)
{
    TlJson *array = tl_json_array();
    for (size_t i = 0; i < polygon->count; i++)
        tl_json_array_push(array, point_to_json(polygon->items[i]));
    return array;
}

static TlPolygon polygon_from_json(const TlJson *array)
{
    TlPolygon polygon = {0};
    for (size_t i = 0; i < tl_json_length(array); i++)
        tl_polygon_push(&polygon, point_from_json(tl_json_at(array, i)));
    return polygon;
}

/* ---- сущности ------------------------------------------------------------ */

static TlJson *outline_to_json(const TlOutline *outline)
{
    TlJson *object = tl_json_object();
    TlJson *thickness = tl_json_array(), *locked = tl_json_array();
    for (size_t i = 0; i < outline->thickness.count; i++) {
        tl_json_array_push(thickness, tl_json_number(outline->thickness.items[i]));
        tl_json_array_push(locked, tl_json_bool(outline->locked.items[i]));
    }
    tl_json_object_set(object, "pts", polygon_to_json(&outline->inner));
    tl_json_object_set(object, "thick", thickness);
    tl_json_object_set(object, "locked", locked);
    return object;
}

static TlOutline *outline_from_json(const TlJson *object)
{
    TlPolygon corners = polygon_from_json(tl_json_get(object, "pts"));
    TlOutline *outline = tl_outline_new(corners.items, corners.count, 200);
    tl_polygon_free(&corners);
    const TlJson *thickness = tl_json_get(object, "thick"), *locked = tl_json_get(object, "locked");
    for (size_t i = 0; i < outline->thickness.count; i++) {
        outline->thickness.items[i] = tl_json_number_or(tl_json_at(thickness, i), 200);
        outline->locked.items[i] = tl_json_bool_or(tl_json_at(locked, i), false);
    }
    return outline;
}

static TlJson *partition_to_json(const TlPartition *partition)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p1", point_to_json(partition->start));
    tl_json_object_set(object, "p2", point_to_json(partition->end));
    tl_json_object_set(object, "thick", tl_json_number(tl_partition_thickness_at(partition, TL_PARTITION_START)));
    TlJson *corners = tl_json_array(); /* отступы углов тела от оси, по порядку TlPartitionCorner */
    for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++)
        tl_json_array_push(corners, tl_json_number(partition->corner_offsets[corner]));
    tl_json_object_set(object, "corners", corners);
    tl_json_object_set(object, "locked", tl_json_bool(partition->locked));
    return object;
}

/* Форма: четыре угла; в старых файлах — толщина и, может быть, толщина у конца. */
static void read_partition_shape(TlPartition *partition, const TlJson *object)
{
    const TlJson *corners = tl_json_get(object, "corners");
    if (tl_json_length(corners) == TL_PARTITION_CORNER_COUNT) {
        for (int corner = 0; corner < TL_PARTITION_CORNER_COUNT; corner++)
            partition->corner_offsets[corner] = tl_json_number_or(tl_json_at(corners, (size_t)corner), 50);
        return;
    }
    double start_thickness = tl_partition_thickness_at(partition, TL_PARTITION_START);
    double end_thickness = tl_json_number_or(tl_json_get(object, "thick_end"), start_thickness);
    partition->corner_offsets[TL_PARTITION_END_LEFT] = end_thickness / 2;
    partition->corner_offsets[TL_PARTITION_END_RIGHT] = end_thickness / 2;
}

static TlPartition *partition_from_json(const TlJson *object)
{
    TlPartition *partition = tl_partition_new(point_from_json(tl_json_get(object, "p1")),
                                              point_from_json(tl_json_get(object, "p2")),
                                              tl_json_number_or(tl_json_get(object, "thick"), 100));
    read_partition_shape(partition, object);
    partition->locked = tl_json_bool_or(tl_json_get(object, "locked"), false);
    return partition;
}

static TlJson *ruler_to_json(const TlRuler *ruler)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p1", point_to_json(ruler->start));
    tl_json_object_set(object, "p2", point_to_json(ruler->end));
    return object;
}

static TlRuler *ruler_from_json(const TlJson *object)
{
    return tl_ruler_new(point_from_json(tl_json_get(object, "p1")), point_from_json(tl_json_get(object, "p2")));
}

static TlJson *expansion_joint_to_json(const TlExpansionJoint *joint)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "p1", point_to_json(joint->start));
    tl_json_object_set(object, "p2", point_to_json(joint->end));
    tl_json_object_set(object, "gap", tl_json_number(joint->gap));
    return object;
}

static TlExpansionJoint *expansion_joint_from_json(const TlJson *object)
{
    return tl_expansion_joint_new(point_from_json(tl_json_get(object, "p1")), point_from_json(tl_json_get(object, "p2")),
                                  tl_json_number_or(tl_json_get(object, "gap"), TL_EXPANSION_JOINT_DEFAULT_GAP));
}

static TlJson *opening_to_json(const TlOpening *opening)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "x", tl_json_number(opening->center.x));
    tl_json_object_set(object, "y", tl_json_number(opening->center.y));
    tl_json_object_set(object, "width", tl_json_number(opening->width));
    tl_json_object_set(object, "threshold", tl_json_number(opening->threshold));
    tl_json_object_set(object, "locked", tl_json_bool(opening->locked));
    return object;
}

static TlOpening *opening_from_json(const TlJson *object)
{
    TlOpening *opening = tl_opening_new(tl_vec2(tl_json_number_or(tl_json_get(object, "x"), 0),
                                                tl_json_number_or(tl_json_get(object, "y"), 0)),
                                        tl_json_number_or(tl_json_get(object, "width"), 800));
    opening->threshold = tl_json_number_or(tl_json_get(object, "threshold"), TL_OPENING_DEFAULT_THRESHOLD);
    opening->locked = tl_json_bool_or(tl_json_get(object, "locked"), false);
    return opening;
}

static TlJson *space_to_json(const TlSpace *space)
{
    TlJson *object = tl_json_object();
    tl_json_object_set(object, "id", tl_json_number(space->id));
    tl_json_object_set(object, "name", tl_json_string(space->name));
    tl_json_object_set(object, "tile", tile_to_json(&space->tile));
    tl_json_object_set(object, "source", space->source_id ? tl_json_number(space->source_id) : tl_json_null());
    tl_json_object_set(object, "pts", polygon_to_json(&space->outline));
    return object;
}

static TlSpace *space_from_json(const TlJson *object)
{
    TlSpace *space = tl_space_new((int)tl_json_number_or(tl_json_get(object, "id"), 0),
                                  tl_json_string_or(tl_json_get(object, "name"), "Комната"),
                                  tile_from_json(tl_json_get(object, "tile")));
    space->source_id = (int)tl_json_number_or(tl_json_get(object, "source"), 0);
    space->outline = polygon_from_json(tl_json_get(object, "pts"));
    return space;
}

/* ---- проект -------------------------------------------------------------- */

TlJson *tl_project_to_json(const TlProject *project)
{
    TlJson *root = tl_json_object();
    TlJson *outlines = tl_json_array(), *partitions = tl_json_array();
    TlJson *openings = tl_json_array(), *spaces = tl_json_array(), *rulers = tl_json_array();
    TlJson *expansion_joints = tl_json_array();
    for (size_t i = 0; i < project->outlines.count; i++)
        tl_json_array_push(outlines, outline_to_json(project->outlines.items[i]));
    for (size_t i = 0; i < project->partitions.count; i++)
        tl_json_array_push(partitions, partition_to_json(project->partitions.items[i]));
    for (size_t i = 0; i < project->openings.count; i++)
        tl_json_array_push(openings, opening_to_json(project->openings.items[i]));
    for (size_t i = 0; i < project->spaces.count; i++)
        tl_json_array_push(spaces, space_to_json(project->spaces.items[i]));
    for (size_t i = 0; i < project->rulers.count; i++)
        tl_json_array_push(rulers, ruler_to_json(project->rulers.items[i]));
    for (size_t i = 0; i < project->expansion_joints.count; i++)
        tl_json_array_push(expansion_joints, expansion_joint_to_json(project->expansion_joints.items[i]));
    tl_json_object_set(root, "version", tl_json_number(FORMAT_VERSION));
    tl_json_object_set(root, "outlines", outlines);
    tl_json_object_set(root, "partitions", partitions);
    tl_json_object_set(root, "openings", openings);
    tl_json_object_set(root, "spaces", spaces);
    tl_json_object_set(root, "rulers", rulers);
    tl_json_object_set(root, "expansion_joints", expansion_joints);
    tl_json_object_set(root, "next_id", tl_json_number(project->next_space_id));
    return root;
}

/* Файлы первых версий: каждая комната — отдельный контур с плиткой. */
static void load_legacy_rooms(TlProject *project, const TlJson *rooms)
{
    for (size_t i = 0; i < tl_json_length(rooms); i++) {
        const TlJson *room = tl_json_at(rooms, i);
        tl_project_add_outline(project, outline_from_json(room));
        TlSpace *space = tl_space_new(tl_project_take_space_id(project),
                                      tl_json_string_or(tl_json_get(room, "name"), "Комната"),
                                      tile_from_json(tl_json_get(room, "tile")));
        space->outline = polygon_from_json(tl_json_get(room, "pts"));
        tl_space_list_push(&project->spaces, space);
    }
}

bool tl_project_from_json(TlProject *project, const TlJson *root, char *error, size_t error_size)
{
    tl_project_clear(project);
    if (tl_json_type(root) != TL_JSON_OBJECT) {
        snprintf(error, error_size, "%s", "файл не похож на проект TileLayout");
        return false;
    }
    const TlJson *list;
    list = tl_json_get(root, "outlines");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_outline(project, outline_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "partitions");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_partition(project, partition_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "openings");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_opening(project, opening_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "expansion_joints");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_expansion_joint(project, expansion_joint_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "rulers");
    for (size_t i = 0; i < tl_json_length(list); i++)
        tl_project_add_ruler(project, ruler_from_json(tl_json_at(list, i)));
    list = tl_json_get(root, "spaces");
    int max_id = 0;
    for (size_t i = 0; i < tl_json_length(list); i++) {
        TlSpace *space = space_from_json(tl_json_at(list, i));
        max_id = space->id > max_id ? space->id : max_id;
        tl_space_list_push(&project->spaces, space);
    }
    project->next_space_id = (int)tl_json_number_or(tl_json_get(root, "next_id"), max_id + 1);
    if (project->next_space_id <= max_id)
        project->next_space_id = max_id + 1;
    load_legacy_rooms(project, tl_json_get(root, "rooms"));
    tl_project_update_spaces(project);
    return true;
}

char *tl_project_to_string(const TlProject *project)
{
    TlJson *root = tl_project_to_json(project);
    char *text = tl_json_stringify(root, true);
    tl_json_free(root);
    return text;
}

bool tl_project_from_string(TlProject *project, const char *text, char *error, size_t error_size)
{
    TlJson *root = tl_json_parse(text, error, error_size);
    if (!root) {
        tl_project_clear(project);
        return false;
    }
    bool ok = tl_project_from_json(project, root, error, error_size);
    tl_json_free(root);
    return ok;
}

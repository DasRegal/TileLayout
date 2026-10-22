/* Проект: состав, создание и удаление элементов, тела стен. */
#include "tl_project.h"

#include <math.h>
#include <stdio.h>

TlProject *tl_project_new(void)
{
    TlProject *project = TL_NEW(TlProject);
    project->next_space_id = 1;
    return project;
}

void tl_project_clear(TlProject *project)
{
    for (size_t i = 0; i < project->outlines.count; i++)
        tl_outline_free(project->outlines.items[i]);
    for (size_t i = 0; i < project->partitions.count; i++)
        tl_partition_free(project->partitions.items[i]);
    for (size_t i = 0; i < project->openings.count; i++)
        tl_opening_free(project->openings.items[i]);
    for (size_t i = 0; i < project->spaces.count; i++)
        tl_space_free(project->spaces.items[i]);
    tl_project_delete_all_rulers(project);
    for (size_t i = 0; i < project->expansion_joints.count; i++)
        tl_expansion_joint_free(project->expansion_joints.items[i]);
    tl_expansion_joint_list_clear(&project->expansion_joints);
    tl_outline_list_clear(&project->outlines);
    tl_partition_list_clear(&project->partitions);
    tl_opening_list_clear(&project->openings);
    tl_space_list_clear(&project->spaces);
    project->next_space_id = 1;
}

void tl_project_free(TlProject *project)
{
    if (!project)
        return;
    tl_project_clear(project);
    tl_outline_list_free(&project->outlines);
    tl_partition_list_free(&project->partitions);
    tl_opening_list_free(&project->openings);
    tl_space_list_free(&project->spaces);
    tl_ruler_list_free(&project->rulers);
    tl_expansion_joint_list_free(&project->expansion_joints);
    free(project);
}

int tl_project_take_space_id(TlProject *project) { return project->next_space_id++; }

void tl_project_add_outline(TlProject *project, TlOutline *outline)
{
    tl_outline_list_push(&project->outlines, outline);
}

void tl_project_add_partition(TlProject *project, TlPartition *partition)
{
    tl_partition_list_push(&project->partitions, partition);
}

void tl_project_add_opening(TlProject *project, TlOpening *opening)
{
    tl_opening_list_push(&project->openings, opening);
}

void tl_project_add_ruler(TlProject *project, TlRuler *ruler) { tl_ruler_list_push(&project->rulers, ruler); }

void tl_project_add_expansion_joint(TlProject *project, TlExpansionJoint *joint)
{
    tl_expansion_joint_list_push(&project->expansion_joints, joint);
}

void tl_project_delete_expansion_joint(TlProject *project, TlExpansionJoint *joint)
{
    long index;
    TL_ARRAY_INDEX_OF(&project->expansion_joints, joint, index);
    if (index < 0)
        return;
    tl_expansion_joint_list_remove_at(&project->expansion_joints, (size_t)index);
    tl_expansion_joint_free(joint);
}

void tl_project_delete_partition(TlProject *project, TlPartition *partition)
{
    long index;
    TL_ARRAY_INDEX_OF(&project->partitions, partition, index);
    if (index < 0)
        return;
    tl_partition_list_remove_at(&project->partitions, (size_t)index);
    tl_partition_free(partition);
}

void tl_project_delete_opening(TlProject *project, TlOpening *opening)
{
    long index;
    TL_ARRAY_INDEX_OF(&project->openings, opening, index);
    if (index < 0)
        return;
    tl_opening_list_remove_at(&project->openings, (size_t)index);
    tl_opening_free(opening);
}

void tl_project_delete_ruler(TlProject *project, TlRuler *ruler)
{
    long index;
    TL_ARRAY_INDEX_OF(&project->rulers, ruler, index);
    if (index < 0)
        return;
    tl_ruler_list_remove_at(&project->rulers, (size_t)index);
    tl_ruler_free(ruler);
}

void tl_project_delete_all_rulers(TlProject *project)
{
    for (size_t i = 0; i < project->rulers.count; i++)
        tl_ruler_free(project->rulers.items[i]);
    tl_ruler_list_clear(&project->rulers);
}

static bool partition_inside_outline(const TlPartition *partition, const TlOutline *outline)
{
    return tl_outline_contains_or_touches(outline, partition->start) &&
           tl_outline_contains_or_touches(outline, partition->end);
}

void tl_project_delete_outline(TlProject *project, TlOutline *outline)
{
    for (size_t i = project->partitions.count; i-- > 0;)
        if (partition_inside_outline(project->partitions.items[i], outline))
            tl_project_delete_partition(project, project->partitions.items[i]);
    long index;
    TL_ARRAY_INDEX_OF(&project->outlines, outline, index);
    if (index >= 0) {
        tl_outline_list_remove_at(&project->outlines, (size_t)index);
        tl_outline_free(outline);
    }
    tl_project_remove_orphan_openings(project);
    tl_project_update_spaces(project);
}

TlSpace *tl_project_space_by_id(TlProject *project, int id)
{
    for (size_t i = 0; i < project->spaces.count; i++)
        if (project->spaces.items[i]->id == id)
            return project->spaces.items[i];
    return NULL;
}

TlSpace *tl_project_space_at(TlProject *project, TlVec2 point)
{
    for (size_t i = 0; i < project->spaces.count; i++)
        if (tl_space_contains(project->spaces.items[i], point))
            return project->spaces.items[i];
    return NULL;
}

/* ---- тела стен ----------------------------------------------------------- */

TlBandList tl_project_bands(TlProject *project)
{
    TlBandList bands = {0};
    for (size_t o = 0; o < project->outlines.count; o++) {
        TlOutline *outline = project->outlines.items[o];
        for (size_t wall = 0; wall < tl_outline_wall_count(outline); wall++)
            tl_band_list_push(&bands, tl_band_of_wall(outline, wall));
    }
    for (size_t i = 0; i < project->partitions.count; i++)
        tl_band_list_push(&bands, tl_band_of_partition(project->partitions.items[i]));
    return bands;
}

bool tl_project_band_of(TlProject *project, TlHost host, TlBand *band)
{
    long index;
    if (host.kind == TL_HOST_WALL) {
        TL_ARRAY_INDEX_OF(&project->outlines, host.outline, index);
        if (index < 0 || host.wall >= tl_outline_wall_count(host.outline))
            return false;
        *band = tl_band_of_wall(host.outline, host.wall);
        return true;
    }
    if (host.kind == TL_HOST_PARTITION) {
        TL_ARRAY_INDEX_OF(&project->partitions, host.partition, index);
        if (index < 0)
            return false;
        *band = tl_band_of_partition(host.partition);
        return true;
    }
    return false;
}

bool tl_project_find_band(TlProject *project, TlVec2 point, double tolerance, TlBand *band)
{
    TlBandList bands = tl_project_bands(project);
    long best = -1;
    for (size_t i = 0; i < bands.count; i++) {
        const TlBand *candidate = &bands.items[i];
        double along, across;
        tl_band_local(candidate, point, &along, &across);
        bool inside = along >= -tolerance && along <= candidate->length + tolerance &&
                      across >= candidate->near - tolerance && across <= candidate->far + tolerance;
        if (inside && (best < 0 || tl_band_thickness(candidate) > tl_band_thickness(&bands.items[best])))
            best = (long)i;
    }
    if (best >= 0)
        *band = bands.items[best];
    tl_band_list_free(&bands);
    return best >= 0;
}

/* ---- демо ---------------------------------------------------------------- */

static TlSpace *demo_space(int id, const char *name, TlTileSettings tile, const TlVec2 *points,
                           size_t count)
{
    TlSpace *space = tl_space_new(id, name, tile);
    space->outline = tl_polygon_from_points(points, count);
    return space;
}

TlProject *tl_project_demo(void)
{
    TlProject *project = tl_project_new();
    const TlVec2 house[] = {{0, 0}, {6400, 0}, {6400, 3600}, {0, 3600}};
    tl_project_add_outline(project, tl_outline_new(house, 4, 250));
    tl_project_add_partition(project, tl_partition_new(tl_vec2(4260, 0), tl_vec2(4260, 2460), 120));
    tl_project_add_partition(project, tl_partition_new(tl_vec2(4200, 2400), tl_vec2(6400, 2400), 120));
    tl_project_add_opening(project, tl_opening_new(tl_vec2(4260, 1750), 700));

    TlTileSettings living_tile = tl_tile_settings_factory();
    living_tile.row_shift = 0.5;
    TlTileSettings bath_tile = tl_tile_settings_factory();
    bath_tile.tile_width = bath_tile.tile_height = 300;
    bath_tile.glue_layer_mm = 3;
    snprintf(bath_tile.color, sizeof bath_tile.color, "%s", "#b9d3dc");

    const TlVec2 living[] = {{0, 0}, {4200, 0}, {4200, 2460}, {6400, 2460}, {6400, 3600}, {0, 3600}};
    const TlVec2 bath[] = {{4320, 0}, {6400, 0}, {6400, 2340}, {4320, 2340}};
    tl_space_list_push(&project->spaces, demo_space(1, "Гостиная", living_tile, living, 6));
    tl_space_list_push(&project->spaces, demo_space(2, "Ванная", bath_tile, bath, 4));
    project->next_space_id = 3;
    tl_project_update_spaces(project);
    return project;
}

/*
 * Внутреннее для модуля расчёта: «группы раскладки» — комнаты, которые
 * раскладываются по одной сетке (сама комната и все, у кого пол «как в ней»),
 * и для каждой ячейки сетки — в каких комнатах лежат её части.
 */
#ifndef TL_REPORT_GROUPS_H
#define TL_REPORT_GROUPS_H

#include "tl_report.h"

typedef struct TlCellOwner {
    TlCell cell;
    TlSpace *space;
    TlTile *tile;
} TlCellOwner;

TL_ARRAY_DEFINE(TlCellOwnerList, TlCellOwner, tl_cell_owner_list)
TL_ARRAY_DEFINE(TlLayoutPtrList, TlLayout *, tl_layout_ptr_list)

typedef struct TlLayoutGroup {
    TlSpace *source;
    TlSpaceList members;
    TlLayoutPtrList layouts;     /* параллельно members */
    TlCellOwnerList owners;      /* упорядочены по ячейке (ряд, затем столбец) */
    bool owns_layouts;
} TlLayoutGroup;

TL_ARRAY_DEFINE(TlLayoutGroupList, TlLayoutGroup, tl_layout_group_list)

/* options == NULL — раскладки из кэша комнат; иначе считаются заново
 * (только для комнат группы source, если он задан). */
TlLayoutGroupList tl_report_build_groups(TlProject *project, const TlLayoutOptions *options,
                                         TlSpace *only_source);
void tl_report_free_groups(TlLayoutGroupList *groups);

int tl_compare_cells(TlCell a, TlCell b);
/* Владельцы ячейки: индекс первого в group->owners и их количество. */
size_t tl_group_cell_owners(const TlLayoutGroup *group, TlCell cell, size_t *first);
size_t tl_group_unique_cells(const TlLayoutGroup *group);
/* Куски, не разделённые стеной между комнатами (кандидаты в пары обрезков). */
TlPieceList tl_group_unshared_pieces(const TlLayoutGroup *group);

#endif

/*
 * Документ — открытый проект со всем, что к нему относится: история правок,
 * файл, признак изменений, выделение, настройки правки и вида.
 *
 * Паттерн «Наблюдатель»: канва, панель свойств и окно подписываются на
 * изменения документа и не знают друг о друге.
 *
 * Любая правка: app_document_checkpoint() перед изменением (точка отмены),
 * app_document_commit() после — документ помечается изменённым,
 * выделение проверяется, подписчики уведомляются.
 */
#ifndef APP_DOCUMENT_H
#define APP_DOCUMENT_H

#include <glib.h>
#include <stdbool.h>

#include "tl_history.h"
#include "tl_project.h"

typedef enum AppSelectionKind {
    APP_SELECT_NONE,
    APP_SELECT_VERTEX,     /* угол дома: outline + index */
    APP_SELECT_WALL,       /* наружная стена: outline + index */
    APP_SELECT_SPACE,      /* комната */
    APP_SELECT_PARTITION,  /* перегородка: index 0 — тело, 1 — начало, 2 — конец,
                              APP_PARTITION_FIRST_CORNER + TlPartitionCorner — угол тела */
    APP_SELECT_OPENING,    /* проём: index 0 — сам проём, APP_OPENING_THRESHOLD — стык комнат в нём,
                              APP_OPENING_THRESHOLD_FROM / _TO — конец стыка у косяка */
    APP_SELECT_RULER,      /* линейка: index 0 — вся, 1 — начало, 2 — конец */
    APP_SELECT_EXPANSION_JOINT, /* деформационный шов: index 0 — весь, 1 — начало, 2 — конец */
    APP_SELECT_NODE,       /* узел перегородок: partition + end — любой его конец;
                              index 0 — середина узла, k + 1 — угол k (см. TlNodeCorner) */
    APP_SELECT_TEE,        /* точка примыкания: partition + end — конец, упёршийся в бок другой */
    APP_SELECT_PIPE,       /* отверстие под трубу */
    /* на развёртке стен комнаты space (вкладка помещения): */
    APP_SELECT_ROOM_WALL,  /* стена: index — номер в развёртке */
    APP_SELECT_WALL_PIPE,  /* отверстие на стене: index — в space->wall_pipes */
    APP_SELECT_WALL_RULER, /* линейка на стене: index — в space->wall_rulers */
    APP_SELECT_PANNO,      /* панно на стене или на полу: index — в space->pannos */
    APP_SELECT_FIXTURE,    /* предмет: index — в project->fixtures */
    APP_SELECT_DRAIN       /* трап: index — в project->drains */
} AppSelectionKind;

#define APP_MAX_ROOMS 64 /* выделенных разом (Shift+щелчок) */

typedef struct AppSelection {
    AppSelectionKind kind;
    TlOutline *outline;
    TlSpace *space;
    TlPartition *partition;
    TlOpening *opening;
    TlRuler *ruler;
    TlExpansionJoint *expansion_joint;
    TlPipe *pipe;
    TlPartitionEnd end; /* у узла */
    int index;
    TlSpace *more_spaces[APP_MAX_ROOMS - 1]; /* у комнаты: ещё комнаты, добавленные Shift+щелчком */
    size_t more_count;
} AppSelection;

typedef enum AppChange {
    APP_CHANGED_MODEL = 1 << 0,
    APP_CHANGED_SELECTION = 1 << 1,
    APP_CHANGED_FILE = 1 << 2,
    APP_CHANGED_VIEW = 1 << 3,
    APP_CHANGED_STATUS = 1 << 4      /* новое сообщение для строки состояния */
} AppChange;

/* Настройки правки (сохраняются между запусками). */
typedef struct AppEditPreferences {
    double snap_step;
    double partition_thickness;
    double opening_width;
    bool link_walls;
    bool keep_angles;
    bool measure_magnet;          /* концы линеек и деформационных швов притягиваются к стенам */
    bool auto_align;              /* выравнивание по горизонтали/вертикали и по углам стен */
    double expansion_joint_gap;   /* зазор новых деформационных швов */
    double divider_gap;           /* шов новых линий раздела */
    int fixture_kind;             /* вид новых предметов (TlFixtureKind) */
    int drain_kind;               /* вид новых трапов (TlDrainKind) */
    double pipe_diameter;         /* диаметр новых отверстий под трубы */
    double house_width, house_height, house_thickness;
    double outline_thickness;     /* поле «Толщина» в свойствах контура */
} AppEditPreferences;

/* Что показывать на плане. */
typedef struct AppViewFlags {
    bool tiles, clips, numbers, dims, grid, expansion_joints;
} AppViewFlags;

typedef struct AppDocument AppDocument;
typedef void (*AppDocumentListener)(AppDocument *document, unsigned changes, void *user_data);

struct AppDocument {
    TlProject *project;
    TlHistory *history;
    char *path;
    bool dirty;
    AppSelection selection;
    int walls_space;              /* открыта вкладка стен этой комнаты (id); 0 — план */
    AppEditPreferences preferences;
    AppViewFlags view;
    char status[256];
};

AppDocument *app_document_new(void);
void app_document_free(AppDocument *document);
void app_document_add_listener(AppDocument *document, AppDocumentListener listener, void *user_data);
void app_document_notify(AppDocument *document, unsigned changes);
/* Сообщение пользователю в строке состояния. */
void app_document_status(AppDocument *document, const char *format, ...) G_GNUC_PRINTF(2, 3);

/* ---- правка ------------------------------------------------------------------ */

void app_document_checkpoint(AppDocument *document, const char *merge_key);
void app_document_commit(AppDocument *document);
/* Правка, меняющая пол комнат (проёмы, швы): комнаты пересчитываются, затем commit. */
void app_document_commit_rooms(AppDocument *document);
/* Правка геометрии с привязками перегородок и проёмов. */
TlEditToken *app_document_begin_geometry(AppDocument *document, const char *merge_key,
                                         TlOutline *moving_outline);
void app_document_end_geometry(AppDocument *document, TlEditToken *token, const TlEditOptions *options);
bool app_document_undo(AppDocument *document);
bool app_document_redo(AppDocument *document);

/* ---- выделение ---------------------------------------------------------------- */

void app_document_select(AppDocument *document, AppSelection selection);
void app_document_clear_selection(AppDocument *document);
/* Shift+щелчок по комнате: добавить к выделенным комнатам или убрать из них. */
void app_document_toggle_space(AppDocument *document, TlSpace *space);
/* Выделенные комнаты (space и добавленные), не больше APP_MAX_ROOMS; 0 — комната не выделена.
   Выделенное на развёртке стен — комната этих стен. */
size_t app_selection_spaces(const AppSelection *selection, TlSpace **spaces);
/* Выделено на развёртке стен (стена, отверстие или линейка на стене). */
bool app_selection_on_walls(const AppSelection *selection);
/* Дом выделенной стены, угла или комнаты. */
TlOutline *app_selection_outline(const AppSelection *selection);
bool app_selection_equal(const AppSelection *a, const AppSelection *b);

#define APP_PARTITION_FIRST_CORNER 3
#define APP_OPENING_THRESHOLD 1
#define APP_OPENING_THRESHOLD_FROM 2 /* у косяка в начале проёма */
#define APP_OPENING_THRESHOLD_TO 3   /* у косяка в конце */

/* Выделен угол тела перегородки (тогда *corner — какой). */
bool app_selection_partition_corner(const AppSelection *selection, TlPartitionCorner *corner);
/* Выделенный узел (заново по концу перегородки: узлы вычисляются). */
bool app_selection_node(const AppSelection *selection, TlProject *project, TlNode *node);
/* Выделенное примыкание. */
bool app_selection_tee(const AppSelection *selection, TlProject *project, TlTee *tee);
/* Точка узла по индексу выделения: 0 — середина, k + 1 — угол k. */
TlVec2 app_node_point(const TlNode *node, int index);

/* ---- файлы -------------------------------------------------------------------- */

void app_document_reset(AppDocument *document, TlProject *project, const char *path);
bool app_document_open(AppDocument *document, const char *path, char **error);
bool app_document_save(AppDocument *document, const char *path, char **error);

#endif

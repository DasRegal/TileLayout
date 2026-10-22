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
    APP_SELECT_OPENING,
    APP_SELECT_RULER,      /* линейка: index 0 — вся, 1 — начало, 2 — конец */
    APP_SELECT_EXPANSION_JOINT /* деформационный шов: index 0 — весь, 1 — начало, 2 — конец */
} AppSelectionKind;

typedef struct AppSelection {
    AppSelectionKind kind;
    TlOutline *outline;
    TlSpace *space;
    TlPartition *partition;
    TlOpening *opening;
    TlRuler *ruler;
    TlExpansionJoint *expansion_joint;
    int index;
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
/* Правка геометрии с привязками перегородок и проёмов. */
TlEditToken *app_document_begin_geometry(AppDocument *document, const char *merge_key,
                                         TlOutline *moving_outline);
void app_document_end_geometry(AppDocument *document, TlEditToken *token, const TlEditOptions *options);
bool app_document_undo(AppDocument *document);
bool app_document_redo(AppDocument *document);

/* ---- выделение ---------------------------------------------------------------- */

void app_document_select(AppDocument *document, AppSelection selection);
void app_document_clear_selection(AppDocument *document);
/* Дом выделенной стены, угла или комнаты. */
TlOutline *app_selection_outline(const AppSelection *selection);
bool app_selection_equal(const AppSelection *a, const AppSelection *b);

#define APP_PARTITION_FIRST_CORNER 3

/* Выделен угол тела перегородки (тогда *corner — какой). */
bool app_selection_partition_corner(const AppSelection *selection, TlPartitionCorner *corner);

/* ---- файлы -------------------------------------------------------------------- */

void app_document_reset(AppDocument *document, TlProject *project, const char *path);
bool app_document_open(AppDocument *document, const char *path, char **error);
bool app_document_save(AppDocument *document, const char *path, char **error);

#endif

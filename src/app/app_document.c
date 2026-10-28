#include "app_document.h"

#include "tl_drag.h"

#include <glib.h>
#include <glib/gprintf.h>
#include <stdarg.h>
#include <string.h>

#include "tl_layout.h"
#include "tl_project_io.h"

typedef struct Subscriber {
    AppDocumentListener listener;
    void *user_data;
} Subscriber;

/* Подписчики хранятся рядом с документом, а не в публичной структуре. */
typedef struct DocumentPrivate {
    AppDocument public;
    GArray *subscribers;
} DocumentPrivate;

static DocumentPrivate *private_of(AppDocument *document) { return (DocumentPrivate *)document; }

AppDocument *app_document_new(void)
{
    DocumentPrivate *self = g_new0(DocumentPrivate, 1);
    self->subscribers = g_array_new(FALSE, FALSE, sizeof(Subscriber));
    AppDocument *document = &self->public;
    document->project = tl_project_demo();
    document->history = tl_history_new();
    document->preferences = (AppEditPreferences){
        .snap_step = 10, .partition_thickness = 100, .opening_width = 800, .link_walls = true,
        .keep_angles = true, .measure_magnet = true, .auto_align = true,
        .expansion_joint_gap = TL_EXPANSION_JOINT_DEFAULT_GAP, .divider_gap = TL_DIVIDER_DEFAULT_GAP, .pipe_diameter = TL_PIPE_DEFAULT_DIAMETER,
        .house_width = 6000, .house_height = 4000, .house_thickness = 200, .outline_thickness = 200};
    document->view = (AppViewFlags){.tiles = true, .clips = true, .dims = true, .grid = true,
                                    .expansion_joints = true};
    return document;
}

void app_document_free(AppDocument *document)
{
    if (!document)
        return;
    tl_project_free(document->project);
    tl_history_free(document->history);
    g_free(document->path);
    g_array_free(private_of(document)->subscribers, TRUE);
    g_free(private_of(document));
}

void app_document_add_listener(AppDocument *document, AppDocumentListener listener, void *user_data)
{
    Subscriber subscriber = {listener, user_data};
    g_array_append_val(private_of(document)->subscribers, subscriber);
}

void app_document_notify(AppDocument *document, unsigned changes)
{
    GArray *subscribers = private_of(document)->subscribers;
    for (guint i = 0; i < subscribers->len; i++) {
        Subscriber *subscriber = &g_array_index(subscribers, Subscriber, i);
        subscriber->listener(document, changes, subscriber->user_data);
    }
}

void app_document_status(AppDocument *document, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    g_vsnprintf(document->status, sizeof document->status, format, arguments);
    va_end(arguments);
    app_document_notify(document, APP_CHANGED_STATUS);
}

/* ---- выделение ------------------------------------------------------------- */

TlOutline *app_selection_outline(const AppSelection *selection)
{
    switch (selection->kind) {
    case APP_SELECT_VERTEX:
    case APP_SELECT_WALL: return selection->outline;
    case APP_SELECT_SPACE: return selection->space ? selection->space->house : NULL;
    default: return NULL;
    }
}

bool app_selection_partition_corner(const AppSelection *selection, TlPartitionCorner *corner)
{
    if (selection->kind != APP_SELECT_PARTITION || selection->index < APP_PARTITION_FIRST_CORNER)
        return false;
    *corner = (TlPartitionCorner)(selection->index - APP_PARTITION_FIRST_CORNER);
    return true;
}

bool app_selection_node(const AppSelection *selection, TlProject *project, TlNode *node)
{
    return selection->kind == APP_SELECT_NODE && selection->partition &&
           tl_project_node_of(project, selection->partition, selection->end, node) &&
           selection->index >= 0 && (size_t)selection->index <= node->count;
}

bool app_selection_tee(const AppSelection *selection, TlProject *project, TlTee *tee)
{
    return selection->kind == APP_SELECT_TEE && selection->partition &&
           tl_project_tee_of(project, selection->partition, selection->end, tee);
}

TlVec2 app_node_point(const TlNode *node, int index) { return tl_node_point(node, index); }

bool app_selection_equal(const AppSelection *a, const AppSelection *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

void app_document_select(AppDocument *document, AppSelection selection)
{
    if (app_selection_equal(&document->selection, &selection))
        return;
    document->selection = selection;
    app_document_notify(document, APP_CHANGED_SELECTION);
}

void app_document_clear_selection(AppDocument *document)
{
    app_document_select(document, (AppSelection){0});
}

bool app_selection_on_walls(const AppSelection *selection)
{
    return selection->kind == APP_SELECT_ROOM_WALL || selection->kind == APP_SELECT_WALL_PIPE ||
           selection->kind == APP_SELECT_WALL_RULER;
}

size_t app_selection_spaces(const AppSelection *selection, TlSpace **spaces)
{
    if (!app_selection_on_walls(selection) && selection->kind != APP_SELECT_SPACE && selection->kind != APP_SELECT_PANNO)
        return 0;
    if (!selection->space)
        return 0;
    spaces[0] = selection->space;
    for (size_t i = 0; i < selection->more_count; i++)
        spaces[i + 1] = selection->more_spaces[i];
    return selection->more_count + 1;
}

void app_document_toggle_space(AppDocument *document, TlSpace *space)
{
    AppSelection s = document->selection;
    if (s.kind != APP_SELECT_SPACE) {
        app_document_select(document, (AppSelection){.kind = APP_SELECT_SPACE, .space = space});
        return;
    }
    TlSpace *rooms[APP_MAX_ROOMS];
    size_t count = app_selection_spaces(&s, rooms), kept = 0;
    bool removed = false;
    for (size_t i = 0; i < count; i++) {
        if (rooms[i] == space)
            removed = true;
        else
            rooms[kept++] = rooms[i];
    }
    if (!removed && kept < APP_MAX_ROOMS)
        rooms[kept++] = space;
    AppSelection next = {0};
    if (kept > 0) {
        next = (AppSelection){.kind = APP_SELECT_SPACE, .space = rooms[0], .more_count = kept - 1};
        for (size_t i = 1; i < kept; i++)
            next.more_spaces[i - 1] = rooms[i];
    }
    app_document_select(document, next);
}

static bool contains_pointer(void *const *items, size_t count, const void *item)
{
    for (size_t i = 0; i < count; i++)
        if (items[i] == item)
            return true;
    return false;
}

/* Выделенные объекты могли исчезнуть после правки — тогда сбрасываем. */
static bool selection_is_valid(AppDocument *document)
{
    const AppSelection *s = &document->selection;
    TlProject *p = document->project;
    switch (s->kind) {
    case APP_SELECT_NONE: return true;
    case APP_SELECT_VERTEX:
    case APP_SELECT_WALL:
        return contains_pointer((void *const *)p->outlines.items, p->outlines.count, s->outline) &&
               (size_t)s->index < tl_outline_wall_count(s->outline);
    case APP_SELECT_SPACE:
        if (!contains_pointer((void *const *)p->spaces.items, p->spaces.count, s->space))
            return false;
        for (size_t i = 0; i < s->more_count; i++)
            if (!contains_pointer((void *const *)p->spaces.items, p->spaces.count, s->more_spaces[i]))
                return false;
        return true;
    case APP_SELECT_PARTITION:
        return contains_pointer((void *const *)p->partitions.items, p->partitions.count, s->partition);
    case APP_SELECT_OPENING:
        return contains_pointer((void *const *)p->openings.items, p->openings.count, s->opening);
    case APP_SELECT_RULER: return contains_pointer((void *const *)p->rulers.items, p->rulers.count, s->ruler);
    case APP_SELECT_EXPANSION_JOINT:
        return contains_pointer((void *const *)p->expansion_joints.items, p->expansion_joints.count, s->expansion_joint);
    case APP_SELECT_PIPE: return contains_pointer((void *const *)p->pipes.items, p->pipes.count, s->pipe);
    case APP_SELECT_FIXTURE: return s->index >= 0 && (size_t)s->index < p->fixtures.count;
    case APP_SELECT_DRAIN: return s->index >= 0 && (size_t)s->index < p->drains.count;
    case APP_SELECT_ROOM_WALL:
    case APP_SELECT_WALL_PIPE:
    case APP_SELECT_WALL_RULER:
    case APP_SELECT_PANNO: {
        if (!contains_pointer((void *const *)p->spaces.items, p->spaces.count, s->space) || s->index < 0)
            return false;
        size_t count = s->kind == APP_SELECT_ROOM_WALL  ? tl_space_wall_count(s->space)
                       : s->kind == APP_SELECT_WALL_PIPE ? s->space->wall_pipes.count
                       : s->kind == APP_SELECT_PANNO     ? s->space->pannos.count
                                                          : s->space->wall_rulers.count;
        return (size_t)s->index < count;
    }
    case APP_SELECT_NODE: {
        TlNode node;
        return contains_pointer((void *const *)p->partitions.items, p->partitions.count, s->partition) &&
               app_selection_node(s, p, &node);
    }
    case APP_SELECT_TEE: {
        TlTee tee;
        return contains_pointer((void *const *)p->partitions.items, p->partitions.count, s->partition) &&
               app_selection_tee(s, p, &tee);
    }
    }
    return false;
}

/* ---- правка ----------------------------------------------------------------- */

void app_document_checkpoint(AppDocument *document, const char *merge_key)
{
    tl_history_checkpoint(document->history, document->project, merge_key,
                          g_get_monotonic_time() / 1e6);
}

void app_document_commit(AppDocument *document)
{
    document->dirty = true;
    unsigned changes = APP_CHANGED_MODEL | APP_CHANGED_FILE;
    if (!selection_is_valid(document)) {
        document->selection = (AppSelection){0};
        changes |= APP_CHANGED_SELECTION;
    }
    app_document_notify(document, changes);
}

void app_document_commit_rooms(AppDocument *document)
{
    tl_project_merge_straight_nodes(document->project);
    tl_project_update_spaces(document->project);
    app_document_commit(document);
}

TlEditToken *app_document_begin_geometry(AppDocument *document, const char *merge_key,
                                         TlOutline *moving_outline)
{
    app_document_checkpoint(document, merge_key);
    return tl_project_begin_edit(document->project, moving_outline);
}

void app_document_end_geometry(AppDocument *document, TlEditToken *token, const TlEditOptions *options)
{
    tl_project_end_edit(document->project, token, options);
    app_document_commit(document);
}

/* Выделение как (вид, номер в списке, индекс) — переживает пересоздание
 * объектов при отмене. */
typedef struct SelectionKey {
    AppSelectionKind kind;
    long position;
    long more[APP_MAX_ROOMS - 1]; /* добавленные комнаты */
    size_t more_count;
    int index;
    TlPartitionEnd end;
} SelectionKey;

static long position_in(void *const *items, size_t count, const void *item)
{
    for (size_t i = 0; i < count; i++)
        if (items[i] == item)
            return (long)i;
    return -1;
}

static SelectionKey selection_key(AppDocument *document)
{
    const AppSelection *s = &document->selection;
    TlProject *p = document->project;
    SelectionKey key = {.kind = s->kind, .position = -1, .index = s->index, .end = s->end};
    switch (s->kind) {
    case APP_SELECT_VERTEX:
    case APP_SELECT_WALL:
        key.position = position_in((void *const *)p->outlines.items, p->outlines.count, s->outline);
        break;
    case APP_SELECT_ROOM_WALL:
    case APP_SELECT_WALL_PIPE:
    case APP_SELECT_WALL_RULER:
    case APP_SELECT_PANNO:
    case APP_SELECT_SPACE:
        key.position = position_in((void *const *)p->spaces.items, p->spaces.count, s->space);
        for (size_t i = 0; i < s->more_count; i++)
            key.more[key.more_count++] = position_in((void *const *)p->spaces.items, p->spaces.count,
                                                     s->more_spaces[i]);
        break;
    case APP_SELECT_PARTITION:
    case APP_SELECT_NODE:
    case APP_SELECT_TEE:
        key.position = position_in((void *const *)p->partitions.items, p->partitions.count, s->partition);
        break;
    case APP_SELECT_OPENING:
        key.position = position_in((void *const *)p->openings.items, p->openings.count, s->opening);
        break;
    case APP_SELECT_RULER:
        key.position = position_in((void *const *)p->rulers.items, p->rulers.count, s->ruler);
        break;
    case APP_SELECT_EXPANSION_JOINT:
        key.position = position_in((void *const *)p->expansion_joints.items, p->expansion_joints.count,
                                   s->expansion_joint);
        break;
    case APP_SELECT_PIPE: key.position = position_in((void *const *)p->pipes.items, p->pipes.count, s->pipe); break;
    default: break;
    }
    return key;
}

static void restore_selection(AppDocument *document, SelectionKey key)
{
    TlProject *p = document->project;
    AppSelection s = {.kind = key.kind, .index = key.index, .end = key.end};
    size_t position = (size_t)key.position;
    bool found = key.position >= 0;
    switch (key.kind) {
    case APP_SELECT_VERTEX:
    case APP_SELECT_WALL:
        found = found && position < p->outlines.count;
        if (found)
            s.outline = p->outlines.items[position];
        break;
    case APP_SELECT_ROOM_WALL:
    case APP_SELECT_WALL_PIPE:
    case APP_SELECT_WALL_RULER:
    case APP_SELECT_PANNO:
    case APP_SELECT_SPACE:
        found = found && position < p->spaces.count;
        if (found)
            s.space = p->spaces.items[position];
        for (size_t i = 0; found && i < key.more_count; i++)
            if (key.more[i] >= 0 && (size_t)key.more[i] < p->spaces.count)
                s.more_spaces[s.more_count++] = p->spaces.items[key.more[i]];
        break;
    case APP_SELECT_PARTITION:
    case APP_SELECT_NODE:
    case APP_SELECT_TEE:
        found = found && position < p->partitions.count;
        if (found)
            s.partition = p->partitions.items[position];
        break;
    case APP_SELECT_OPENING:
        found = found && position < p->openings.count;
        if (found)
            s.opening = p->openings.items[position];
        break;
    case APP_SELECT_RULER:
        found = found && position < p->rulers.count;
        if (found)
            s.ruler = p->rulers.items[position];
        break;
    case APP_SELECT_EXPANSION_JOINT:
        found = found && position < p->expansion_joints.count;
        if (found)
            s.expansion_joint = p->expansion_joints.items[position];
        break;
    case APP_SELECT_FIXTURE:
    case APP_SELECT_DRAIN: found = true; break; /* номер — в index */
    case APP_SELECT_PIPE:
        found = found && position < p->pipes.count;
        if (found)
            s.pipe = p->pipes.items[position];
        break;
    default: found = false; break;
    }
    document->selection = found ? s : (AppSelection){0};
    if (!selection_is_valid(document))
        document->selection = (AppSelection){0};
}

static bool step_history(AppDocument *document, bool (*step)(TlHistory *, TlProject *))
{
    SelectionKey key = selection_key(document);
    if (!step(document->history, document->project))
        return false;
    restore_selection(document, key);
    document->dirty = true;
    app_document_notify(document, APP_CHANGED_MODEL | APP_CHANGED_SELECTION | APP_CHANGED_FILE);
    return true;
}

bool app_document_undo(AppDocument *document) { return step_history(document, tl_history_undo); }

bool app_document_redo(AppDocument *document) { return step_history(document, tl_history_redo); }

/* ---- файлы ------------------------------------------------------------------ */

void app_document_reset(AppDocument *document, TlProject *project, const char *path)
{
    tl_project_free(document->project);
    document->project = project;
    g_free(document->path);
    document->path = g_strdup(path);
    document->dirty = false;
    document->selection = (AppSelection){0};
    tl_history_clear(document->history);
    app_document_notify(document, APP_CHANGED_MODEL | APP_CHANGED_SELECTION | APP_CHANGED_FILE);
}

bool app_document_open(AppDocument *document, const char *path, char **error)
{
    char *text = NULL;
    GError *io_error = NULL;
    if (!g_file_get_contents(path, &text, NULL, &io_error)) {
        *error = g_strdup(io_error->message);
        g_error_free(io_error);
        return false;
    }
    TlProject *project = tl_project_new();
    char message[256] = "";
    bool ok = tl_project_from_string(project, text, message, sizeof message);
    g_free(text);
    if (!ok) {
        tl_project_free(project);
        *error = g_strdup(message[0] ? message : "неверный файл проекта");
        return false;
    }
    app_document_reset(document, project, path);
    return true;
}

bool app_document_save(AppDocument *document, const char *path, char **error)
{
    char *text = tl_project_to_string(document->project);
    GError *io_error = NULL;
    bool ok = g_file_set_contents(path, text, -1, &io_error);
    free(text);
    if (!ok) {
        *error = g_strdup(io_error->message);
        g_error_free(io_error);
        return false;
    }
    if (document->path != path) {
        g_free(document->path);
        document->path = g_strdup(path);
    }
    document->dirty = false;
    app_document_notify(document, APP_CHANGED_FILE);
    return true;
}

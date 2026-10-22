#include "tl_history.h"

#include <stdio.h>
#include <string.h>

#include "tl_project_io.h"

TL_ARRAY_DEFINE(SnapshotStack, char *, snapshot_stack)

struct TlHistory {
    SnapshotStack undo, redo;
    char last_key[64];
    double last_time;
};

static void free_snapshots(SnapshotStack *stack)
{
    for (size_t i = 0; i < stack->count; i++)
        free(stack->items[i]);
    snapshot_stack_clear(stack);
}

TlHistory *tl_history_new(void) { return TL_NEW(TlHistory); }

void tl_history_clear(TlHistory *history)
{
    free_snapshots(&history->undo);
    free_snapshots(&history->redo);
    history->last_key[0] = '\0';
}

void tl_history_free(TlHistory *history)
{
    if (!history)
        return;
    tl_history_clear(history);
    snapshot_stack_free(&history->undo);
    snapshot_stack_free(&history->redo);
    free(history);
}

void tl_history_checkpoint(TlHistory *history, const TlProject *project, const char *merge_key,
                           double now_seconds)
{
    const char *key = merge_key ? merge_key : "";
    bool continues_series = key[0] && strcmp(key, history->last_key) == 0 &&
                            now_seconds - history->last_time < TL_HISTORY_MERGE_SECONDS;
    snprintf(history->last_key, sizeof history->last_key, "%s", key);
    history->last_time = now_seconds;
    if (continues_series)
        return;
    if (history->undo.count >= TL_HISTORY_LIMIT) {
        free(history->undo.items[0]);
        snapshot_stack_remove_at(&history->undo, 0);
    }
    snapshot_stack_push(&history->undo, tl_project_to_string(project));
    free_snapshots(&history->redo);
}

/* Текущее состояние — в стек to, состояние из from — в проект. */
static bool move_between(SnapshotStack *from, SnapshotStack *to, TlProject *project, char *last_key)
{
    if (!from->count)
        return false;
    snapshot_stack_push(to, tl_project_to_string(project));
    char *snapshot = from->items[--from->count];
    char error[128];
    tl_project_from_string(project, snapshot, error, sizeof error);
    free(snapshot);
    last_key[0] = '\0';
    return true;
}

bool tl_history_undo(TlHistory *history, TlProject *project)
{
    return move_between(&history->undo, &history->redo, project, history->last_key);
}

bool tl_history_redo(TlHistory *history, TlProject *project)
{
    return move_between(&history->redo, &history->undo, project, history->last_key);
}

size_t tl_history_undo_count(const TlHistory *history) { return history->undo.count; }

size_t tl_history_redo_count(const TlHistory *history) { return history->redo.count; }

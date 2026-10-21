#include "spec.h"
#include "specs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_history.h"
#include "tl_json.h"
#include "tl_project_io.h"

#ifndef TL_FIXTURES_DIR
#define TL_FIXTURES_DIR "tests/fixtures"
#endif

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    size_t read = fread(text, 1, (size_t)size, file);
    text[read] = '\0';
    fclose(file);
    return text;
}

void spec_storage(void)
{
    DESCRIBE("JSON")
    {
        IT("разбирает вложенные объекты, массивы, числа, строки с экранированием и юникодом")
        {
            char error[128] = "";
            TlJson *root = tl_json_parse("{\"a\": [1, -2.5e2, true, null], \"name\": \"Ванная \\\"№1\\\" \\u00e9\"}",
                                         error, sizeof error);
            EXPECT(root != NULL);
            if (root) {
                const TlJson *array = tl_json_get(root, "a");
                EXPECT_INT(tl_json_length(array), 4);
                EXPECT_NEAR(tl_json_number_or(tl_json_at(array, 1), 0), -250, 1e-12);
                EXPECT(tl_json_bool_or(tl_json_at(array, 2), false));
                EXPECT_STR(tl_json_string_or(tl_json_get(root, "name"), ""), "Ванная \"№1\" é");
                tl_json_free(root);
            }
        }
        IT("сообщает об ошибке в неверном тексте")
        {
            char error[128] = "";
            EXPECT(tl_json_parse("{\"a\": [1, 2", error, sizeof error) == NULL);
            EXPECT(error[0] != '\0');
        }
        IT("пишет числа с точкой и коротко, строки — с экранированием")
        {
            TlJson *root = tl_json_object();
            tl_json_object_set(root, "x", tl_json_number(0.1));
            tl_json_object_set(root, "y", tl_json_number(6400));
            tl_json_object_set(root, "s", tl_json_string("a\"b\n"));
            char *text = tl_json_stringify(root, false);
            EXPECT_STR(text, "{\"x\":0.1,\"y\":6400,\"s\":\"a\\\"b\\n\"}");
            free(text);
            tl_json_free(root);
        }
    }

    DESCRIBE("Сохранение проекта")
    {
        IT("сохраняет и загружает проект без потерь")
        {
            TlProject *original = tl_project_demo();
            original->partitions.items[0]->locked = true;
            original->spaces.items[1]->tile.wall_gap = 5;
            char *text = tl_project_to_string(original);
            TlProject *loaded = tl_project_new();
            char error[128] = "";
            EXPECT(tl_project_from_string(loaded, text, error, sizeof error));
            EXPECT_INT(loaded->spaces.count, 2);
            EXPECT_INT(loaded->partitions.count, 2);
            EXPECT_INT(loaded->openings.count, 1);
            EXPECT(loaded->partitions.items[0]->locked);
            EXPECT_STR(loaded->spaces.items[0]->name, "Гостиная");
            EXPECT_NEAR(loaded->spaces.items[1]->tile.wall_gap, 5, 1e-12);
            EXPECT_NEAR(tl_space_area(loaded->spaces.items[0]), tl_space_area(original->spaces.items[0]), 1e-6);
            free(text);
            tl_project_free(loaded);
            tl_project_free(original);
        }
        IT("открывает файл, сохранённый Python-версией TileLayout")
        {
            char *text = read_file(TL_FIXTURES_DIR "/python_demo.tilelayout.json");
            EXPECT(text != NULL);
            if (text) {
                TlProject *project = tl_project_new();
                char error[128] = "";
                EXPECT(tl_project_from_string(project, text, error, sizeof error));
                EXPECT_INT(project->spaces.count, 3);
                TlSpace *bedroom = project->spaces.items[2];
                EXPECT_STR(bedroom->name, "Спальня");
                EXPECT_NEAR(tl_space_area(bedroom) / 1e6, 7.02, 0.001);
                EXPECT(bedroom->layout_source == project->spaces.items[0]);
                EXPECT(project->outlines.items[0]->locked.items[1]);
                EXPECT(project->partitions.items[0]->locked);
                tl_project_free(project);
                free(text);
            }
        }
    }

    DESCRIBE("История правок")
    {
        IT("отменяет и повторяет изменения проекта")
        {
            TlProject *project = tl_project_demo();
            TlHistory *history = tl_history_new();
            tl_history_checkpoint(history, project, "", 0.0);
            tl_space_set_name(project->spaces.items[0], "Зал");
            EXPECT(tl_history_undo(history, project));
            EXPECT_STR(project->spaces.items[0]->name, "Гостиная");
            EXPECT(tl_history_redo(history, project));
            EXPECT_STR(project->spaces.items[0]->name, "Зал");
            EXPECT(!tl_history_redo(history, project));
            tl_history_free(history);
            tl_project_free(project);
        }
        IT("серию одинаковых правок подряд склеивает в один шаг отмены")
        {
            TlProject *project = tl_project_demo();
            TlHistory *history = tl_history_new();
            tl_history_checkpoint(history, project, "width", 10.0);
            tl_history_checkpoint(history, project, "width", 10.5);
            tl_history_checkpoint(history, project, "width", 11.0);
            EXPECT_INT(tl_history_undo_count(history), 1);
            tl_history_checkpoint(history, project, "width", 20.0); /* пауза — новый шаг */
            EXPECT_INT(tl_history_undo_count(history), 2);
            tl_history_free(history);
            tl_project_free(project);
        }
    }
}

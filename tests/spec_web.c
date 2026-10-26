#include "spec.h"
#include "specs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tl_json.h"
#include "tl_web.h"

#ifndef TL_FIXTURES_DIR
#define TL_FIXTURES_DIR "tests/fixtures"
#endif

static char *read_fixture(const char *path)
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

/* Поле плитки комнаты id из tl_web_space_tile. */
static double tile_number(int id, const char *key)
{
    TlJson *json = tl_json_parse(tl_web_space_tile(id), NULL, 0);
    double value = tl_json_number_or(tl_json_get(tl_json_get(json, "tile"), key), -1);
    tl_json_free(json);
    return value;
}

/* Число плиток в комнате id по сцене. */
static int scene_count(int id)
{
    TlJson *scene = tl_json_parse(tl_web_scene(), NULL, 0);
    const TlJson *spaces = tl_json_get(scene, "spaces");
    int count = -1;
    for (size_t i = 0; i < tl_json_length(spaces); i++) {
        const TlJson *space = tl_json_at(spaces, i);
        if ((int)tl_json_number_or(tl_json_get(space, "id"), 0) == id)
            count = (int)tl_json_number_or(tl_json_get(tl_json_get(space, "layout"), "count"), -1);
    }
    tl_json_free(scene);
    return count;
}

void spec_web(void)
{
    DESCRIBE("Ядро веб-версии")
    {
        IT("открывает файл проекта и отдаёт сцену: стены, перегородки, проёмы, комнаты с раскладкой")
        {
            char *text = read_fixture(TL_FIXTURES_DIR "/python_demo.tilelayout.json");
            EXPECT(text && tl_web_open(text));
            free(text);
            TlJson *scene = tl_json_parse(tl_web_scene(), NULL, 0);
            EXPECT(scene != NULL);
            EXPECT_INT((int)tl_json_length(tl_json_get(scene, "walls")), 1);
            EXPECT(tl_json_length(tl_json_get(scene, "partitions")) >= 3);
            EXPECT_INT((int)tl_json_length(tl_json_get(scene, "openings")), 1);
            EXPECT_INT((int)tl_json_length(tl_json_get(scene, "dimensions")), 4);
            const TlJson *spaces = tl_json_get(scene, "spaces");
            EXPECT_INT((int)tl_json_length(spaces), 3);
            const TlJson *layout = tl_json_get(tl_json_at(spaces, 0), "layout");
            size_t numbers = tl_json_length(tl_json_get(layout, "tiles"));
            EXPECT(numbers > 0 && numbers % 5 == 0); /* left, top, right, bottom, вид */
            EXPECT_INT((int)tl_json_length(tl_json_get(layout, "matrix")), 6);
            tl_json_free(scene);
        }
        IT("правка поля плитки меняет раскладку; отмена возвращает; серия правок поля — один шаг")
        {
            char *text = read_fixture(TL_FIXTURES_DIR "/python_demo.tilelayout.json");
            tl_web_open(text);
            free(text);
            int before = scene_count(1);
            EXPECT_NEAR(tile_number(1, "w"), 600, 1e-9);
            EXPECT(tl_web_set_tile_field(1, "w", "300", 100.0));
            EXPECT(tl_web_set_tile_field(1, "w", "200", 100.5)); /* та же серия */
            EXPECT_NEAR(tile_number(1, "w"), 200, 1e-9);
            EXPECT(scene_count(1) > before);
            EXPECT_INT(tl_web_undo_count(), 1);
            EXPECT(tl_web_undo());
            EXPECT_NEAR(tile_number(1, "w"), 600, 1e-9);
            EXPECT_INT(scene_count(1), before);
            EXPECT(tl_web_redo());
            EXPECT_NEAR(tile_number(1, "w"), 200, 1e-9);

            EXPECT(tl_web_set_tile_fields(1, "{\"texture\": \"samples/oak-light\", \"w\": 1200, \"h\": 200,"
                                             " \"color\": \"#c4a070\"}", 200.0));
            EXPECT_NEAR(tile_number(1, "h"), 200, 1e-9);
            EXPECT(strstr(tl_web_space_tile(1), "samples/oak-light") != NULL);
            EXPECT(!tl_web_set_tile_field(1, "w", "\"широкая\"", 300.0)); /* не тот тип */
            EXPECT(!tl_web_set_tile_field(1, "нет такого", "1", 300.0));
            EXPECT(!tl_web_set_tile_field(99, "w", "300", 300.0));
            EXPECT(strcmp(tl_web_space_tile(99), "null") == 0);
        }
        IT("сохраняет проект текстом файла; неверный текст не открывается, проект прежний")
        {
            tl_web_new_demo();
            EXPECT(tl_web_set_tile_field(1, "gap", "3", 0));
            const char *text = tl_web_save();
            char *saved = memcpy(malloc(strlen(text) + 1), text, strlen(text) + 1);
            EXPECT(!tl_web_open("{ это не JSON"));
            EXPECT(tl_web_error()[0] != '\0');
            EXPECT_NEAR(tile_number(1, "gap"), 3, 1e-9); /* проект прежний */
            tl_web_new_demo();
            EXPECT(tl_web_open(saved));
            EXPECT_NEAR(tile_number(1, "gap"), 3, 1e-9);
            EXPECT_INT(tl_web_undo_count(), 0); /* у открытого файла — своя история */
            free(saved);
        }
        IT("комната в точке; сводка к покупке по видам плитки")
        {
            char *text = read_fixture(TL_FIXTURES_DIR "/python_demo.tilelayout.json");
            tl_web_open(text);
            free(text);
            int left = tl_web_space_at(1000, 1000);
            EXPECT(left > 0 && strstr(tl_web_space_tile(left), "\"Спальня\"") != NULL);
            EXPECT_INT(tl_web_space_at(-5000, -5000), 0);
            TlJson *summary = tl_json_parse(tl_web_summary(), NULL, 0);
            const TlJson *types = tl_json_get(summary, "types");
            EXPECT(tl_json_length(types) >= 1);
            int to_buy = (int)tl_json_number_or(tl_json_get(tl_json_at(types, 0), "to_buy"), 0);
            int count = (int)tl_json_number_or(tl_json_get(tl_json_at(types, 0), "count"), 0);
            EXPECT(to_buy >= count && count > 0);
            EXPECT(tl_json_number_or(tl_json_get(summary, "floor_area"), 0) > 10);
            tl_json_free(summary);
        }
    }
}

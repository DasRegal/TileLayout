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

/* Поле ответа (JSON-объект) — строкой или числом. */
static double answer_number(const char *text, const char *key)
{
    TlJson *json = tl_json_parse(text, NULL, 0);
    double value = tl_json_number_or(tl_json_get(json, key), -1);
    tl_json_free(json);
    return value;
}

static bool answer_is(const char *text, const char *key, const char *expected)
{
    TlJson *json = tl_json_parse(text, NULL, 0);
    bool same = strcmp(tl_json_string_or(tl_json_get(json, key), ""), expected) == 0;
    tl_json_free(json);
    return same;
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
        IT("поля плитки — сразу нескольким комнатам, одним шагом отмены; длина ⇄ ширина — у каждой своя")
        {
            char *text = read_fixture(TL_FIXTURES_DIR "/python_demo.tilelayout.json");
            tl_web_open(text);
            free(text);
            EXPECT_NEAR(tile_number(2, "w"), 300, 1e-9); /* Ванная 300×300, Гостиная 600×300 */
            EXPECT(tl_web_set_rooms_tile_fields("[1, 2]", "{\"svp_long\": 3}", 10.0));
            EXPECT(tl_web_set_rooms_tile_fields("[1, 2]", "{\"svp_long\": 5}", 10.5)); /* та же серия */
            EXPECT(tl_web_set_rooms_tile_fields("[1, 2]", "{\"glue_mm\": 6, \"gap\": 1.5}", 11.0));
            EXPECT_NEAR(tile_number(1, "svp_long"), 5, 1e-9);
            EXPECT_NEAR(tile_number(2, "svp_long"), 5, 1e-9);
            EXPECT_NEAR(tile_number(2, "glue_mm"), 6, 1e-9);
            EXPECT_NEAR(tile_number(1, "gap"), 1.5, 1e-9);
            EXPECT_INT(tl_web_undo_count(), 2);
            EXPECT(tl_web_swap_tiles("[1, 2]", 12.0));
            EXPECT_NEAR(tile_number(1, "w"), 300, 1e-9);
            EXPECT_NEAR(tile_number(1, "h"), 600, 1e-9);
            EXPECT_NEAR(tile_number(2, "w"), 300, 1e-9);
            EXPECT(tl_web_undo() && tl_web_undo());
            EXPECT_NEAR(tile_number(1, "gap"), tile_number(3, "gap"), 1e-9); /* Спальня — пол Гостиной */
            EXPECT_NEAR(tile_number(2, "svp_long"), 5, 1e-9);
            EXPECT(tl_web_undo());
            EXPECT(tile_number(2, "svp_long") != 5);
            EXPECT(!tl_web_set_rooms_tile_fields("[1, 99]", "{\"gap\": 2}", 13.0)); /* нет комнаты — никому */
            EXPECT(!tl_web_set_rooms_tile_fields("[1, 2]", "{\"gap\": \"широкий\"}", 13.0));
            EXPECT(!tl_web_set_rooms_tile_fields("[]", "{\"gap\": 2}", 13.0));
            EXPECT(!tl_web_swap_tiles("не JSON", 13.0));
            EXPECT_INT(tl_web_undo_count(), 0);
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
        IT("рисование: дом контуром, комната внутри, перегородка, проём; выбор, свойства, удаление, отмена")
        {
            tl_web_open("{\"version\": 4, \"outlines\": [], \"partitions\": [], \"spaces\": []}");
            const char *house = tl_web_add_contour("[0, 0, 6000, 0, 6000, 4000, 0, 4000]", 250, 100, 1);
            EXPECT(answer_is(house, "result", "house"));
            int big = (int)answer_number(house, "space");
            EXPECT(big > 0);
            EXPECT(answer_is(tl_web_add_contour("[0, 0, 1000]", 250, 100, 2), "result", "invalid"));
            const char *room = tl_web_add_contour("[0, 0, 2000, 0, 2000, 1500, 0, 1500]", 250, 100, 3);
            EXPECT(answer_is(room, "result", "room"));
            int index = tl_web_add_partition(4000, 0, 4000, 4000, 100, 4);
            EXPECT(index >= 0);
            EXPECT(answer_is(tl_web_pick(4000, 3000, 20), "kind", "partition"));
            EXPECT(answer_is(tl_web_pick(-100, 2000, 20), "kind", "wall"));
            EXPECT(answer_is(tl_web_pick(5000, 3000, 20), "kind", "space"));
            EXPECT(answer_is(tl_web_pick(-5000, 0, 20), "kind", "none"));
            EXPECT(tl_web_set_partition_thickness(index, 120, 5));
            EXPECT_NEAR(answer_number(tl_web_object("partition", index, 0, 0), "thickness"), 120, 1e-9);
            EXPECT(!tl_web_set_partition_thickness(index, 1, 6)); /* меньше 10 мм */
            EXPECT(strcmp(tl_web_opening_preview(-5000, 0, 800, 20, 10), "null") == 0);
            EXPECT(strstr(tl_web_opening_preview(4000, 3000, 800, 20, 10), "\"partition\"") != NULL);
            int door = tl_web_add_opening(4000, 3000, 800, 20, 10, 7);
            EXPECT(door >= 0);
            EXPECT(answer_is(tl_web_pick(4035, 3000, 20), "kind", "opening")); /* в стороне от стыка */
            EXPECT(tl_web_set_opening_width(door, 900, 8));
            EXPECT_NEAR(answer_number(tl_web_object("opening", door, 0, 0), "width"), 900, 1e-9);
            int undo = tl_web_undo_count();
            EXPECT(tl_web_delete("partition", index, 0, 0, 9)); /* проём в ней уходит вместе с ней */
            EXPECT(answer_is(tl_web_pick(4000, 3000, 20), "kind", "space"));
            EXPECT(strcmp(tl_web_object("opening", door, 0, 0), "null") == 0);
            EXPECT(tl_web_undo());
            EXPECT_INT(tl_web_undo_count(), undo);
            EXPECT(answer_is(tl_web_pick(4035, 3000, 20), "kind", "opening")); /* в стороне от стыка */
            EXPECT(!tl_web_delete("partition", 99, 0, 0, 10));
        }
        IT("маркеры: угол дома, свободный конец, узел; перетаскивание угла и перегородки с отменой; толщина стены")
        {
            tl_web_open("{\"version\": 4, \"outlines\": [], \"partitions\": [], \"spaces\": []}");
            tl_web_add_contour("[0, 0, 6000, 0, 6000, 4000, 0, 4000]", 200, 100, 1);
            int index = tl_web_add_partition(3000, 0, 3000, 2500, 100, 2);
            const char *corner = tl_web_pick(6005, 3995, 20);
            EXPECT(answer_is(corner, "kind", "vertex") && answer_number(corner, "part") == 2);
            const char *free_end = tl_web_pick(3000, 2500, 20);
            EXPECT(answer_is(free_end, "kind", "partition") && answer_number(free_end, "part") == 2);
            EXPECT(strstr(tl_web_object("partition", index, 0, 0), "\"handles\"") != NULL);

            EXPECT(tl_web_drag_begin("vertex", 0, 2, 0, 6000, 4000));
            EXPECT(strstr(tl_web_drag_move(6302, 4403, 20, 10, false, 3), "\"vertex\"") != NULL);
            EXPECT(answer_is(tl_web_drag_end(), "kind", "vertex"));
            const char *moved = tl_web_pick(6300, 4400, 5); /* угол — по сетке 10 мм */
            EXPECT(answer_is(moved, "kind", "vertex") && answer_number(moved, "part") == 2);
            int undo = tl_web_undo_count();
            EXPECT(tl_web_drag_begin("partition", index, 2, 0, 3000, 2500));
            tl_web_drag_move(3000, 3000, 20, 10, false, 4);
            tl_web_drag_move(3000, 3200, 20, 10, false, 4.1);
            const char *after = tl_web_drag_end();
            EXPECT(answer_is(after, "kind", "partition"));
            EXPECT_NEAR(answer_number(tl_web_object("partition", (int)answer_number(after, "index"), 0, 0), "length"), 3200,
                        1e-9);
            EXPECT_INT(tl_web_undo_count(), undo + 1); /* всё перетаскивание — один шаг */
            EXPECT(tl_web_undo());
            EXPECT_NEAR(answer_number(tl_web_object("partition", index, 0, 0), "length"), 2500, 1e-9);
            EXPECT(!tl_web_drag_begin("space", 0, 0, 0, 100, 100)); /* комнату не тянут — дом целиком: */
            EXPECT(tl_web_drag_begin("house", 0, 0, 0, 1000, 1000));
            const char *house_moved = tl_web_drag_move(1500, 1000, 20, 10, true, 4.5);
            EXPECT(strstr(house_moved, "\"space\"") != NULL);
            tl_web_drag_end();
            EXPECT(answer_is(tl_web_pick(400, 2000, 5), "kind", "wall")); /* левая стена (300…500) — на 500 мм правее */
            EXPECT(tl_web_undo());
            EXPECT(tl_web_drag_begin("tiles", 0, 0, 0, 1000, 1000));
            tl_web_drag_move(1037, 1000, 20, 10, false, 4.7);
            tl_web_drag_end();
            EXPECT(tl_web_undo());

            EXPECT(tl_web_set_wall_thickness(0, 1, 300, 5));
            EXPECT_NEAR(answer_number(tl_web_object("wall", 0, 1, 0), "thickness"), 300, 1e-9);
            int node_partition = tl_web_add_partition(3000, 2500, 6000, 2500, 100, 6); /* узел в (3000, 2500) */
            EXPECT(node_partition >= 0);
            EXPECT(answer_is(tl_web_pick(3000, 2500, 20), "kind", "node"));
            EXPECT(tl_web_delete("vertex", 0, 2, 0, 7)); /* угол — дом стал треугольником */
            EXPECT_NEAR(answer_number(tl_web_object("wall", 0, 0, 0), "walls"), 3, 1e-9);
            EXPECT(!tl_web_delete("vertex", 0, 0, 0, 8)); /* у треугольника — нельзя */
        }
        IT("маркеры перегородки: концы, углы тела, середина, точки узла; угол тела меняет толщину у конца")
        {
            tl_web_open("{\"version\": 4, \"outlines\": [], \"partitions\": [], \"spaces\": []}");
            tl_web_add_contour("[0, 0, 6000, 0, 6000, 4000, 0, 4000]", 200, 100, 1);
            int index = tl_web_add_partition(3000, 0, 3000, 2500, 100, 2);
            TlJson *object = tl_json_parse(tl_web_object("partition", index, 0, 0), NULL, 0);
            const TlJson *handles = tl_json_get(object, "handles");
            int ends = 0, corners = 0, middles = 0, corner_part = -1;
            for (size_t i = 0; i < tl_json_length(handles); i++) {
                const char *shape = tl_json_string_or(tl_json_get(tl_json_at(handles, i), "shape"), "");
                ends += strcmp(shape, "end") == 0;
                middles += strcmp(shape, "middle") == 0;
                if (strcmp(shape, "corner") == 0) {
                    corners++;
                    corner_part = (int)tl_json_number_or(tl_json_get(tl_json_get(tl_json_at(handles, i), "grab"), "part"), -1);
                }
            }
            tl_json_free(object);
            EXPECT_INT(ends, 2);    /* оба конца свободны: один на стене, другой в комнате */
            EXPECT_INT(corners, 4); /* углы тела у обоих концов */
            EXPECT_INT(middles, 1);
            EXPECT(corner_part >= 3);
            /* угол тела у конца (3000, 2500) с левой стороны — тянем дальше влево: перегородка толще */
            object = tl_json_parse(tl_web_object("partition", index, 0, 0), NULL, 0);
            handles = tl_json_get(object, "handles");
            int left_corner = -1;
            double corner_x = 0, corner_y = 0;
            for (size_t i = 0; i < tl_json_length(handles); i++) {
                const TlJson *handle = tl_json_at(handles, i);
                const TlJson *point = tl_json_get(handle, "point");
                double x = tl_json_number_or(tl_json_at(point, 0), 0), y = tl_json_number_or(tl_json_at(point, 1), 0);
                if (strcmp(tl_json_string_or(tl_json_get(handle, "shape"), ""), "corner") == 0 && x < 3000 && y > 1000) {
                    left_corner = (int)tl_json_number_or(tl_json_get(tl_json_get(handle, "grab"), "part"), -1);
                    corner_x = x;
                    corner_y = y;
                }
            }
            tl_json_free(object);
            EXPECT(left_corner >= 3);
            EXPECT(tl_web_drag_begin("partition", index, left_corner, 0, corner_x, corner_y));
            tl_web_drag_move(corner_x - 100, corner_y, 20, 10, false, 3);
            tl_web_drag_end();
            EXPECT_NEAR(answer_number(tl_web_object("partition", index, 0, 0), "thickness"), 200, 1e-9);
            EXPECT(tl_web_set_partition_length(index, 2000, 3.5));
            EXPECT_NEAR(answer_number(tl_web_object("partition", index, 0, 0), "length"), 2000, 1e-9);
            EXPECT(tl_web_set_partition_angle(index, 300, 3.6)); /* вниз и вправо — внутрь дома */
            EXPECT_NEAR(answer_number(tl_web_object("partition", index, 0, 0), "angle"), 300, 0.1);
            EXPECT(!tl_web_set_partition_length(index, 1, 3.7));
            EXPECT(tl_web_undo() && tl_web_undo()); /* угол и длина — назад */

            tl_web_add_partition(3000, 2500, 6000, 2500, 100, 4); /* узел в конце */
            object = tl_json_parse(tl_web_object("partition", index, 0, 0), NULL, 0);
            handles = tl_json_get(object, "handles");
            int nodes = 0;
            for (size_t i = 0; i < tl_json_length(handles); i++)
                nodes += strcmp(tl_json_string_or(tl_json_get(tl_json_at(handles, i), "shape"), ""), "node") == 0;
            tl_json_free(object);
            EXPECT_INT(nodes, 1);
        }
        IT("линейка и шов: добавить, выбрать, протащить, ширина щели, удалить; стык полов в проёме")
        {
            tl_web_open("{\"version\": 4, \"outlines\": [], \"partitions\": [], \"spaces\": []}");
            tl_web_add_contour("[0, 0, 6000, 0, 6000, 4000, 0, 4000]", 200, 100, 1);
            int ruler = tl_web_add_ruler(500, 500, 2500, 500, 2);
            EXPECT_INT(ruler, 0);
            EXPECT(answer_is(tl_web_pick(1500, 505, 20), "kind", "ruler"));
            EXPECT_NEAR(answer_number(tl_web_pick(2500, 500, 20), "part"), 2, 1e-9);
            EXPECT(tl_web_drag_begin("ruler", ruler, 2, 0, 2500, 500));
            const char *moved = tl_web_drag_move(3000, 520, 20, 10, false, 3); /* выравнивание по горизонтали */
            EXPECT(strstr(moved, "\"ruler\"") != NULL);
            tl_web_drag_end();
            EXPECT_NEAR(answer_number(tl_web_object("ruler", ruler, 0, 0), "length"), 2500, 25);
            const char *measured = tl_web_measure_point(5990, 1000, false, 0, 0, "walls", 20, false);
            EXPECT(strstr(measured, "\"snapped\":true") != NULL); /* к грани стены x = 6000 */
            int joint = tl_web_add_joint(3000, 0, 3000, 4000, 10, 4);
            EXPECT_INT(joint, 0);
            EXPECT(strstr(tl_web_scene(), "\"joints\":[{") != NULL);
            EXPECT(answer_is(tl_web_pick(3003, 2000, 20), "kind", "joint"));
            EXPECT(tl_web_set_joint_gap(joint, 8, 5));
            EXPECT_NEAR(answer_number(tl_web_object("joint", joint, 0, 0), "gap"), 8, 1e-9);
            EXPECT(!tl_web_set_joint_gap(joint, 0, 6));
            EXPECT(tl_web_delete("joint", joint, 0, 0, 7));
            EXPECT(tl_web_delete("ruler", ruler, 0, 0, 8));
            EXPECT(strstr(tl_web_scene(), "\"rulers\":[]") != NULL);

            int partition = tl_web_add_partition(3000, 0, 3000, 4000, 100, 9);
            EXPECT(partition >= 0);
            int door = tl_web_add_opening(3000, 2000, 800, 20, 10, 10);
            EXPECT(door >= 0);
            const char *threshold = tl_web_pick(3000, 2000, 20);
            EXPECT(answer_is(threshold, "kind", "threshold") || answer_is(threshold, "kind", "opening"));
            EXPECT(tl_web_drag_begin("threshold", door, 1, 0, 3000, 2000));
            tl_web_drag_move(2960, 2000, 20, 10, true, 11);
            tl_web_drag_end();
            EXPECT(strcmp(tl_web_object("threshold", door, 1, 0), "null") != 0);
        }
        IT("по числам: точка угла, длина стены, проём от края, стык, блокировка, общий пол, новый угол и дом")
        {
            tl_web_open("{\"version\": 4, \"outlines\": [], \"partitions\": [], \"spaces\": []}");
            tl_web_add_contour("[0, 0, 6000, 0, 6000, 4000, 0, 4000]", 200, 100, 1);
            EXPECT(answer_is(tl_web_set_point("vertex", 0, 2, 0, 6500, 4500, 2), "kind", "vertex"));
            EXPECT_NEAR(answer_number(tl_web_object("vertex", 0, 2, 0), "x"), 6500, 1e-9);
            EXPECT(tl_web_undo());
            EXPECT(tl_web_set_wall_length(0, 0, 7400, true, true, 3)); /* наружная 7400 — внутренняя 7000 */
            EXPECT_NEAR(answer_number(tl_web_object("wall", 0, 0, 0), "length"), 7000, 1e-6);
            EXPECT(tl_web_set_locked("wall", 0, 0, true, 4));
            EXPECT(!tl_web_set_wall_length(0, 0, 6000, false, true, 5));
            EXPECT(tl_web_set_locked("wall", 0, 0, false, 6));

            int index = tl_web_add_partition(3000, 0, 3000, 4000, 100, 7);
            int door = tl_web_add_opening(3000, 2000, 800, 20, 10, 8);
            EXPECT(tl_web_place_opening(door, 500, false, 9));
            EXPECT_NEAR(answer_number(tl_web_object("opening", door, 0, 0), "from_start"), 500, 0.1);
            EXPECT(tl_web_set_threshold(door, 30, 0, 10)); /* глубина проёма — 100 мм (перегородка) */
            const char *threshold = tl_web_object("threshold", door, 1, 0);
            EXPECT_NEAR(answer_number(threshold, "depth"), 100, 1e-9);
            EXPECT_NEAR(answer_number(threshold, "right"), 30, 1e-9);
            EXPECT(tl_web_set_locked("partition", index, 0, true, 11));
            EXPECT(!tl_web_set_partition_thickness(index, 200, 12));
            EXPECT(tl_web_set_locked("partition", index, 0, false, 13));

            int left = tl_web_space_at(1000, 1000), right = tl_web_space_at(5000, 1000);
            EXPECT(strstr(tl_web_space_tile(left), "\"sources\":[{") != NULL);
            EXPECT(tl_web_set_space_source(left, right, 14));
            EXPECT(strstr(tl_web_space_tile(left), "\"source\":\"") != NULL); /* пол как у правой */
            EXPECT(!tl_web_set_space_source(left, left, 15));
            EXPECT(tl_web_set_space_source(left, 0, 16));

            EXPECT(tl_web_insert_vertex(0, 0, 1503, -100, 10, 17)); /* угол на верхней стене */
            EXPECT_NEAR(answer_number(tl_web_object("vertex", 0, 1, 0), "x"), 1500, 1e-6);
            int house = tl_web_add_house(3000, 2000, 200, 18);
            EXPECT(house > 0 && house != left && house != right);
        }
        IT("притягивание к углу дома и к сетке; раскладка от угла и с минимумом плиток; название комнаты")
        {
            tl_web_open("{\"version\": 4, \"outlines\": [], \"partitions\": [], \"spaces\": []}");
            const char *house = tl_web_add_contour("[0, 0, 3000, 0, 3000, 2000, 0, 2000]", 250, 100, 1);
            int id = (int)answer_number(house, "space");
            TlJson *snap = tl_json_parse(tl_web_snap(6, -4, false, 0, 0, "[]", 20, 10, false), NULL, 0);
            const TlJson *point = tl_json_get(snap, "point");
            EXPECT_NEAR(tl_json_number_or(tl_json_at(point, 0), -1), 0, 1e-9); /* угол дома */
            EXPECT_NEAR(tl_json_number_or(tl_json_at(point, 1), -1), 0, 1e-9);
            tl_json_free(snap);
            snap = tl_json_parse(tl_web_snap(1234, 777, false, 0, 0, "[]", 1, 10, false), NULL, 0);
            point = tl_json_get(snap, "point");
            EXPECT_NEAR(tl_json_number_or(tl_json_at(point, 0), -1), 1230, 1e-9); /* сетка 10 мм */
            tl_json_free(snap);
            TlJson *aligned = tl_json_parse(tl_web_align_tiles(id, "top_left", 2), NULL, 0);
            EXPECT(tl_json_bool_or(tl_json_get(aligned, "ok"), false));
            tl_json_free(aligned);
            aligned = tl_json_parse(tl_web_align_tiles(id, "min_tiles", 3), NULL, 0);
            EXPECT(tl_json_bool_or(tl_json_get(aligned, "ok"), false));
            tl_json_free(aligned);
            aligned = tl_json_parse(tl_web_align_tiles(id, "nonsense", 4), NULL, 0);
            EXPECT(!tl_json_bool_or(tl_json_get(aligned, "ok"), true));
            tl_json_free(aligned);
            EXPECT(tl_web_set_space_name(id, "Кухня", 5));
            EXPECT(strstr(tl_web_space_tile(id), "\"Кухня\"") != NULL);
            EXPECT(!tl_web_set_space_name(id, "", 6));
        }
        IT("для печати: номер у каждой плитки плана, карточки подрезок с контурами кусков")
        {
            char *text = read_fixture(TL_FIXTURES_DIR "/python_demo.tilelayout.json");
            tl_web_open(text);
            free(text);
            TlJson *numbers = tl_json_parse(tl_web_tile_numbers(), NULL, 0);
            int tiles = 0, numbered = 0;
            for (size_t i = 0; i < tl_json_length(numbers); i++) {
                const TlJson *space = tl_json_at(numbers, i);
                int id = (int)tl_json_number_or(tl_json_get(space, "space"), 0);
                numbered += (int)tl_json_length(tl_json_get(space, "numbers")) / 5;
                tiles += scene_count(id);
            }
            EXPECT(numbered > 0);
            EXPECT_INT(numbered, tiles);
            tl_json_free(numbers);
            TlJson *cards = tl_json_parse(tl_web_cut_cards(), NULL, 0);
            EXPECT(tl_json_length(cards) > 0);
            const TlJson *card = tl_json_at(cards, 0);
            EXPECT(tl_json_string_or(tl_json_get(card, "title"), "")[0] != '\0');
            const TlJson *part = tl_json_at(tl_json_get(card, "parts"), 0);
            EXPECT(tl_json_length(tl_json_at(tl_json_get(part, "shapes"), 0)) >= 6); /* хотя бы треугольник */
            EXPECT(tl_json_number_or(tl_json_get(tl_json_get(card, "tile"), "w"), 0) > 0);
            tl_json_free(cards);
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

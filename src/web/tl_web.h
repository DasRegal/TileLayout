/*
 * Ядро для веб-версии: один открытый проект, история правок и обмен с
 * JavaScript строками JSON. Функции — простые (числа и строки), чтобы их было
 * легко вызывать из WebAssembly; здесь нет ничего от Emscripten — модуль
 * собирается и проверяется обычным компилятором.
 *
 * Возвращённые строки принадлежат модулю и живут до следующего вызова той же
 * функции.
 */
#ifndef TL_WEB_H
#define TL_WEB_H

#include <stdbool.h>

/* ---- проект ---------------------------------------------------------------- */

/* Пример проекта (как при первом запуске программы). */
void tl_web_new_demo(void);
/* Проект из текста файла; false — ошибка (см. tl_web_error), проект прежний. */
bool tl_web_open(const char *json);
const char *tl_web_error(void);
/* Версия программы (project() в CMakeLists.txt). */
const char *tl_web_version(void);
/* Проект — текстом файла (*.tilelayout.json, как у настольной программы). */
const char *tl_web_save(void);

/* ---- что рисовать ------------------------------------------------------------ */

/* Сцена в мм (формат — в tl_web_scene.c). */
const char *tl_web_scene(void);
/* Комната в точке (мм) или 0. */
int tl_web_space_at(double x, double y);

/* ---- плитка комнаты ------------------------------------------------------- */

/* Настройки плитки комнаты (у общего пола — комнаты-источника):
   {"space": id, "name": "...", "source": "..." | null, "source_id": id или 0 — «своя раскладка»,
    "sources": [{"id", "name"}] — с кем можно сделать общий пол, "area": м², "perimeter": мм,
    "tile": {ключ: значение}, "formats": ["600×600 мм", …] — виды плитки рисунка укладки,
    "doors": [{"index", "name"}], "rulers": [{"index", "name", "length"}] — двери и линейки комнаты
    (index — номер в проекте) для раскладки от двери и по линейке}.
   Ключи — как в файле проекта (tl_tile_settings_fields). */
const char *tl_web_space_tile(int space_id);
/* Поле плитки комнаты; value — JSON-значение (число, true/false, строка).
   now_seconds — для склейки серии правок одного поля в один шаг отмены. */
bool tl_web_set_tile_field(int space_id, const char *key, const char *value, double now_seconds);
/* Несколько полей одним шагом отмены: fields — JSON-объект {ключ: значение}. */
bool tl_web_set_tile_fields(int space_id, const char *fields, double now_seconds);
/* Те же поля нескольким комнатам одним шагом отмены (выделение с Shift):
   space_ids — JSON-массив id, fields — JSON-объект {ключ: значение}; серия правок
   одного поля тех же комнат склеивается. false — нет такой комнаты или значение не то. */
bool tl_web_set_rooms_tile_fields(const char *space_ids, const char *fields, double now_seconds);
/* Зона (zone — "border" | "carpet" пола, tl_zone.h; "wall_lower" | "wall_frieze" | "wall_main" —
   полосы стен, tl_wall.h) у комнат space_ids: fields — {"enabled", "fit",
   "width", "height", "dx", "dy" и поля плитки зоны}; одно поле — серия правок склеивается.
   В tl_web_space_tile у зоны ещё "laid_width", "laid_height" — размер, как кладут (с fit). */
bool tl_web_set_zone_fields(const char *space_ids, const char *zone, const char *fields, double now_seconds);
/* Поменять длину и ширину плитки у комнат space_ids (JSON-массив id) — одним шагом. */
bool tl_web_swap_tiles(const char *space_ids, double now_seconds);
/* Высота помещения и облицовка стен у комнат space_ids: fields — {"room_height", "enabled", "height"}
   (height 0 — до потолка). В tl_web_space_tile — объект "walls" с полосами "bands". */
bool tl_web_set_walls(const char *space_ids, const char *fields, double now_seconds);
/* Часть пола region комнаты (между линиями раздела и швами; в tl_web_space_tile — "regions"):
   covering "room" | "own" (своя плитка) | "none" (без плитки) | "" — не менять; name — название участка
   ("" — не менять). Плитка участка — tl_web_set_zone_fields(…, "area:<region>", …). */
bool tl_web_set_area(int space_id, int region, const char *covering, const char *name, double now_seconds);
/* Предмет (kind — "bath" | "tray" | "installation" | "cabinet" | "kitchen") в точке; у стены — спинкой
   к ней (free — без этого) → номер. Правка — fields {"kind" (размеры — обычные для вида), "w", "d", "h",
   "angle", "x", "y", "under", "behind"}. В сцене — "fixtures", выбор и перетаскивание — "fixture". */
int tl_web_add_fixture(const char *kind, double x, double y, bool free, double now_seconds);
/* Трап (kind — "point" | "linear") в точке; линейный у стены — вдоль неё (free — без этого) → номер.
   Правка — fields {"kind", "size", "length", "angle", "slope", "x", "y", "envelope"}. В сцене — "drains"
   и "slopes" (разрезы конверта), выбор и перетаскивание — "drain" (tl_web_object: и "drop" — перепад, мм). */
int tl_web_add_drain(const char *kind, double x, double y, bool free, double now_seconds);
bool tl_web_set_drain(int index, const char *fields, double now_seconds);
bool tl_web_set_fixture(int index, const char *fields, double now_seconds);
/* Линия раздела покрытия (шов gap мм) → номер среди швов (tl_web_object "joint") или −1. */
int tl_web_add_divider(double x0, double y0, double x1, double y1, double gap, double now_seconds);
/* Развёртка стен комнаты (формат — в tl_web_walls.c); "null" — нет комнаты. */
const char *tl_web_room_walls(int space_id);
/* Правка на развёртке комнаты space_id; wall — номер стены в развёртке, точка на стене — x от левого
   угла, h от пола. Сдвиг плитки стены (x — вправо, y — вверх); «Швы как у пола» — у стены wall или
   у всех (−1), ответ — у скольких совмещено. */
bool tl_web_set_wall_offset(int space_id, int wall, double x, double y, double now_seconds);
int tl_web_wall_align_floor(int space_id, int wall, double now_seconds);
/* Отверстие и линейка на стене: add — номер или −1; линейка — на своей стене. */
int tl_web_add_wall_pipe(int space_id, int wall, double x, double h, double diameter, double now_seconds);
bool tl_web_set_wall_pipe(int space_id, int index, int wall, double x, double h, double diameter, double now_seconds);
int tl_web_add_wall_ruler(int space_id, int wall, double x0, double h0, double x1, double h1, double now_seconds);
bool tl_web_set_wall_ruler(int space_id, int index, double x0, double h0, double x1, double h1, double now_seconds);
/* Панно на стене wall развёртки (посередине) или на полу (wall −1, в центре) → номер или −1;
   правка — fields {"columns", "rows", "w", "h", "gap", "x", "y", "price", "color", "texture", "wall"}
   (что есть; одно поле — серия правок склеивается). В tl_web_space_tile — "pannos", в сцене и развёртке —
   их плитки. Удаление — tl_web_delete_wall_item(…, "panno", …). */
int tl_web_add_panno(int space_id, int wall, double now_seconds);
bool tl_web_set_panno(int space_id, int index, const char *fields, double now_seconds);
/* kind — "wall_pipe" | "wall_ruler" | "panno". */
bool tl_web_delete_wall_item(int space_id, const char *kind, int index, double now_seconds);
/* Точка развёртки, притянутая к углам, полу, проёмам и швам плитки: "[x, y]". */
const char *tl_web_walls_snap(int space_id, double x, double y, double tolerance);

/* Раскладка комнаты: смещение сетки по правилу mode — "top_left", "top_right",
   "bottom_left", "bottom_right" (целая плитка в углу), "center" (центр плитки в
   центре комнаты), "center_joint" (там — пересечение швов), "min_tiles"
   (меньше всего плиток к покупке), "min_narrow" (меньше узких подрезок), "door:N" (от проёма N
   проекта: от порога целый ряд), "ruler_joint:N" / "ruler_center:N" (по линейке N проекта — шов
   или середина плитки).
   Ответ — {"ok": bool, "message": "что получилось"}. */
const char *tl_web_align_tiles(int space_id, const char *mode, double now_seconds);
bool tl_web_set_space_name(int space_id, const char *name, double now_seconds);
/* Пол комнаты как у другой (общая сетка) или свой (source_id 0). */
bool tl_web_set_space_source(int space_id, int source_id, double now_seconds);

/* ---- правка плана (tl_web_edit.c) ---------------------------------------------
 * Предметы плана — {"kind": "partition" | "opening" | "house", "index": номер в
 * списке проекта}; номера живут до следующей правки. tolerance — допуск
 * попадания в мм (из масштаба вида), grid — шаг сетки, мм (0 — без сетки). */

/* Точка курсора с привязками: к углам, концам перегородок, граням стен, сетке,
   выравнивание с опорными точками и с началом start (has_start).
   refs — JSON-массив [x0, y0, …] — точки рисуемого контура. free — без привязок.
   {"point": [x, y], "on_face": bool, "face": [x0, y0, x1, y1] | null,
    "guides": [[x, y, вертикальная], …]} */
const char *tl_web_snap(double x, double y, bool has_start, double start_x, double start_y, const char *refs,
                        double tolerance, double grid, bool free);
/* Контур [x0, y0, …]: на пустом месте — дом, внутри дома — комната.
   {"result": "house" | "room" | "partitions" | "invalid", "space": id комнаты в контуре или 0} */
const char *tl_web_add_contour(const char *points, double house_thickness, double partition_thickness,
                               double now_seconds);
/* Перегородка; ответ — номер или −1 (слишком короткая). */
int tl_web_add_partition(double x0, double y0, double x1, double y1, double thickness, double now_seconds);
/* Проём шириной width на стене или перегородке у точки: предпросмотр — вырез
   {"cut": [...], "host": "wall" | "partition"} или null; добавить — номер или −1. */
const char *tl_web_opening_preview(double x, double y, double width, double tolerance, double grid);
int tl_web_add_opening(double x, double y, double width, double tolerance, double grid, double now_seconds);

/* Линейка и деформационный шов (from — to); ответ — номер. Шов режет плитку щелью gap, мм. */
int tl_web_add_ruler(double x0, double y0, double x1, double y1, double now_seconds);
int tl_web_add_joint(double x0, double y0, double x1, double y1, double gap, double now_seconds);
/* Отверстие под трубу диаметром diameter, мм (коронка); ответ — номер или −1. */
int tl_web_add_pipe(double x, double y, double diameter, double now_seconds);
/* Имя линейки или проёма (kind "ruler" | "opening"); false — пустое или длиннее TL_NAME_LENGTH байт. */
bool tl_web_set_name(const char *kind, int index, const char *name, double now_seconds);
/* Центр и диаметр трубы — по числам с панели. */
bool tl_web_set_pipe(int index, double x, double y, double diameter, double now_seconds);
bool tl_web_set_joint_gap(int index, double gap, double now_seconds);
/* Конец линейки ("all") или шва ("walls"): притягивание к граням, осям, швам
   плитки и выравнивание от anchor. {"point": [x, y], "snapped": bool, "guides": [...]} */
const char *tl_web_measure_point(double x, double y, bool has_anchor, double anchor_x, double anchor_y,
                                 const char *targets, double tolerance, bool free);

/* Предмет под точкой — линейки и швы поверх всего, маркеры важнее тел: угол
   дома, середина узла перегородок, примыкание, свободный конец перегородки,
   стык полов в проёме, проём, перегородка, стена дома, комната; трубы — сразу после швов.
   {"kind": "ruler" | "joint" | "pipe" | "vertex" | "node" | "tee" | "partition" | "threshold" | "opening" | "wall"
            | "space" | "none",
    "index": номер (у угла и стены — дома; у узла и примыкания — перегородки; у стыка — проёма),
    "part": угол или стена дома; у перегородки, линейки, шва 0 — тело, 1 — начало, 2 — конец;
            у стыка 1 — целиком, 2 — конец у косяка в начале проёма, 3 — в конце,
    "end": конец перегородки в узле или примыкании (TlPartitionEnd: 1 — начало, 2 — конец),
    "space": id комнаты} */
const char *tl_web_pick(double x, double y, double tolerance);
/* Свойства предмета (как от tl_web_pick) для панели и подсветки; null — нет такого.
   Общее: "outline" — контуры для подсветки, "locked", "handles" — маркеры
   [{"point": [x, y], "shape": "end" | "corner" | "middle" | "node" | "point",
     "grab": предмет для tl_web_drag_begin}] (у перегородки — концы, углы тела, точки
   узлов и примыканий, середина);
   перегородка — "thickness", "length", "angle"; проём — "width", "host", "from_start",
   "to_end" (мм от краёв стены); стена — "thickness", "length" (внутр.), "outer_length",
   "walls"; стык — "depth", "right", "skew" (мм), "right_room", "left_room" ("" — улица);
   угол и узел — "x", "y"; угол — "walls"; узел — "count" (перегородок); линейка — "length";
   шов — "gap", "length"; труба — "x", "y", "diameter"; у линейки и проёма — "name". */
const char *tl_web_object(const char *kind, int index, int part, int end);
bool tl_web_set_partition_thickness(int index, double thickness, double now_seconds);
/* Длина (конец «начало + длина») и угол перегородки (0° — вправо, против часовой),
   как поля на панели программы. */
bool tl_web_set_partition_length(int index, double length, double now_seconds);
bool tl_web_set_partition_angle(int index, double degrees, double now_seconds);
bool tl_web_set_opening_width(int index, double width, double now_seconds);
/* Вид проёма ("door" | "window" | "arch"; "" — не менять), высота и низ от пола (подоконник), мм. */
bool tl_web_set_opening(int index, const char *kind, double height, double sill, double now_seconds);
/* Откосы проёма плиткой: глубина, мм; 0 — без откосов. */
bool tl_web_set_opening_reveal(int index, double depth, double now_seconds);
/* Толщина стены дома (вместе со смежной стеной соседнего дома). */
bool tl_web_set_wall_thickness(int house, int wall, double thickness, double now_seconds);
/* Точка предмета — в (x, y): угол дома ("vertex"), узел ("node": part 0 — середина,
   1… — угол), примыкание ("tee"). Ответ — предмет после правки (примыкание за
   гранью становится узлом) или null — нельзя. */
const char *tl_web_set_point(const char *kind, int index, int part, int end, double x, double y, double now_seconds);
/* Длина стены дома: внутренняя или наружная (outer); keep_angles — углы между
   стенами сохраняются (сдвигается соседняя стена). */
bool tl_web_set_wall_length(int house, int wall, double length, bool outer, bool keep_angles, double now_seconds);
/* Проём — на distance мм от начала стены (from_end — от конца) до своего края. */
bool tl_web_place_opening(int index, double distance, bool from_end, double now_seconds);
/* Стык полов: right — мм глубины проёма у комнаты справа (по середине стыка),
   skew — на сколько мм конец у косяка в конце дальше, чем в начале. */
bool tl_web_set_threshold(int index, double right, double skew, double now_seconds);
/* Заблокировать от перемещения: стену ("wall"), перегородку, проём. */
bool tl_web_set_locked(const char *kind, int index, int part, bool locked, double now_seconds);
/* Новый угол дома на стене wall в точке (x, y) (она проецируется на стену; по сетке grid). */
bool tl_web_insert_vertex(int house, int wall, double x, double y, double grid, double now_seconds);
/* Новый дом по внутренним размерам — справа от существующих. Ответ — id его комнаты. */
int tl_web_add_house(double width, double height, double thickness, double now_seconds);

/* Удалить: перегородку, проём, дом (kind "wall" или "house"), угол дома (если углов
   больше трёх), узел (сломанная перегородка выпрямляется), линейку, шов, трубу.
   Заблокированное — нельзя. */
bool tl_web_delete(const char *kind, int index, int part, int end, double now_seconds);

/* Перетаскивание предмета (как от tl_web_pick) или kind "house" — дом комнаты в
   точке (x, y) целиком, "tiles" — сетка плитки этой комнаты.
   begin — false, если тянуть нельзя; move — шаг (первый запоминает проект для
   отмены), ответ — {"guides": [...], "selection": предмет сейчас}; end — отпустили,
   ответ — предмет после правки (перегородки могли слиться). */
bool tl_web_drag_begin(const char *kind, int index, int part, int end, double x, double y);
const char *tl_web_drag_move(double x, double y, double tolerance, double grid, bool free, double now_seconds);
const char *tl_web_drag_end(void);

/* ---- печать (tl_web_print.c) ----------------------------------------------------- */

/* Сквозные номера плиток по комнатам — для плана с номерами. */
const char *tl_web_tile_numbers(void);
/* Карточки чертежей подрезок. */
const char *tl_web_cut_cards(void);
/* Карта раскроя (формат — в tl_web_print.c): что резать стопкой, какие две подрезки — из одной плитки. */
const char *tl_web_cut_map(void);
/* Объёмный вид комнаты (формат — в tl_web_walls.c): камера — углы, °, и расстояние, мм; холст в пикселях. */
const char *tl_web_room_3d(int space_id, double yaw, double pitch, double distance, double width, double height);

/* ---- подложка (tl_web_underlay.c) ---------------------------------------------- */

/* Вся подложка с содержимым (формат — в tl_web_underlay.c); в сцене — без содержимого. */
const char *tl_web_underlay(void);
/* Фото: data:-URL (JavaScript уменьшает картинку и сжимает в JPEG) и размер в пикселях. */
bool tl_web_set_underlay_image(const char *data_url, int width, int height, double now_seconds);
/* DXF-план текстом; false — причина в tl_web_error. */
bool tl_web_load_underlay_dxf(const char *text, double now_seconds);
/* Поля {"visible", "opacity", "scale", "x", "y", "dx", "dy" — сдвиг} — любые из них. */
bool tl_web_set_underlay(const char *fields, double now_seconds);
/* Точки (ax, ay) и (bx, by) на плане на самом деле в distance мм друг от друга. */
bool tl_web_calibrate_underlay(double ax, double ay, double bx, double by, double distance, double now_seconds);
void tl_web_clear_underlay(double now_seconds);

/* Чертёж DXF (tl_dxf.h): cuts — чертежи подрезок для станка, иначе план. */
const char *tl_web_dxf(int cuts);

/* ---- расчёт и отмена ---------------------------------------------------------- */

/* Сводка к покупке (формат — в tl_web_summary.c). */
const char *tl_web_summary(void);

bool tl_web_undo(void);
bool tl_web_redo(void);
int tl_web_undo_count(void);
int tl_web_redo_count(void);

#endif

/*
 * Проект — всё, что есть на плане: дома (контуры наружных стен),
 * перегородки, проёмы, деформационные швы, комнаты и линейки. Комнаты не рисуются, а вычисляются:
 * пол каждого дома минус тела перегородок; каждая замкнутая область —
 * комната. Настройки комнаты переходят к новой области с наибольшим
 * перекрытием (tl_project_update_spaces).
 *
 * Правка геометрии оформляется как транзакция: tl_project_begin_edit()
 * запоминает, какие концы перегородок лежат на каких гранях и где стоят
 * проёмы; после изменения tl_project_end_edit() возвращает их на свои
 * стены и пересчитывает комнаты.
 */
#ifndef TL_PROJECT_H
#define TL_PROJECT_H

#include "tl_band.h"
#include "tl_expansion_joint.h"
#include "tl_opening.h"
#include "tl_outline.h"
#include "tl_partition.h"
#include "tl_ruler.h"
#include "tl_space.h"

#define TL_MIN_SPACE_AREA 20000.0 /* мм²: меньшие области (щели) — не комнаты */

TL_ARRAY_DEFINE(TlOutlineList, TlOutline *, tl_outline_list)
TL_ARRAY_DEFINE(TlPartitionList, TlPartition *, tl_partition_list)
TL_ARRAY_DEFINE(TlOpeningList, TlOpening *, tl_opening_list)
TL_ARRAY_DEFINE(TlRulerList, TlRuler *, tl_ruler_list)
TL_ARRAY_DEFINE(TlExpansionJointList, TlExpansionJoint *, tl_expansion_joint_list)

typedef struct TlProject {
    TlOutlineList outlines;
    TlPartitionList partitions;
    TlOpeningList openings;
    TlSpaceList spaces;
    TlRulerList rulers;
    TlExpansionJointList expansion_joints;
    int next_space_id;
} TlProject;

typedef struct TlWallRef {
    TlOutline *outline;
    size_t wall;
} TlWallRef;

TL_ARRAY_DEFINE(TlWallRefList, TlWallRef, tl_wall_ref_list)
TL_ARRAY_DEFINE(TlBandList, TlBand, tl_band_list)

/* Узел: совпадающие концы двух и более перегородок (см. tl_project_nodes.c).
 * Концы — против часовой стрелки по направлению от узла вдоль перегородки. */
#define TL_NODE_MAX 8

typedef struct TlNodeEnd {
    TlPartition *partition;
    TlPartitionEnd end;
} TlNodeEnd;

typedef struct TlNode {
    TlVec2 point;
    TlNodeEnd ends[TL_NODE_MAX];
    size_t count;
} TlNode;

TL_ARRAY_DEFINE(TlNodeList, TlNode, tl_node_list)

/* Угол узла между концами k и k + 1: first — на левом боку перегородки k,
   second — на правом боку k + 1. Обычно бока сходятся в одну точку (joined);
   если они параллельны или сходятся слишком далеко — у каждой свой угол. */
typedef struct TlNodeCorner {
    TlVec2 first, second;
    bool joined;
} TlNodeCorner;

/* Грань, к которой примыкают перегородки: внутренняя сторона стены дома
 * (side = 0) или бок перегородки (side = +1 / -1). */
typedef struct TlFace {
    TlHost host;
    int side;
    TlVec2 start, end;
} TlFace;

TL_ARRAY_DEFINE(TlFaceList, TlFace, tl_face_list)

typedef struct TlEditToken TlEditToken;

/* Как завершать правку. */
typedef struct TlEditOptions {
    TlPartition *master;          /* перегородку двигает пользователь: узлы идут за ней */
    TlPartition *frozen;          /* концы этой перегородки не возвращать на грани */
    TlVec2 translate;             /* сдвиг перегородок внутри перемещаемого дома */
    bool has_translate;
    bool keep_ends;               /* концы на гранях — на месте: перегородки поворачиваются вокруг них
                                     (точку примыкания увели в узел — та ломается, а не едет) */
    bool skip_space_update;
} TlEditOptions;

/* ---- создание ------------------------------------------------------------ */

TlProject *tl_project_new(void);
void tl_project_free(TlProject *project);
void tl_project_clear(TlProject *project);
TlProject *tl_project_demo(void);
int tl_project_take_space_id(TlProject *project);

void tl_project_add_outline(TlProject *project, TlOutline *outline);
void tl_project_add_partition(TlProject *project, TlPartition *partition);
void tl_project_add_opening(TlProject *project, TlOpening *opening);
void tl_project_add_ruler(TlProject *project, TlRuler *ruler);
/* Швы влияют на раскладку: после добавления, удаления и правки шва —
   tl_project_update_spaces. */
void tl_project_add_expansion_joint(TlProject *project, TlExpansionJoint *joint);
void tl_project_delete_partition(TlProject *project, TlPartition *partition);
void tl_project_delete_opening(TlProject *project, TlOpening *opening);
void tl_project_delete_ruler(TlProject *project, TlRuler *ruler);
void tl_project_delete_all_rulers(TlProject *project);
void tl_project_delete_expansion_joint(TlProject *project, TlExpansionJoint *joint);
/* Удаляет дом вместе с перегородками внутри и осиротевшими проёмами. */
void tl_project_delete_outline(TlProject *project, TlOutline *outline);

/* ---- добавление на план (tl_project_commands.c) ---------------------------- */

typedef enum TlContourResult {
    TL_CONTOUR_HOUSE,      /* на пустом месте — новый дом */
    TL_CONTOUR_ROOM,       /* внутри дома — комната из перегородок по сторонам контура */
    TL_CONTOUR_PARTITIONS  /* внутри дома, но замкнутой комнаты не получилось */
} TlContourResult;

TlContourResult tl_project_add_contour(TlProject *project, const TlVec2 *corners, size_t count,
                                       double house_thickness, double partition_thickness);
/* Новая перегородка; продолжила другую по прямой — сливается с ней. Возвращает
   перегородку на этом месте (NULL — короче 1 мм). */
TlPartition *tl_project_draw_partition(TlProject *project, TlVec2 start, TlVec2 end, double thickness);
/* Проём шириной width с центром along на полосе стены; NULL — стена короче проёма. */
TlOpening *tl_project_place_opening(TlProject *project, const TlBand *band, double along, double width);

/* ---- правка по числам (поля панели) --------------------------------------------
 * false — заблокировано или неприменимо; проект после — согласован. */

/* Угол дома — в точку (перегородки и проёмы на его стенах следуют). */
bool tl_project_set_vertex(TlProject *project, TlOutline *outline, size_t index, TlVec2 point);
/* Точка узла — в точку: index 0 — середина (узел целиком), 1… — угол узла. */
bool tl_project_set_node_point(TlProject *project, const TlNode *node, int index, TlVec2 point);
/* Точка примыкания — в точку: вдоль бока скользит, за грань — узел (*became_node). */
bool tl_project_set_tee_point(TlProject *project, TlPartition *branch, TlPartitionEnd end, TlVec2 point,
                              bool *became_node);
/* Внутренняя длина стены; keep_angles — углы между стенами сохраняются (сдвигается
   соседняя стена), иначе — соседняя стена поворачивается. linked — со смежной
   стеной соседнего дома. */
bool tl_project_set_wall_length(TlProject *project, TlOutline *outline, size_t wall, double inner_length,
                                bool keep_angles, bool linked);
/* Проём — на distance мм от начала стены (from_end — от конца) до своего края;
   концы стены — как у tl_project_opening_span. */
bool tl_project_place_opening_at(TlProject *project, TlOpening *opening, double distance, bool from_end);
/* Стык полов в проёме: middle — доля глубины проёма посередине, skew — разница
   долей у косяков (наклон); концы не выходят за грани. */
bool tl_project_set_threshold(TlProject *project, TlOpening *opening, double middle, double skew);

/* ---- комнаты ------------------------------------------------------------- */

void tl_project_update_spaces(TlProject *project);
TlSpace *tl_project_space_by_id(TlProject *project, int id);
TlSpace *tl_project_space_at(TlProject *project, TlVec2 point);

/* ---- тела стен ----------------------------------------------------------- */

TlBandList tl_project_bands(TlProject *project);
bool tl_project_band_of(TlProject *project, TlHost host, TlBand *band);
/* Стена, в теле которой лежит точка (самая толстая из подходящих). */
bool tl_project_find_band(TlProject *project, TlVec2 point, double tolerance, TlBand *band);

/* ---- соседние дома ------------------------------------------------------- */

TlWallRefList tl_project_linked_walls(TlProject *project, TlOutline *outline, size_t wall);
void tl_project_shift_wall(TlProject *project, TlOutline *outline, size_t wall, TlVec2 offset,
                           bool linked);
void tl_project_set_thickness(TlProject *project, TlOutline *outline, size_t wall, double value,
                              bool linked);

/* ---- перегородки: узлы и тела -------------------------------------------- */

TlNodeList tl_project_nodes(TlProject *project);
bool tl_project_node_of(TlProject *project, const TlPartition *partition, TlPartitionEnd end, TlNode *node);
/* Углы узла: столько же, сколько концов; corners — не меньше TL_NODE_MAX. */
size_t tl_node_corners(const TlNode *node, TlNodeCorner *corners);
/* Середина узла — среднее его углов: за неё узел двигают. */
TlVec2 tl_node_center(const TlNode *node);
/* Углы тела у конца index в узле: левый и правый по направлению от узла. */
void tl_node_end_corners(const TlNode *node, size_t index, TlVec2 *left, TlVec2 *right);
/* Середина узла из трёх и больше перегородок (или клин у очень острого угла). */
bool tl_node_core(const TlNode *node, TlPolygon *core);
/* Заплатка угла узла для силуэта: угол между боками, сошедшимися в corner, и
   середина узла. Тела у узла срезаны по диагонали угла — их обводка у острого
   конца срезана, а заплатка даёт обводке настоящий угол. false — бока не
   сошлись или продолжают друг друга. */
bool tl_node_corner_patch(const TlNode *node, size_t corner, TlPolygon *patch);
/* В узле есть заблокированная перегородка. */
bool tl_node_locked(const TlNode *node);
/* Все концы узла — в точку. */
void tl_project_move_node(TlProject *project, const TlNode *node, TlVec2 point);
/* Угол узла — в точку: меняются отступы у узла обоих боков, сходящихся в нём;
   дальние углы тел остаются на месте. */
void tl_project_set_node_corner(TlProject *project, const TlNode *node, size_t corner, TlVec2 point);
/* ---- примыкания (см. tl_project_tees.c) ---------------------------------- */

/* Конец branch упирается в бок host посередине; host цела. */
typedef struct TlTee {
    TlPartition *branch;
    TlPartitionEnd end;
    TlPartition *host;
    int side;     /* бок host: +1 левый, −1 правый */
    TlVec2 point; /* точка примыкания — конец branch на боку host */
} TlTee;

TL_ARRAY_DEFINE(TlTeeList, TlTee, tl_tee_list)

TlTeeList tl_project_tees(TlProject *project);
bool tl_project_tee_of(TlProject *project, const TlPartition *branch, TlPartitionEnd end, TlTee *tee);
/* Точку примыкания — к target. Пока target не дальше граней host, конец branch
   скользит по её боку; дальше — host делится в этом месте, и получается узел из
   трёх в точке target. true — узел появился. */
bool tl_project_move_tee(TlProject *project, const TlTee *tee, TlVec2 target);
/* Узел, где две перегородки на одной прямой и тело не ломается, — снова одна
   перегородка; остальные концы узла упираются в её бок. Вызывать при фиксации правки. */
void tl_project_merge_straight_nodes(TlProject *project);
/* Убрать узел: самая прямая пара его перегородок выпрямляется в одну, остальные
   упираются в её бок. */
bool tl_project_dissolve_node(TlProject *project, const TlNode *node);

/* Тело перегородки. Торец в узле идёт по углам узла; свободный торец срезан
   точно по грани, в которую упирается, — внутренней стороне стены дома или
   боку другой перегородки. */
TlPolygon tl_project_partition_body(TlProject *project, const TlPartition *partition, double extension);
/* Тела всех перегородок и середины узлов. */
TlPolygonList tl_project_partition_bodies(TlProject *project, double extension);
/* Силуэт перегородок для рисования: тела, середины узлов и заплатки углов,
   все против часовой стрелки. */
TlPolygonList tl_project_partition_silhouette(TlProject *project);

/* ---- грани ---------------------------------------------------------------- */

TlFaceList tl_project_faces(TlProject *project);
/* Текущее положение грани: точка и направление. */
bool tl_project_face_line(TlProject *project, TlHost host, int side, TlVec2 *point,
                          TlVec2 *direction);

/* ---- правка с привязками ------------------------------------------------- */

TlEditToken *tl_project_begin_edit(TlProject *project, TlOutline *moving_outline);
/* Применяет привязки к текущей геометрии; token остаётся (перетаскивание
 * мышью применяет одну правку на каждом движении). options может быть NULL. */
void tl_project_apply_edit(TlProject *project, TlEditToken *token, const TlEditOptions *options);
void tl_edit_token_free(TlEditToken *token);
/* apply + free. */
void tl_project_end_edit(TlProject *project, TlEditToken *token, const TlEditOptions *options);

/* ---- проёмы -------------------------------------------------------------- */

bool tl_project_opening_host(TlProject *project, const TlOpening *opening, TlBand *band);
/* Вырез проёма шириной width с центром along: часть тела стены между краями
 * проёма. У перегородки — по её точной форме (трапеция, торцы по граням), у
 * наружной стены — на всю толщину вместе с примыкающей стеной соседнего дома. */
TlPolygon tl_project_opening_shape(TlProject *project, const TlBand *band, double along, double width);
bool tl_project_opening_cut(TlProject *project, const TlOpening *opening, TlPolygon *cut);
/* Пол под проёмом. Поперёк проёма — сечение от грани 0 до грани 1: у
 * перегородки — от правого бока к левому; у основной стены — от внутренней
 * грани её дома к дальней (у стены между домами — к внутренней грани соседнего
 * дома). Стык делит сечение на доле threshold; пол комнаты по каждую сторону —
 * до стыка. За наружной стеной улица: там пол только от внутренней грани до
 * стыка, а стык на грани 0 — пола под проёмом нет. */

/* Пол под проёмом между косяками — обеих сторон вместе (false — пола нет). */
bool tl_project_opening_doorway(TlProject *project, const TlOpening *opening, TlPolygon *doorway);
/* Часть пола под проёмом основной стены, которая принадлежит дому house (с
 * небольшим заходом в пол дома, чтобы сомкнуться с ним). Не стена дома и не
 * соседняя с ней — false; у перегородки — тоже false: её проём внутри дома. */
bool tl_project_opening_wall_floor(TlProject *project, const TlOpening *opening, const TlOutline *house,
                                   TlPolygon *floor);
/* Линия стыка — от косяка до косяка, на доле threshold сечения. */
bool tl_project_opening_threshold(TlProject *project, const TlOpening *opening, TlVec2 *from, TlVec2 *to);
/* Косяки проёма в перегородке: края по обе стороны стыка (from[i]—to[i] поперёк;
   i = 0 — у начала стыка, 1 — у конца). false — проём не в стене. */
bool tl_project_opening_jambs(TlProject *project, const TlOpening *opening, TlVec2 from[2], TlVec2 to[2]);
/* Сечение посередине проёма: от грани 0 (*right) к грани 1 (*left). */
bool tl_project_opening_section(TlProject *project, const TlOpening *opening, TlVec2 *right, TlVec2 *left);
/* Проём в наружной стене: за гранью 1 — улица. */
bool tl_project_opening_outside(TlProject *project, const TlOpening *opening);
/* Стык нового проёма: в наружной стене — на грани 0 (пола нет), иначе посередине. */
double tl_project_default_threshold(TlProject *project, const TlOpening *opening);
void tl_project_remove_orphan_openings(TlProject *project);
/* Самый длинный участок стены или перегородки, не закрытый проёмами (и
 * проёмами соседнего дома, проходящими сквозь неё): вдоль от *from до *to. */
void tl_project_solid_stretch(TlProject *project, const TlBand *band, double *from, double *to);

/* ---- разбивка граней для размеров --------------------------------------- */

/* Точки вдоль оси band, где грань прерывается: её концы (у перегородки — с
 * прямым продолжением через узлы), углы тел примкнувших перегородок, края
 * проёмов (кроме exclude). side: у перегородки +1 — левый бок, −1 — правый; у
 * стены — внутренняя грань. По возрастанию. */
TlDoubleArray tl_project_face_breaks(TlProject *project, const TlBand *band, int side, const TlOpening *exclude);
/* Где вдоль полосы проёма кончается его стена: у перегородки — дальний из углов
   тел обоих боков (с прямым продолжением через узлы), у основной стены — её
   внутренняя грань. Для «от начала стены» / «до конца стены». */
bool tl_project_opening_span(TlProject *project, const TlOpening *opening, double *start, double *end);
/* Ближайшие к проёму [from, to] точки разбивки грани: до него и после. */
bool tl_project_opening_gaps(TlProject *project, const TlBand *band, int side, const TlOpening *exclude,
                             double from, double to, double *before, double *after);

#endif

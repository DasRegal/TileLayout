/*
 * Перетаскивание предметов плана — общее для программы и сайта:
 *   угол дома — с привязкой к углам и выравниванием по соседним;
 *   стена — параллельно себе (со смежной стеной соседнего дома, link_walls);
 *   дом целиком — с перегородками и проёмами;
 *   перегородка за тело — только поперёк себя; за конец — вдоль своей оси, а
 *     если увести в сторону — поворачивается (с выравниванием по 0°/90°); за
 *     угол тела — угол сдвигается поперёк оси, меняя форму;
 *   узел перегородок за середину — целиком, за угол — углы тел у узла;
 *   примыкание — вдоль бока перегородки, а за грань — становится узлом;
 *   проём — вдоль своей стены; стык полов в нём — поперёк стены, за конец у
 *     косяка — только этот конец (стык наискосок);
 *   линейка и деформационный шов за линию — целиком, за конец — с притягиванием
 *     к стенам и выравниванием по другому концу; концы других швов и линий
 *     раздела, лежащие на тянутой линии, держатся за неё (сочленения);
 *   труба — целиком, по сетке;
 *   панно на полу — целиком, в осях раскладки комнаты, по сетке;
 *   предмет (ванна, тумба…) — целиком; у стены встаёт к ней спинкой;
 *   трап — по сетке; линейный — у стены вдоль неё;
 *   сетка плитки комнаты — сдвиг раскладки (целыми мм).
 *
 * Вызывающий решает, когда начинается перетаскивание (порог в пикселях) и
 * запоминает проект для отмены до первого tl_drag_move.
 */
#ifndef TL_DRAG_H
#define TL_DRAG_H

#include "tl_measure_snap.h"
#include "tl_snap.h"

typedef enum TlDragKind {
    TL_DRAG_NONE,
    TL_DRAG_VERTEX,    /* outline, index — угол */
    TL_DRAG_WALL,      /* outline, index — стена */
    TL_DRAG_HOUSE,     /* outline */
    TL_DRAG_PARTITION, /* partition, index — см. TL_DRAG_PARTITION_* */
    TL_DRAG_NODE,      /* partition, end — конец в узле; index 0 — середина, 1… — угол узла */
    TL_DRAG_TEE,       /* partition, end — конец на боку другой перегородки */
    TL_DRAG_OPENING,   /* opening */
    TL_DRAG_THRESHOLD, /* opening, index — см. TL_DRAG_THRESHOLD_* */
    TL_DRAG_RULER,     /* ruler, index — 0 линия, 1 начало, 2 конец */
    TL_DRAG_JOINT,     /* joint (деформационный шов), index — как у линейки */
    TL_DRAG_TILES,     /* space — сетка плитки (у общего пола — комнаты-источника) */
    TL_DRAG_PIPE,      /* pipe — отверстие под трубу */
    TL_DRAG_PANNO,     /* space, index — панно на полу (space->pannos) */
    TL_DRAG_FIXTURE,   /* index — предмет (project->fixtures) */
    TL_DRAG_DRAIN      /* index — трап (project->drains) */
} TlDragKind;

#define TL_DRAG_PARTITION_BODY 0
#define TL_DRAG_PARTITION_START 1
#define TL_DRAG_PARTITION_END 2
#define TL_DRAG_PARTITION_FIRST_CORNER 3 /* + TlPartitionCorner — угол тела */

#define TL_DRAG_THRESHOLD_LINE 1 /* стык целиком */
#define TL_DRAG_THRESHOLD_FROM 2 /* конец у косяка в начале проёма */
#define TL_DRAG_THRESHOLD_TO 3   /* у косяка в конце */

#define TL_DRAG_MAX_FOLLOWERS 32

typedef struct TlDragTarget {
    TlDragKind kind;
    TlOutline *outline;
    TlPartition *partition;
    TlPartitionEnd end;
    TlOpening *opening;
    TlRuler *ruler;
    TlExpansionJoint *joint;
    TlPipe *pipe;
    TlSpace *space;
    int index;
    bool link_walls;
} TlDragTarget;

typedef struct TlDrag {
    TlDragTarget target;           /* примыкание, ушедшее за грань, становится узлом */
    TlVec2 start;                  /* где взяли, мм */
    TlPolygon original_corners;    /* дом: углы */
    TlWallRefList links;           /* стена: смежные стены соседних домов */
    TlPolygonList links_original;
    TlVec2 original_start, original_end; /* перегородка */
    double original_corner_offset;       /* угол тела */
    TlVec2 original_point, original_node; /* узел и примыкание */
    TlPartition *frozen;           /* узел из примыкания: эта перегородка не возвращается на бок */
    TlBand band;                   /* проём */
    double original_along;
    double original_shares[2];     /* стык: доли у косяков в начале и в конце проёма */
    double original_share;         /* стык: доля под курсором, когда взяли */
    TlSnapPoint measure;           /* линейка и шов: к чему притянулся конец */
    TlVec2 *followers[TL_DRAG_MAX_FOLLOWERS]; /* шов: концы других линий на нём */
    double follower_at[TL_DRAG_MAX_FOLLOWERS]; /* где на линии (0 — начало, 1 — конец) */
    size_t follower_count;
    double original_offset_x, original_offset_y; /* сетка плитки */
    struct TlMeasureAnchors *measures;   /* проём: концы линеек на его краях едут с ним */
    bool geometry_changed;
    TlEditToken *token;
} TlDrag;

/* Узел: 0 — середина, 1… — его углы. */
TlVec2 tl_node_point(const TlNode *node, int index);

/* false — тянуть нельзя (заблокировано или предмета нет); *drag не занят. */
bool tl_drag_begin(TlDrag *drag, TlProject *project, TlDragTarget target, TlVec2 start);
/* Курсор в world; snap — привязки (tolerance, сетка, выравнивание), free — без
   них (Shift). guides — линии выравнивания для показа (может быть NULL). */
void tl_drag_move(TlDrag *drag, TlProject *project, TlVec2 world, const TlSnapContext *snap, bool free,
                  TlGuides *guides);
/* Отпустили: узел, вернувшийся на прямую, снова одна перегородка; комнаты пересчитаны. */
void tl_drag_end(TlDrag *drag, TlProject *project);
void tl_drag_free(TlDrag *drag);

#endif

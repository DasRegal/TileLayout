/*
 * Проём: центр лежит в теле стены или перегородки, ширина — вдоль неё.
 * В проёме перегородки — пол; его делит между комнатами стык, параллельный
 * полотну двери: threshold — доля толщины от правого бока перегородки (0) к
 * левому (1) у косяка в начале проёма (по ходу стены), threshold_end — у косяка
 * в конце; разные доли — стык наискосок. Шов threshold_gap — зазор между плиткой комнат по линии стыка,
 * поровну в обе стороны; отступ плитки от стен на этом краю пола не действует.
 *
 * Проём — дверь, окно или арка. Под окном пола нет (стык и пол проёма — только
 * у двери и арки); по высоте проём — от sill (у окна — подоконник) до sill + height:
 * так он вырезается из развёртки стены.
 */
#ifndef TL_OPENING_H
#define TL_OPENING_H

#include "tl_ruler.h"

typedef enum TlOpeningKind { TL_OPENING_DOOR, TL_OPENING_WINDOW, TL_OPENING_ARCH } TlOpeningKind;

typedef struct TlOpening {
    char name[TL_NAME_LENGTH]; /* короткое имя («Д1», «О1») — для выбора двери в раскладке от двери */
    TlOpeningKind kind;
    double height;             /* высота проёма, мм */
    double sill;               /* от пола до низа проёма (подоконник), мм; у двери и арки — 0 */
    double reveal;             /* откосы плиткой: глубина откоса, мм; 0 — без откосов */
    TlVec2 center;
    double width;
    double threshold, threshold_end;
    double threshold_gap; /* мм */
    bool locked;
} TlOpening;

#define TL_OPENING_DEFAULT_THRESHOLD 0.5     /* стык посередине толщины */
#define TL_OPENING_DEFAULT_THRESHOLD_GAP 0.0 /* плитка комнат встык */
#define TL_DOOR_HEIGHT 2100.0
#define TL_WINDOW_HEIGHT 1400.0
#define TL_WINDOW_SILL 900.0

TlOpening *tl_opening_new(TlVec2 center, double width);
TlOpening *tl_opening_clone(const TlOpening *opening);
/* Стык параллельно полотну: одна доля у обоих косяков. */
void tl_opening_set_threshold(TlOpening *opening, double share);
void tl_opening_free(TlOpening *opening);
/* Сменить вид: высота и подоконник — обычные для вида. */
void tl_opening_set_kind(TlOpening *opening, TlOpeningKind kind);
/* Есть пол под проёмом (дверь, арка) — не окно. */
bool tl_opening_has_floor(const TlOpening *opening);
const char *tl_opening_kind_name(TlOpeningKind kind); /* «Дверь», «Окно», «Арка» */

#endif

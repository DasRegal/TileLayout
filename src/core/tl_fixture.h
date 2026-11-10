/*
 * Предмет в комнате: ванна, душевой поддон, инсталляция, тумба, кухня, лестница — прямоугольник
 * width × depth на плане (центр center, поворот angle; «спинка» — сторона −depth/2 по оси Y
 * предмета) высотой height.
 *
 * Плитку под предметом (under) и за ним на стенах (behind) кладут или нет — по выбору: без
 * плитки под предметом раскладки пола обходят его; без плитки за ним стена, к которой он
 * прижат, на развёртке не облицовывается от пола до его высоты (как проём).
 *
 * Лестница: steps ступеней, подъём — от переда к спинке; проступь — depth / steps, подступенок —
 * height / steps. Её плитка (проступи и подступенки, по плитке комнаты) — на развёртке комнаты.
 */
#ifndef TL_FIXTURE_H
#define TL_FIXTURE_H

#include "tl_geometry.h"

typedef enum TlFixtureKind {
    TL_FIXTURE_BATH,
    TL_FIXTURE_SHOWER_TRAY,
    TL_FIXTURE_INSTALLATION,
    TL_FIXTURE_CABINET,
    TL_FIXTURE_KITCHEN,
    TL_FIXTURE_STAIRS,
    TL_FIXTURE_KIND_COUNT
} TlFixtureKind;

typedef struct TlFixture {
    TlFixtureKind kind;
    TlVec2 center;
    double width, depth, height; /* мм */
    double angle;                /* поворот, ° */
    bool under, behind;          /* плитка под предметом и за ним на стенах */
    int steps;                   /* у лестницы — ступеней; иначе 0 */
} TlFixture;

TL_ARRAY_DEFINE(TlFixtureList, TlFixture, tl_fixture_list)

/* Предмет вида kind с обычными размерами и выбором плитки. */
TlFixture tl_fixture_default(TlFixtureKind kind, TlVec2 center);
const char *tl_fixture_kind_name(TlFixtureKind kind); /* «Ванна», «Душевой поддон»… */
const char *tl_fixture_kind_key(TlFixtureKind kind);  /* "bath", "tray"… — в файле проекта */
TlFixtureKind tl_fixture_kind_from_key(const char *key);
/* Контур на плане (4 угла: спинка слева направо, затем перед). */
TlPolygon tl_fixture_outline(const TlFixture *fixture);
/* Поставить спинкой к ближайшей стороне контура walls не дальше reach: поворот — по стороне,
   спинка — вплотную. false — стен рядом нет (предмет не трогается). */
bool tl_fixture_snap_to_walls(TlFixture *fixture, const TlPolygon *walls, double reach);

#endif

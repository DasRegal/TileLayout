#include "spec.h"
#include "specs.h"

#include <string.h>

#include "tl_report.h"

/* Демо-проект и спальня за перегородкой с общей раскладкой — как в Python-версии. */
static TlProject *demo_with_bedroom(void)
{
    TlProject *project = tl_project_demo();
    tl_project_add_partition(project, tl_partition_new(tl_vec2(2000, 0), tl_vec2(2000, 3600), 100));
    tl_project_update_spaces(project);
    TlSpace *living = project->spaces.items[0];
    TlSpace *bedroom = project->spaces.items[2];
    tl_space_set_name(bedroom, "Спальня");
    bedroom->source_id = living->id;
    tl_project_update_spaces(project);
    return project;
}

static const TlTileTypeSummary *type_with_width(const TlSummary *summary, double width)
{
    for (size_t i = 0; i < summary->types.count; i++)
        if (summary->types.items[i].tile->tile_width == width)
            return &summary->types.items[i];
    return NULL;
}

/* Отдельная комната (свой дом) шириной width и высотой height, левый нижний угол в (x, 0). */
static TlSpace *add_room(TlProject *project, double x, double width, double height, const char *name)
{
    const TlVec2 corners[] = {{x, 0}, {x + width, 0}, {x + width, height}, {x, height}};
    tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
    tl_project_update_spaces(project);
    TlSpace *space = tl_project_space_at(project, tl_vec2(x + width / 2, height / 2));
    tl_space_set_name(space, name);
    return space;
}

/* Две отдельные комнаты с плиткой 600×300: в одной подрезка 400×300, в другой 190×300. */
static TlProject *two_rooms_with_strips(void)
{
    TlProject *project = tl_project_new();
    add_room(project, 0, 400, 300, "Кухня");
    add_room(project, 3000, 190, 300, "Кладовая");
    return project;
}

void spec_summary(void)
{
    DESCRIBE("Сводный расчёт")
    {
        /* Python-версия без пола под дверью давала 100 плиток, ванную 56, клей 125,51 кг и 461 СВП;
           пол дверного проёма добавляет 1 плитку 600×300, 2 полоски в ванной и 5 СВП. Угловые
           куски (обрезаны с двух сторон) тоже идут в пары — ещё 1 плитка 600×300 и 2 в ванной. */
        IT("расчёт демо-плана: 100 плиток 600×300 (−6 на стыке, −10 из обрезков), ванная 56")
        {
            TlProject *project = demo_with_bedroom();
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 2);
            const TlTileTypeSummary *main_tile = type_with_width(&summary, 600);
            const TlTileTypeSummary *bath_tile = type_with_width(&summary, 300);
            EXPECT(main_tile && bath_tile);
            if (main_tile && bath_tile) {
                EXPECT_INT(main_tile->count, 100);
                EXPECT_INT(main_tile->shared_saving, 6);
                EXPECT_INT(main_tile->offcut_saving, 10);
                EXPECT_INT(main_tile->to_buy, 110);
                EXPECT_INT(main_tile->spaces.count, 2);
                EXPECT_INT(bath_tile->count, 56);
                EXPECT_INT(bath_tile->to_buy, 62);
            }
            EXPECT_NEAR(summary.glue_total_kg, 125.95, 0.01);
            EXPECT_INT(summary.glue.count, 1);
            EXPECT_INT(summary.glue.items[0].units, 6);
            EXPECT_INT(summary.clip_total, 466);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("комнаты без плитки не входят в площадь под плитку")
        {
            TlProject *project = tl_project_demo();
            project->spaces.items[1]->tile.enabled = false;
            TlSummary summary = tl_report_summarize(project);
            EXPECT_NEAR(summary.floor_area / 1e6, 22.579, 0.01);
            EXPECT_NEAR(summary.tiled_area / 1e6, 17.670, 0.01);
            EXPECT_INT(summary.types.count, 1);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("остатки подрезок: полосы 200 и 196 мм режутся из одной плитки 600")
        {
            TlProject *project = tl_project_new();
            const TlVec2 corners[] = {{0, 0}, {1000, 0}, {1000, 602}, {0, 602}};
            tl_project_add_outline(project, tl_outline_new(corners, 4, 200));
            tl_project_update_spaces(project);
            TlSpace *room = project->spaces.items[0];
            room->tile.offset_x = 202; /* слева полоса 200, справа 196 */
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.items[0].count, 4);
            EXPECT_INT(summary.types.items[0].offcut_saving, 2);
            tl_summary_free(&summary);
            room->tile.reuse_offcuts = false;
            summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.items[0].count, 6);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
    }

    DESCRIBE("Обрезки и запас по всем комнатам")
    {
        IT("остаток подрезки одной комнаты идёт в подрезку другой с той же плиткой")
        {
            TlProject *project = two_rooms_with_strips();
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 1);
            EXPECT_INT(summary.types.items[0].count, 1); /* 400 + 190 + рез 3 ≤ 600 */
            EXPECT_INT(summary.types.items[0].offcut_saving, 1);
            EXPECT_INT(summary.offcut_pairs.count, 1);
            if (summary.offcut_pairs.count == 1)
                EXPECT(summary.offcut_pairs.items[0].first.space != summary.offcut_pairs.items[0].second.space);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("плитка 300×600 в другой комнате — та же плитка: подрезки подбираются с поворотом")
        {
            TlProject *project = tl_project_new();
            add_room(project, 0, 400, 300, "Кухня");
            TlSpace *pantry = add_room(project, 3000, 300, 190, "Кладовая");
            pantry->tile.tile_width = 300;
            pantry->tile.tile_height = 600; /* кусок 300×190 — это полоса 190 поперёк плитки */
            tl_project_update_spaces(project);
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.count, 1);
            EXPECT_INT(summary.types.items[0].count, 1);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("комната без «использовать обрезки» своих подрезок не отдаёт и чужих не берёт")
        {
            TlProject *project = two_rooms_with_strips();
            tl_project_space_at(project, tl_vec2(3095, 150))->tile.reuse_offcuts = false;
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.items[0].count, 2);
            EXPECT_INT(summary.types.items[0].offcut_saving, 0);
            tl_summary_free(&summary);
            tl_project_free(project);
        }
        IT("запас округляется один раз на вид плитки, а не в каждой комнате")
        {
            TlProject *project = tl_project_new();
            add_room(project, 0, 600, 300, "Тамбур");   /* по одной целой плитке */
            add_room(project, 3000, 600, 300, "Ниша");
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.types.items[0].count, 2);
            EXPECT_INT(summary.types.items[0].to_buy, 3); /* 2 × 1,1 = 2,2 → 3, а не 2 + 2 */
            tl_summary_free(&summary);
            tl_project_free(project);
        }
    }

    DESCRIBE("Нумерация плиток")
    {
        IT("сквозная: плитка на стыке комнат с общим полом имеет один номер")
        {
            TlProject *project = demo_with_bedroom();
            tl_report_assign_numbers(project);
            TlLayout *living = tl_space_layout(project->spaces.items[0]);
            TlLayout *bedroom = tl_space_layout(project->spaces.items[2]);
            int shared = 0, same_number = 0, max_number = 0;
            for (size_t i = 0; i < living->tiles.count; i++) {
                TlTile *twin = tl_layout_tile_at(bedroom, living->tiles.items[i].cell);
                if (twin) {
                    shared++;
                    same_number += twin->number == living->tiles.items[i].number;
                }
                if (living->tiles.items[i].number > max_number)
                    max_number = living->tiles.items[i].number;
            }
            EXPECT_INT(shared, 6);
            EXPECT_INT(same_number, 6);
            EXPECT_INT(max_number, 110);
            TlLayout *bath = tl_space_layout(project->spaces.items[1]);
            EXPECT_INT(bath->tiles.items[0].number, 111);
            tl_project_free(project);
        }
    }

    DESCRIBE("Чертежи подрезок")
    {
        IT("плитка на две комнаты и пара обрезков — каждая на одной карточке")
        {
            TlProject *project = demo_with_bedroom();
            TlCutCardList cards = tl_report_cut_cards(project);
            int shared_cards = 0, pair_cards = 0;
            for (size_t i = 0; i < cards.count; i++) {
                if (cards.items[i].kind == TL_CARD_SHARED_TILE)
                    shared_cards++;
                if (cards.items[i].kind == TL_CARD_OFFCUT_PAIR)
                    pair_cards++;
                EXPECT(cards.items[i].parts.count >= 1);
            }
            EXPECT_INT(shared_cards, 6);
            EXPECT_INT(pair_cards, 13); /* 10 пар основной плитки и 3 — плитки ванной */
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
    }

    DESCRIBE("Чертежи подрезок из разных комнат")
    {
        IT("пара обрезков из двух комнат — одна карточка с обеими комнатами")
        {
            TlProject *project = tl_project_new();
            add_room(project, 0, 400, 300, "Кухня");
            TlSpace *pantry = add_room(project, 3000, 300, 190, "Кладовая");
            pantry->tile.tile_width = 300;
            pantry->tile.tile_height = 600;
            tl_project_update_spaces(project);
            TlCutCardList cards = tl_report_cut_cards(project);
            EXPECT_INT(cards.count, 1);
            if (cards.count == 1) {
                const TlCutCard *card = &cards.items[0];
                EXPECT_INT(card->kind, TL_CARD_OFFCUT_PAIR);
                EXPECT(strstr(card->title, "Кухня") && strstr(card->title, "Кладовая"));
                EXPECT_INT(card->parts.count, 2);
                double widths = 0; /* куски в осях плитки 600×300: 400 и повёрнутый 190 */
                for (size_t p = 0; p < card->parts.count; p++)
                    widths += tl_rect_width(tl_polygon_bounds(&card->parts.items[p].shapes.items[0]));
                EXPECT_NEAR(widths, 590, 1e-6);
            }
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
    }

    DESCRIBE("Угловые куски из одной плитки")
    {
        IT("два угловых куска — пара: каждый в своём углу плитки, между ними — запас на рез, не внахлёст")
        {
            TlProject *project = tl_project_new();
            add_room(project, 0, 400, 200, "Кухня");      /* кусок 400×200 — обрезан с двух сторон */
            add_room(project, 3000, 150, 250, "Кладовая"); /* 150×250 */
            TlSummary summary = tl_report_summarize(project);
            EXPECT_INT(summary.offcut_pairs.count, 1); /* 400 + 150 + 3 ≤ 600 — бок о бок */
            if (summary.types.count == 1)
                EXPECT_INT(summary.types.items[0].count, 1);
            tl_summary_free(&summary);
            TlCutCardList cards = tl_report_cut_cards(project);
            EXPECT_INT(cards.count, 1);
            if (cards.count == 1 && cards.items[0].parts.count == 2) {
                TlRect a = tl_polygon_bounds(&cards.items[0].parts.items[0].shapes.items[0]);
                TlRect b = tl_polygon_bounds(&cards.items[0].parts.items[1].shapes.items[0]);
                if (a.left > b.left) {
                    TlRect swap = a;
                    a = b;
                    b = swap;
                }
                EXPECT(b.left - a.right >= 3 - 1e-9); /* не внахлёст, запас на рез */
                EXPECT(a.left >= 0 && b.right <= 600 + 1e-9 && a.bottom <= 300 + 1e-9 && b.bottom <= 300 + 1e-9);
                EXPECT_NEAR(tl_rect_height(a) + tl_rect_height(b), 200 + 250, 1e-9); /* настоящие высоты кусков */
            }
            tl_cut_card_list_free_all(&cards);
            tl_project_free(project);
        }
    }

    DESCRIBE("Подбор «Минимум плиток»")
    {
        IT("из неудачного смещения находит раскладку с меньшим числом плиток")
        {
            TlProject *project = demo_with_bedroom();
            TlSpace *living = project->spaces.items[0];
            living->tile.offset_x = 37;
            living->tile.offset_y = 151;
            TlTileCountResult result;
            EXPECT(tl_report_optimize_count(project, living, &result));
            EXPECT(result.after.tiles < result.before.tiles);
            EXPECT_INT(result.after.tiles, 100);
            tl_project_free(project);
        }
    }
}

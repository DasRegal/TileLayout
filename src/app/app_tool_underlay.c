/*
 * Инструмент «Подложка» (включается кнопками раздела «Подложка» на панели):
 * калибровка — два щелчка по точкам подложки, расстояние между которыми известно, и само расстояние
 * в окошке; сдвиг — подложку тянут мышью, чтобы совместить с уже нарисованными стенами.
 */
#include "app_canvas_private.h"

#include <math.h>

#include "app_draw.h"
#include "app_render.h"
#include "app_widgets.h"

/* Настоящее расстояние между точками калибровки; 0 — отказались. */
static double ask_distance(AppCanvas *canvas, double measured)
{
    GtkWidget *parent = gtk_widget_get_toplevel(canvas->area);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Калибровка подложки", GTK_WINDOW(parent),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Отмена",
                                                    GTK_RESPONSE_CANCEL, "Применить", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);
    GtkWidget *label = gtk_label_new("Сколько на самом деле между отмеченными точками?\n"
                                     "Например, длина стены по размеру на плане.");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    GtkWidget *spin = app_unit_spin_new(1, 1000000, 10, 0, "мм");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), round(measured));
    gtk_entry_set_activates_default(GTK_ENTRY(spin), TRUE);
    gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(spin);
    double distance = 0;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
        distance = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
    }
    gtk_widget_destroy(dialog);
    return distance;
}

/* Точка калибровки — с притягиванием к углам DXF-подложки (и стенам), Shift — свободно. */
static TlVec2 calibration_point(AppCanvas *canvas, const AppPointer *pointer)
{
    TlSnapContext context = app_canvas_snap_context(canvas);
    context.grid_step = 0;
    context.align = false;
    return tl_snap_wall_point(&context, pointer->world, NULL, pointer->shift).point;
}

static void activate(AppCanvas *canvas)
{
    canvas->underlay_points = 0;
    canvas->underlay_dragging = false;
    app_document_status(canvas->document, "%s",
                        canvas->underlay_move ? "Тяните подложку мышью, чтобы совместить её со стенами. Esc — готово."
                                              : "Щёлкните две точки подложки, расстояние между которыми знаете. Esc — отмена.");
}

static void press(AppCanvas *canvas, const AppPointer *pointer)
{
    AppDocument *document = canvas->document;
    TlUnderlay *underlay = &document->project->underlay;
    if (pointer->button != 1 || !tl_underlay_present(underlay))
        return;
    if (canvas->underlay_move) {
        app_document_checkpoint(document, "");
        canvas->underlay_dragging = true;
        canvas->underlay_start = pointer->world;
        canvas->underlay_origin = underlay->origin;
        return;
    }
    TlVec2 point = calibration_point(canvas, pointer);
    if (canvas->underlay_points == 0) {
        canvas->underlay_a = point;
        canvas->underlay_points = 1;
        return;
    }
    TlVec2 a = canvas->underlay_a;
    canvas->underlay_points = 0;
    double distance = ask_distance(canvas, tl_distance(a, point));
    if (distance > 0 && tl_distance(a, point) > 1e-6) {
        app_document_checkpoint(document, "");
        tl_underlay_calibrate(underlay, a, point, distance);
        app_document_commit(document);
        app_document_status(document, "Подложка откалибрована: %.0f мм между точками.", distance);
    }
    app_canvas_set_tool(canvas, APP_TOOL_SELECT);
}

static void motion(AppCanvas *canvas, const AppPointer *pointer)
{
    TlUnderlay *underlay = &canvas->document->project->underlay;
    if (canvas->underlay_dragging) {
        underlay->origin = tl_vec2(canvas->underlay_origin.x + pointer->world.x - canvas->underlay_start.x,
                                   canvas->underlay_origin.y + pointer->world.y - canvas->underlay_start.y);
        app_document_notify(canvas->document, APP_CHANGED_MODEL);
    }
    canvas->cursor = canvas->underlay_move ? pointer->world : calibration_point(canvas, pointer);
    app_canvas_redraw(canvas);
}

static void release(AppCanvas *canvas, const AppPointer *pointer)
{
    if (!canvas->underlay_dragging)
        return;
    canvas->underlay_dragging = false;
    app_document_commit(canvas->document);
}

static bool cancel(AppCanvas *canvas)
{
    if (canvas->underlay_points == 0)
        return false;
    canvas->underlay_points = 0;
    return true;
}

static void draw(AppCanvas *canvas, cairo_t *cr)
{
    if (canvas->underlay_move)
        return;
    app_canvas_draw_cross(canvas, cr, canvas->cursor);
    if (canvas->underlay_points == 1) {
        app_canvas_draw_cross(canvas, cr, canvas->underlay_a);
        app_canvas_draw_dimension(canvas, cr, canvas->underlay_a, canvas->cursor, true, NULL);
    }
}

const AppTool APP_UNDERLAY_TOOL = {
    .cursor = "crosshair",
    .activate = activate,
    .press = press,
    .motion = motion,
    .release = release,
    .cancel = cancel,
    .draw = draw,
};

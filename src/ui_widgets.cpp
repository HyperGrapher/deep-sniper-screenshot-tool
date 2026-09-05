#include "ui_widgets.hpp"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace {

void drawIcon(ButtonIcon icon, int x, int y) {
    fl_line_style(FL_SOLID | FL_CAP_ROUND | FL_JOIN_ROUND, 2);
    switch (icon) {
    case ButtonIcon::Save:
        fl_line(x + 12, y + 2, x + 12, y + 15);
        fl_line(x + 7, y + 10, x + 12, y + 15, x + 17, y + 10);
        fl_line(x + 3, y + 16, x + 3, y + 21, x + 21, y + 21);
        fl_line(x + 21, y + 21, x + 21, y + 16);
        break;
    case ButtonIcon::SaveAs:
    case ButtonIcon::Folder:
        fl_line(x + 2, y + 20, x + 2, y + 5, x + 9, y + 5);
        fl_line(x + 9, y + 5, x + 12, y + 8, x + 22, y + 8);
        fl_line(x + 22, y + 8, x + 22, y + 20, x + 2, y + 20);
        if (icon == ButtonIcon::SaveAs) {
            fl_line(x + 12, y + 11, x + 12, y + 17);
            fl_line(x + 9, y + 14, x + 15, y + 14);
        }
        break;
    case ButtonIcon::Copy:
        fl_rounded_rect(x + 8, y + 8, 13, 14, 2);
        fl_line(x + 5, y + 17, x + 2, y + 17, x + 2, y + 2);
        fl_line(x + 2, y + 2, x + 16, y + 2, x + 16, y + 5);
        break;
    case ButtonIcon::Close:
        fl_line(x + 5, y + 5, x + 19, y + 19);
        fl_line(x + 19, y + 5, x + 5, y + 19);
        break;
    case ButtonIcon::None:
        break;
    }
    fl_line_style(0);
}

}

ThemedButton::ThemedButton(int x, int y, int width, int height, const char* label, ButtonIcon icon)
    : Fl_Button(x, y, width, height, label), icon_(icon) {
    color(UiTheme::kControl);
    labelcolor(UiTheme::kText);
    labelfont(FL_HELVETICA_BOLD);
    labelsize(13);
    visible_focus(1);
}

int ThemedButton::handle(int event) {
    if (event == FL_ENTER || event == FL_LEAVE) {
        isHovered_ = event == FL_ENTER;
        redraw();
        return 1;
    }
    if (event == FL_FOCUS || event == FL_UNFOCUS) {
        redraw();
    }
    return Fl_Button::handle(event);
}

void ThemedButton::draw() {
    fl_push_clip(x(), y(), w(), h());
    const bool isAccent = color() == UiTheme::kAccent;
    Fl_Color background = color();
    if (value() || isHovered_) {
        background = isAccent ? fl_color_average(color(), FL_WHITE, 0.8F) : UiTheme::kHover;
    }
    fl_color(background);
    fl_rounded_rectf(x(), y(), w(), h(), 8);
    fl_color(Fl::focus() == this ? UiTheme::kAccent : UiTheme::kBorder);
    fl_rounded_rect(x(), y(), w() - 1, h() - 1, 8);
    if (icon_ == ButtonIcon::None) {
        draw_label();
    } else {
        fl_color(active_r() ? labelcolor() : UiTheme::kMuted);
        drawIcon(icon_, x() + (w() - 24) / 2, y() + (h() - 24) / 2);
    }
    fl_pop_clip();
}

void ThemedChoice::draw() {
    fl_push_clip(x(), y(), w(), h());
    fl_color(color());
    fl_rounded_rectf(x(), y(), w(), h(), 8);
    fl_color(Fl::focus() == this ? UiTheme::kAccent : UiTheme::kBorder);
    fl_rounded_rect(x(), y(), w() - 1, h() - 1, 8);
    fl_color(active_r() ? textcolor() : UiTheme::kMuted);
    fl_font(textfont(), textsize());
    if (mvalue() != nullptr) {
        fl_draw(mvalue()->label(), x() + 12, y(), w() - 44, h(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
    const int arrowX = x() + w() - 20;
    const int arrowY = y() + h() / 2;
    fl_line_style(FL_SOLID | FL_CAP_ROUND | FL_JOIN_ROUND, 2);
    fl_line(arrowX - 4, arrowY - 2, arrowX, arrowY + 2, arrowX + 4, arrowY - 2);
    fl_line_style(0);
    fl_pop_clip();
}

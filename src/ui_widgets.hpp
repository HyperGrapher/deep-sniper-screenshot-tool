#pragma once

#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>

namespace UiTheme {
inline const Fl_Color kBackground = fl_rgb_color(17, 19, 24);
inline const Fl_Color kPanel = fl_rgb_color(25, 29, 37);
inline const Fl_Color kControl = fl_rgb_color(34, 40, 50);
inline const Fl_Color kHover = fl_rgb_color(46, 55, 68);
inline const Fl_Color kBorder = fl_rgb_color(57, 66, 80);
inline const Fl_Color kText = fl_rgb_color(235, 239, 245);
inline const Fl_Color kMuted = fl_rgb_color(158, 170, 187);
inline const Fl_Color kAccent = fl_rgb_color(107, 224, 190);
inline const Fl_Color kDanger = fl_rgb_color(255, 139, 149);
}

enum class ButtonIcon { None, Save, SaveAs, Copy, Close, Folder };

class ThemedButton : public Fl_Button {
public:
    ThemedButton(int x, int y, int width, int height, const char* label,
                 ButtonIcon icon = ButtonIcon::None);
    int handle(int event) override;

protected:
    void draw() override;

private:
    ButtonIcon icon_;
    bool isHovered_{};
};

class ThemedChoice final : public Fl_Choice {
public:
    using Fl_Choice::Fl_Choice;

protected:
    void draw() override;
};

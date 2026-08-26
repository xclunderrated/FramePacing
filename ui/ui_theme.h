// Pacer UI Theme - Ultra-Premium Steam Minimalist Dark Theme
#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace ui {

// --- Ultra-Premium Steam Framepacer Palette ---
namespace colors {
    inline constexpr COLORREF kBgWindow       = RGB(15, 15, 17);     // #0F0F11 (Deep Zinc Charcoal)
    inline constexpr COLORREF kBgCard         = RGB(24, 24, 27);     // #18181B (Card / Container)
    inline constexpr COLORREF kBorderCard     = RGB(39, 39, 42);     // #27272A (Subtle 1px Border)
    inline constexpr COLORREF kGridLine       = RGB(32, 32, 36);     // #202024 (Vertical grid ticks)

    inline constexpr COLORREF kBtnNormal      = RGB(32, 32, 36);     // #202024 (Toolbar Buttons)
    inline constexpr COLORREF kBtnHover       = RGB(48, 48, 54);     // #303036
    inline constexpr COLORREF kBtnPressed     = RGB(63, 63, 70);     // #3F3F46
    inline constexpr COLORREF kBtnBorder      = RGB(45, 45, 50);     // #2D2D32

    inline constexpr COLORREF kTextPrimary    = RGB(250, 250, 250);  // #FAFAFA (Crisp White)
    inline constexpr COLORREF kTextStats      = RGB(56, 189, 248);   // #38BDF8 (Vibrant Neon Cyan)
    inline constexpr COLORREF kTextMuted      = RGB(161, 161, 170);  // #A1A1AA (Neutral Grey)

    inline constexpr COLORREF kGraphLine      = RGB(56, 189, 248);   // #38BDF8 (Neon Cyan Frametime line)
    inline constexpr COLORREF kGraphLineIdle  = RGB(113, 113, 122);  // #71717A (Neutral Idle line)
}

inline int scale_dpi(int val, UINT dpi) {
    if (dpi == 0) return val;
    return MulDiv(val, (int)dpi, 96);
}

// --- Font Manager ---
class FontManager {
public:
    static FontManager& instance();

    HFONT regular_font() const { return h_regular_; }
    HFONT bold_font() const { return h_bold_; }
    HFONT stats_font() const { return h_stats_; }
    HFONT num_font() const { return h_num_; }

    void init(UINT dpi = 96);
    void cleanup();

private:
    FontManager() = default;
    ~FontManager();

    HFONT h_regular_ = nullptr;
    HFONT h_bold_ = nullptr;
    HFONT h_stats_ = nullptr;
    HFONT h_num_ = nullptr;
    UINT current_dpi_ = 96;
};

// --- Theme Utility Functions ---
void init_theme(UINT dpi = 96);
void cleanup_theme();

void enable_dark_mode(HWND hwnd);

void draw_rounded_box(HDC dc, const RECT& rc, int radius,
                      COLORREF bg_col, COLORREF border_col, int border_width = 1);

void draw_icon_button(HDC dc, const RECT& rc, int icon_type,
                      bool is_hovered, bool is_active);

void draw_text_button(HDC dc, const RECT& rc, const wchar_t* text,
                      bool is_hovered, bool is_active, HFONT font);

}  // namespace ui

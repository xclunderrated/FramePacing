#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>

#include "ui_theme.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace ui {

static ULONG_PTR g_gdiplus_token = 0;

FontManager& FontManager::instance() {
    static FontManager s_inst;
    return s_inst;
}

void FontManager::init(UINT dpi) {
    if (h_regular_ && current_dpi_ == dpi) return;
    cleanup();
    current_dpi_ = (dpi == 0) ? 96 : dpi;

    const wchar_t* font_family = L"Segoe UI";

    int reg_h = scale_dpi(-13, current_dpi_);
    int bold_h = scale_dpi(-13, current_dpi_);
    int stats_h = scale_dpi(-12, current_dpi_);
    int num_h = scale_dpi(-15, current_dpi_);

    h_regular_ = CreateFontW(reg_h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_family);

    h_bold_ = CreateFontW(bold_h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_family);

    h_stats_ = CreateFontW(stats_h, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_family);

    h_num_ = CreateFontW(num_h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_family);
}

void FontManager::cleanup() {
    if (h_regular_) { DeleteObject(h_regular_); h_regular_ = nullptr; }
    if (h_bold_) { DeleteObject(h_bold_); h_bold_ = nullptr; }
    if (h_stats_) { DeleteObject(h_stats_); h_stats_ = nullptr; }
    if (h_num_) { DeleteObject(h_num_); h_num_ = nullptr; }
}

FontManager::~FontManager() {
    cleanup();
}

void init_theme(UINT dpi) {
    if (g_gdiplus_token == 0) {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        Gdiplus::GdiplusStartup(&g_gdiplus_token, &gdiplusStartupInput, nullptr);
    }
    FontManager::instance().init(dpi);
}

void cleanup_theme() {
    FontManager::instance().cleanup();
    if (g_gdiplus_token) {
        Gdiplus::GdiplusShutdown(g_gdiplus_token);
        g_gdiplus_token = 0;
    }
}

void enable_dark_mode(HWND hwnd) {
    BOOL darkMode = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode)))) {
        DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(darkMode));
    }

    // Windows 11 rounded window corners (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2)
    DWORD corner_pref = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner_pref, sizeof(corner_pref));

    // Dark title bar color (#0F0F11)
    COLORREF title_col = colors::kBgWindow;
    DwmSetWindowAttribute(hwnd, 35, &title_col, sizeof(title_col));
}

void draw_rounded_box(HDC dc, const RECT& rc, int radius,
                      COLORREF bg_col, COLORREF border_col, int border_width) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::RectF rectF((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                        (Gdiplus::REAL)(rc.right - rc.left - 1),
                        (Gdiplus::REAL)(rc.bottom - rc.top - 1));

    Gdiplus::GraphicsPath path;
    Gdiplus::REAL diameter = (Gdiplus::REAL)(radius * 2);
    path.AddArc(rectF.X, rectF.Y, diameter, diameter, 180, 90);
    path.AddArc(rectF.GetRight() - diameter, rectF.Y, diameter, diameter, 270, 90);
    path.AddArc(rectF.GetRight() - diameter, rectF.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(rectF.X, rectF.GetBottom() - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();

    Gdiplus::Color bg(GetRValue(bg_col), GetGValue(bg_col), GetBValue(bg_col));
    Gdiplus::SolidBrush bgBrush(bg);
    g.FillPath(&bgBrush, &path);

    if (border_width > 0) {
        Gdiplus::Color border(GetRValue(border_col), GetGValue(border_col), GetBValue(border_col));
        Gdiplus::Pen borderPen(border, (Gdiplus::REAL)border_width);
        g.DrawPath(&borderPen, &path);
    }
}

void draw_icon_button(HDC dc, const RECT& rc, int icon_type,
                      bool is_hovered, bool is_active) {
    COLORREF bg_col = is_active ? colors::kBtnPressed :
                      (is_hovered ? colors::kBtnHover : colors::kBtnNormal);
    draw_rounded_box(dc, rc, 6, bg_col, colors::kBtnBorder, 1);

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::Color iconCol(240, 240, 240);
    Gdiplus::Pen iconPen(iconCol, 1.8f);
    iconPen.SetStartCap(Gdiplus::LineCapRound);
    iconPen.SetEndCap(Gdiplus::LineCapRound);

    float cx = (rc.left + rc.right) / 2.0f;
    float cy = (rc.top + rc.bottom) / 2.0f;

    if (icon_type == 1) {
        // Hamburger Menu: 3 crisp horizontal lines
        float hw = 6.5f;
        g.DrawLine(&iconPen, cx - hw, cy - 4.5f, cx + hw, cy - 4.5f);
        g.DrawLine(&iconPen, cx - hw, cy, cx + hw, cy);
        g.DrawLine(&iconPen, cx - hw, cy + 4.5f, cx + hw, cy + 4.5f);
    } else if (icon_type == 2) {
        // App Grid: 4 squares (2x2)
        Gdiplus::SolidBrush iconBrush(iconCol);
        float sq = 4.5f;
        float gap = 2.0f;
        g.FillRectangle(&iconBrush, cx - sq - gap / 2.0f, cy - sq - gap / 2.0f, sq, sq);
        g.FillRectangle(&iconBrush, cx + gap / 2.0f, cy - sq - gap / 2.0f, sq, sq);
        g.FillRectangle(&iconBrush, cx - sq - gap / 2.0f, cy + gap / 2.0f, sq, sq);
        g.FillRectangle(&iconBrush, cx + gap / 2.0f, cy + gap / 2.0f, sq, sq);
    } else if (icon_type == 3) {
        // Pin Button
        Gdiplus::GraphicsPath pinPath;
        pinPath.AddEllipse(cx - 3.0f, cy - 5.0f, 6.0f, 6.0f);
        Gdiplus::SolidBrush pinBrush(is_active ? Gdiplus::Color(56, 189, 248) : iconCol);
        g.FillPath(&pinBrush, &pinPath);

        Gdiplus::Pen pinPen(is_active ? Gdiplus::Color(56, 189, 248) : iconCol, 1.8f);
        g.DrawLine(&pinPen, cx, cy + 1.0f, cx, cy + 6.0f);
    }
}

void draw_text_button(HDC dc, const RECT& rc, const wchar_t* text,
                      bool is_hovered, bool is_active, HFONT font) {
    COLORREF bg_col = is_active ? colors::kBtnPressed :
                      (is_hovered ? colors::kBtnHover : colors::kBtnNormal);
    draw_rounded_box(dc, rc, 6, bg_col, colors::kBtnBorder, 1);

    HFONT oldFont = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors::kTextPrimary);

    RECT tr = rc;
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
}

}  // namespace ui

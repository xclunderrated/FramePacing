#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "ui_graph.h"
#include "ui_theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ui {

extern pacer::ShmBox g_shm_box;

static FrametimeGraph g_graph_instance;

void FrametimeGraph::paint(HDC hdc, const RECT& client_rect, pacer::ShmBox& shm_box) {
    int W = client_rect.right - client_rect.left;
    int H = client_rect.bottom - client_rect.top;
    if (W <= 0 || H <= 0) return;

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // Fill the full client rect (incl. the rounded card's corners) with the
    // window background so transparent corner pixels stay charcoal.
    HBRUSH bgBrush = CreateSolidBrush(colors::kBgWindow);
    FillRect(memDC, &client_rect, bgBrush);
    DeleteObject(bgBrush);

    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Rounded card background
    draw_rounded_box(memDC, client_rect, 6, colors::kBgCard, colors::kBorderCard, 1);

    // Graph Area Padding
    const int padL = 4;
    const int padR = 4;
    const int padT = 4;
    const int padB = 4;
    const int graphW = W - padL - padR;
    const int graphH = H - padT - padB;

    // Draw Vertical Grid Lines (12 vertical ticks matching screenshot)
    Gdiplus::Color gridCol(GetRValue(colors::kGridLine), GetGValue(colors::kGridLine), GetBValue(colors::kGridLine));
    Gdiplus::Pen gridPen(gridCol, 1.0f);

    int num_ticks = 12;
    for (int i = 1; i < num_ticks; ++i) {
        Gdiplus::REAL x = (Gdiplus::REAL)(padL + ((double)i / num_ticks) * graphW);
        g.DrawLine(&gridPen, x, (Gdiplus::REAL)padT + 2, x, (Gdiplus::REAL)(H - padB - 2));
    }

    if (shm_box.valid()) {
        double freq = (double)shm_box.shm->ctl.qpc_frequency;
        if (freq <= 0.0) freq = 10.0e6;

        stats_.is_limited = (shm_box.shm->ctl.state == pacer::PacerState_Limited);
        stats_.target_fps = pacer::ctl_read_double(&shm_box.shm->ctl.target_fps_bits);
        if (stats_.target_fps <= 1.0) stats_.target_fps = 60.0;
        stats_.measured_hz = pacer::ctl_read_double(&shm_box.shm->ctl.measured_refresh_hz_bits);
        stats_.api = shm_box.shm->ctl.api;

        bool limited = stats_.is_limited;
        Gdiplus::Color lineCol = limited ? Gdiplus::Color(GetRValue(colors::kGraphLineLimited),
                                                           GetGValue(colors::kGraphLineLimited),
                                                           GetBValue(colors::kGraphLineLimited))
                                         : Gdiplus::Color(GetRValue(colors::kGraphLine),
                                                           GetGValue(colors::kGraphLine),
                                                           GetBValue(colors::kGraphLine));

        double target_ms = 1000.0 / stats_.target_fps;
        double max_ms = (std::max)(33.33, target_ms * 2.0);

        auto ms_to_y = [&](double ms) -> Gdiplus::REAL {
            double clamped = (std::min)((std::max)(0.0, ms), max_ms);
            return (Gdiplus::REAL)(padT + graphH - 4 - (clamped / max_ms) * (graphH - 8));
        };

        // Faint horizontal gridlines (33% / 66% of scale) for readability.
        Gdiplus::Color hGridCol(GetRValue(colors::kGridLine), GetGValue(colors::kGridLine), GetBValue(colors::kGridLine));
        Gdiplus::Pen hGridPen(hGridCol, 1.0f);
        for (int f = 1; f <= 2; ++f) {
            Gdiplus::REAL y = (Gdiplus::REAL)(padT + ((double)f / 3.0) * (graphH - 8) + 4);
            g.DrawLine(&hGridPen, (Gdiplus::REAL)padL, y, (Gdiplus::REAL)(padL + graphW), y);
        }

        LONG idx = shm_box.shm->write_idx;
        int avail = idx > (LONG)pacer::kRingCapacity ? (LONG)pacer::kRingCapacity : idx;
        if (avail > graphW) avail = graphW;

        if (avail > 1) {
            std::vector<Gdiplus::PointF> points;
            points.reserve(avail);

            double latest_ms = target_ms;
            for (int i = 0; i < avail; ++i) {
                LONG ri = idx - avail + i;
                std::uint64_t ticks = shm_box.shm->ring[(std::uint32_t)ri & (pacer::kRingCapacity - 1)];
                double ms = (double)ticks / freq * 1000.0;
                if (i == avail - 1) latest_ms = ms;

                Gdiplus::REAL x = (Gdiplus::REAL)(padL + ((double)i / (avail - 1)) * graphW);
                Gdiplus::REAL y = ms_to_y(ms);
                points.push_back(Gdiplus::PointF(x, y));
            }

            stats_.current_ms = latest_ms;
            stats_.fps = (latest_ms > 0.001) ? (1000.0 / latest_ms) : 0.0;

            // Subtle gradient fill under the frametime curve (tinted to match state)
            if (points.size() >= 2) {
                std::vector<Gdiplus::PointF> fillPoly = points;
                fillPoly.push_back(Gdiplus::PointF((Gdiplus::REAL)(padL + graphW), (Gdiplus::REAL)(padT + graphH - 4)));
                fillPoly.push_back(Gdiplus::PointF((Gdiplus::REAL)padL, (Gdiplus::REAL)(padT + graphH - 4)));

                Gdiplus::Color fillTop = limited ? Gdiplus::Color(45, 34, 197, 94)
                                                 : Gdiplus::Color(35, 56, 189, 248);
                Gdiplus::Color fillBot = limited ? Gdiplus::Color(0, 34, 197, 94)
                                                 : Gdiplus::Color(0, 56, 189, 248);
                Gdiplus::RectF gradRect((Gdiplus::REAL)padL, (Gdiplus::REAL)padT, (Gdiplus::REAL)graphW, (Gdiplus::REAL)graphH);
                Gdiplus::LinearGradientBrush gradBrush(gradRect, fillTop, fillBot,
                                                       Gdiplus::LinearGradientModeVertical);
                g.FillPolygon(&gradBrush, fillPoly.data(), (INT)fillPoly.size());
            }

            // Soft glow underlay for a premium neon look
            Gdiplus::Color glowCol = limited ? Gdiplus::Color(60, 34, 197, 94)
                                             : Gdiplus::Color(60, 56, 189, 248);
            Gdiplus::Pen glowPen(glowCol, 4.0f);
            glowPen.SetLineJoin(Gdiplus::LineJoinRound);
            g.DrawLines(&glowPen, points.data(), (INT)points.size());

            // Crisp frametime waveform line
            Gdiplus::Pen curvePen(lineCol, 1.6f);
            curvePen.SetLineJoin(Gdiplus::LineJoinRound);
            g.DrawLines(&curvePen, points.data(), (INT)points.size());
        }

        // Target frametime reference line (dashed) so pacing flatness is obvious
        Gdiplus::REAL targetY = ms_to_y(target_ms);
        Gdiplus::Color targetCol = limited ? Gdiplus::Color(110, 34, 197, 94)
                                           : Gdiplus::Color(90, 56, 189, 248);
        Gdiplus::Pen targetPen(targetCol, 1.0f);
        targetPen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawLine(&targetPen, (Gdiplus::REAL)padL, targetY,
                   (Gdiplus::REAL)(padL + graphW), targetY);
    } else {
        // Idle flat baseline
        Gdiplus::Color lineCol(GetRValue(colors::kGraphLineIdle), GetGValue(colors::kGraphLineIdle), GetBValue(colors::kGraphLineIdle));
        Gdiplus::Pen idlePen(lineCol, 1.2f);
        Gdiplus::REAL midY = (Gdiplus::REAL)(padT + graphH / 2.0f);
        g.DrawLine(&idlePen, (Gdiplus::REAL)padL + 6, midY, (Gdiplus::REAL)(W - padR - 6), midY);
    }

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

LRESULT CALLBACK graph_control_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        g_graph_instance.paint(hdc, rc, g_shm_box);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace ui

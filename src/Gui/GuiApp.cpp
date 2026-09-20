#include "Gui/GuiApp.h"

#include "Core/Injector.h"
#include "Core/ProcessTarget.h"
#include "Windows/ProcessUtils.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// Only guaranteed present in an SDK new enough to know about Windows 11;
// defining them ourselves when missing keeps this building against older
// SDKs too, since DwmSetWindowAttribute just wants plain integer values.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    // ---- Palette: dark neutral background with a violet accent ----------
    constexpr ImVec4 kColorBackground   { 0.071f, 0.063f, 0.086f, 1.00f };
    constexpr ImVec4 kColorSurface      { 0.106f, 0.098f, 0.129f, 1.00f };
    constexpr ImVec4 kColorSurfaceAlt   { 0.145f, 0.133f, 0.176f, 1.00f };
    constexpr ImVec4 kColorBorder       { 0.20f,  0.19f,  0.24f,  0.60f };
    constexpr ImVec4 kColorText         { 0.925f, 0.925f, 0.937f, 1.00f };
    constexpr ImVec4 kColorTextMuted    { 0.55f,  0.53f,  0.60f,  1.00f };
    constexpr ImVec4 kColorAccent       { 0.545f, 0.361f, 0.965f, 1.00f }; // #8B5CF6
    constexpr ImVec4 kColorAccentHover  { 0.655f, 0.545f, 0.980f, 1.00f }; // #A78BFA
    constexpr ImVec4 kColorAccentActive { 0.486f, 0.227f, 0.929f, 1.00f }; // #7C3AED
    constexpr ImVec4 kColorSuccess      { 0.38f,  0.82f,  0.55f,  1.00f };
    constexpr ImVec4 kColorDanger       { 0.94f,  0.38f,  0.42f,  1.00f };

    // Layout constants for the two bottom sections. The injection panel's
    // height is computed at runtime from real font/style metrics (see
    // ComputeInjectionPanelHeight) instead of a guessed pixel value, so the
    // process table above always reserves exactly the right amount of space.
    constexpr ImVec2 kInjectionPanelPadding{ 16.0f, 12.0f };
    constexpr float kInjectButtonHeight = 38.0f;
    constexpr float kStatusBarHeight = 44.0f;
    constexpr float kGapAboveStatusBar = 6.0f;
    constexpr float kStatusBarHorizontalPadding = 10.0f;

    // Custom title bar: replaces the OS-drawn caption entirely (see
    // WM_NCCALCSIZE/WM_NCHITTEST in WndProc), so we own its whole look.
    constexpr float kTitleBarHeight = 44.0f;
    constexpr float kTitleBarButtonWidth = 46.0f;
    constexpr float kContentPaddingX = 20.0f;
    constexpr float kContentPaddingY = 16.0f;

    // D3D11 device state, owned for the lifetime of the window.
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_deviceContext = nullptr;
    IDXGISwapChain* g_swapChain = nullptr;
    bool g_swapChainOccluded = false;
    ID3D11RenderTargetView* g_renderTargetView = nullptr;

    // Screen-space (== client-space, single-viewport) rects of the three
    // caption buttons from the last drawn frame. WM_NCHITTEST needs these to
    // carve the buttons out of the draggable title bar area — otherwise a
    // click on them would be swallowed as a caption-drag instead of reaching
    // ImGui as an ordinary click.
    struct PixelRect
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool Contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    };
    PixelRect g_minimizeButtonRect;
    PixelRect g_maximizeButtonRect;
    PixelRect g_closeButtonRect;

    struct StatusMessage
    {
        std::wstring text;
        bool isError = false;
        bool set = false;
    };

    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty())
            return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }

    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty())
            return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ToLower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return text;
    }

    void ApplyDyloraTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 8.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 6.0f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.WindowPadding = ImVec2(20, 16);
        style.FramePadding = ImVec2(10, 6);
        style.ItemSpacing = ImVec2(8, 6);
        style.ItemInnerSpacing = ImVec2(8, 6);
        style.CellPadding = ImVec2(14, 6);
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = kColorText;
        colors[ImGuiCol_TextDisabled] = kColorTextMuted;
        colors[ImGuiCol_WindowBg] = kColorBackground;
        colors[ImGuiCol_ChildBg] = kColorSurface;
        colors[ImGuiCol_PopupBg] = kColorSurfaceAlt;
        colors[ImGuiCol_Border] = kColorBorder;
        colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_FrameBg] = kColorSurfaceAlt;
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.17f, 0.23f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.20f, 0.27f, 1.00f);
        colors[ImGuiCol_TitleBg] = kColorBackground;
        colors[ImGuiCol_TitleBgActive] = kColorBackground;
        colors[ImGuiCol_MenuBarBg] = kColorSurface;
        colors[ImGuiCol_ScrollbarBg] = kColorSurface;
        colors[ImGuiCol_ScrollbarGrab] = kColorSurfaceAlt;
        colors[ImGuiCol_ScrollbarGrabHovered] = kColorAccentHover;
        colors[ImGuiCol_ScrollbarGrabActive] = kColorAccentActive;
        colors[ImGuiCol_CheckMark] = kColorAccent;
        colors[ImGuiCol_SliderGrab] = kColorAccent;
        colors[ImGuiCol_SliderGrabActive] = kColorAccentActive;
        colors[ImGuiCol_Button] = kColorSurfaceAlt;
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.22f, 0.29f, 1.00f);
        colors[ImGuiCol_ButtonActive] = kColorAccentActive;
        colors[ImGuiCol_Header] = ImVec4(kColorAccent.x, kColorAccent.y, kColorAccent.z, 0.35f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(kColorAccent.x, kColorAccent.y, kColorAccent.z, 0.55f);
        colors[ImGuiCol_HeaderActive] = ImVec4(kColorAccent.x, kColorAccent.y, kColorAccent.z, 0.75f);
        colors[ImGuiCol_Separator] = kColorBorder;
        colors[ImGuiCol_SeparatorHovered] = kColorAccentHover;
        colors[ImGuiCol_SeparatorActive] = kColorAccentActive;
        colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ResizeGripHovered] = kColorAccentHover;
        colors[ImGuiCol_ResizeGripActive] = kColorAccentActive;
        colors[ImGuiCol_Tab] = kColorSurface;
        colors[ImGuiCol_TabHovered] = kColorAccentHover;
        colors[ImGuiCol_TabActive] = kColorAccent;
        colors[ImGuiCol_TableHeaderBg] = kColorSurfaceAlt;
        colors[ImGuiCol_TableBorderStrong] = kColorBorder;
        colors[ImGuiCol_TableBorderLight] = kColorBorder;
        colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(kColorAccent.x, kColorAccent.y, kColorAccent.z, 0.35f);
        colors[ImGuiCol_NavHighlight] = kColorAccent;
    }

    // Loads the body font and returns a second, slightly larger instance of
    // the same face for the "Dylora" wordmark in the title bar — a real font
    // size rather than a runtime scale, so it stays crisp and other widgets
    // drawn after it are unaffected.
    ImFont* LoadInterfaceFont(ImGuiIO& io)
    {
        wchar_t fontsDir[MAX_PATH];
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 2;

        if (SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, 0, fontsDir) == S_OK)
        {
            std::wstring path = std::wstring(fontsDir) + L"\\segoeui.ttf";
            if (PathFileExistsW(path.c_str()))
            {
                std::string utf8Path = WideToUtf8(path);
                io.Fonts->AddFontFromFileTTF(utf8Path.c_str(), 18.0f, &config);
                return io.Fonts->AddFontFromFileTTF(utf8Path.c_str(), 21.0f, &config);
            }
        }

        ImFont* bodyFont = io.Fonts->AddFontDefault();
        return bodyFont;
    }

    // -------------------------------------------------------------------
    // Direct3D 11 device/swapchain plumbing (minimal, single-viewport).
    // -------------------------------------------------------------------
    bool CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (!backBuffer)
            return false;

        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
        backBuffer->Release();
        return true;
    }

    void CleanupRenderTarget()
    {
        if (g_renderTargetView)
        {
            g_renderTargetView->Release();
            g_renderTargetView = nullptr;
        }
    }

    bool CreateDeviceD3D(HWND hwnd)
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = 0;
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_swapChain, &g_device, &featureLevel, &g_deviceContext);

        if (hr != S_OK)
            return false;

        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
        if (g_deviceContext) { g_deviceContext->Release(); g_deviceContext = nullptr; }
        if (g_device) { g_device->Release(); g_device = nullptr; }
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        case WM_SIZE:
            if (g_device && wParam != SIZE_MINIMIZED)
            {
                CleanupRenderTarget();
                g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_GETMINMAXINFO:
        {
            // Keeps the window from shrinking past the point where the fixed
            // bottom sections would no longer fit above the table's min height.
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = { 760, 560 };
            return 0;
        }
        case WM_NCCALCSIZE:
            // Claiming the whole window rect as client area removes the
            // OS-drawn caption and borders. WS_THICKFRAME keeps working for
            // resize (Windows hit-tests that from the window style, not from
            // what we return here), we just have to inset by the resize
            // frame ourselves while maximized so content doesn't run under
            // the taskbar or past the monitor edge.
            if (wParam == TRUE)
            {
                if (IsZoomed(hwnd))
                {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    int borderX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    int borderY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left += borderX;
                    params->rgrc[0].right -= borderX;
                    params->rgrc[0].top += borderY;
                    params->rgrc[0].bottom -= borderY;
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_NCHITTEST:
        {
            // Let Windows do the normal edge/corner resize hit-testing first;
            // only promote a plain "client" hit to HTCAPTION when it falls in
            // our own title bar strip, so the window can still be dragged and
            // double-click-to-maximize keeps working, while our own buttons
            // (excluded below) keep receiving ordinary clicks.
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT)
            {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);
                float x = static_cast<float>(pt.x);
                float y = static_cast<float>(pt.y);
                bool onButton = g_minimizeButtonRect.Contains(x, y) ||
                    g_maximizeButtonRect.Contains(x, y) ||
                    g_closeButtonRect.Contains(x, y);
                if (y >= 0 && y < kTitleBarHeight && !onButton)
                    return HTCAPTION;
            }
            return hit;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // Native "open file" dialog, filtered to .dll files.
    std::wstring BrowseForDll(HWND owner)
    {
        wchar_t path[MAX_PATH]{};

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Select a DLL to inject";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&ofn))
            return path;

        return {};
    }

    // -------------------------------------------------------------------
    // Application state and per-frame UI.
    // -------------------------------------------------------------------
    struct AppState
    {
        std::vector<ProcessInfo> processes;
        char filter[128] = "";
        int selectedIndex = -1;
        char dllPathUtf8[MAX_PATH * 2] = "";
        StatusMessage status;
    };

    void RefreshProcessList(AppState& state)
    {
        state.processes = GetProcessList();
        std::sort(state.processes.begin(), state.processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
            return ToLower(a.name) < ToLower(b.name);
            });
        state.selectedIndex = -1;
    }

    void SetStatus(AppState& state, const std::wstring& text, bool isError)
    {
        state.status = { text, isError, true };
    }

    // Real height of DrawInjectionPanel's content, from actual font/style
    // metrics rather than a hand-measured guess — stays correct regardless
    // of the loaded font or any future spacing tweak.
    float ComputeInjectionPanelHeight()
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        float height = kInjectionPanelPadding.y * 2.0f;   // child's own top+bottom padding
        height += 6.0f + style.ItemSpacing.y;             // leading Dummy(0, 6)
        height += ImGui::GetTextLineHeight() + style.ItemSpacing.y; // "DLL PATH" label
        height += 2.0f + style.ItemSpacing.y;             // Dummy(0, 2)
        height += ImGui::GetFrameHeight() + style.ItemSpacing.y;    // path field + Browse row
        height += 6.0f + style.ItemSpacing.y;             // Dummy(0, 6)
        height += kInjectButtonHeight;                    // Inject button (last item)
        return height;
    }

    // One caption button (minimize/maximize/close): hit-tests and draws its
    // own hover fill via an InvisibleButton, then the icon is drawn as plain
    // vector lines (never a font glyph — see the status dot for why) so it
    // renders identically regardless of font coverage.
    bool DrawCaptionButton(const char* id, float x, ImVec4 hoverBg, PixelRect& outRect)
    {
        ImGui::SetCursorPos(ImVec2(x, 0));
        bool clicked = ImGui::InvisibleButton(id, ImVec2(kTitleBarButtonWidth, kTitleBarHeight));

        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        outRect = { rmin.x, rmin.y, rmax.x, rmax.y };

        if (ImGui::IsItemHovered())
            ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, ImGui::ColorConvertFloat4ToU32(hoverBg));

        return clicked;
    }

    void DrawTitleBar(HWND hwnd, ImFont* titleFont)
    {
        float windowWidth = ImGui::GetWindowSize().x;

        // Title, left-aligned and inset to match the content padding below
        // it, vertically centered on the wordmark's own (larger) line height.
        // "DLL Injector" is drawn in the smaller body font, so its own line
        // is shorter than "Dylora"'s — centering it on the shared row height
        // (rather than reusing "Dylora"'s top) is what actually lines up the
        // two baselines visually.
        ImGui::PushFont(titleFont);
        float rowHeight = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        float bodyLineHeight = ImGui::GetTextLineHeight();

        float titleY = (kTitleBarHeight - rowHeight) * 0.5f;
        ImGui::SetCursorPos(ImVec2(kContentPaddingX, titleY));
        ImGui::PushFont(titleFont);
        ImGui::TextColored(kColorAccent, "Dylora");
        ImGui::PopFont();

        ImGui::SameLine(0.0f, 10.0f);
        ImGui::SetCursorPosY(titleY + (rowHeight - bodyLineHeight) * 0.5f);
        ImGui::TextColored(kColorTextMuted, "DLL Injector");

        // Caption buttons, flush to the top-right corner like a native title bar.
        float closeX = windowWidth - kTitleBarButtonWidth;
        float maximizeX = closeX - kTitleBarButtonWidth;
        float minimizeX = maximizeX - kTitleBarButtonWidth;

        if (DrawCaptionButton("##minimize", minimizeX, kColorSurfaceAlt, g_minimizeButtonRect))
            ShowWindow(hwnd, SW_MINIMIZE);

        bool maximized = IsZoomed(hwnd);
        if (DrawCaptionButton("##maximize", maximizeX, kColorSurfaceAlt, g_maximizeButtonRect))
            ShowWindow(hwnd, maximized ? SW_RESTORE : SW_MAXIMIZE);

        if (DrawCaptionButton("##close", closeX, kColorDanger, g_closeButtonRect))
            PostMessage(hwnd, WM_CLOSE, 0, 0);

        // Icons, drawn on top of the (invisible) buttons as simple vector shapes.
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(kColorTextMuted);
        ImU32 closeIconColor = ImGui::ColorConvertFloat4ToU32(kColorText);
        const float half = 5.0f;

        ImVec2 minC{ (g_minimizeButtonRect.x0 + g_minimizeButtonRect.x1) * 0.5f, (g_minimizeButtonRect.y0 + g_minimizeButtonRect.y1) * 0.5f };
        drawList->AddLine(ImVec2(minC.x - half, minC.y), ImVec2(minC.x + half, minC.y), iconColor, 1.5f);

        ImVec2 maxC{ (g_maximizeButtonRect.x0 + g_maximizeButtonRect.x1) * 0.5f, (g_maximizeButtonRect.y0 + g_maximizeButtonRect.y1) * 0.5f };
        drawList->AddRect(ImVec2(maxC.x - half, maxC.y - half), ImVec2(maxC.x + half, maxC.y + half), iconColor, 0.0f, 0, 1.5f);

        ImVec2 closeC{ (g_closeButtonRect.x0 + g_closeButtonRect.x1) * 0.5f, (g_closeButtonRect.y0 + g_closeButtonRect.y1) * 0.5f };
        drawList->AddLine(ImVec2(closeC.x - half, closeC.y - half), ImVec2(closeC.x + half, closeC.y + half), closeIconColor, 1.5f);
        drawList->AddLine(ImVec2(closeC.x - half, closeC.y + half), ImVec2(closeC.x + half, closeC.y - half), closeIconColor, 1.5f);

        // Thin separator between the title bar and the content below it.
        drawList->AddLine(ImVec2(0, kTitleBarHeight), ImVec2(windowWidth, kTitleBarHeight), ImGui::ColorConvertFloat4ToU32(kColorBorder));

        // Reserve exactly the title bar's height regardless of what was
        // actually drawn, so the content child below always starts at the
        // same place.
        ImGui::SetCursorPos(ImVec2(0.0f, kTitleBarHeight));
    }

    void DrawProcessList(AppState& state)
    {
        ImGui::TextColored(kColorTextMuted, "TARGET PROCESS");
        ImGui::Dummy(ImVec2(0, 2));

        ImGui::SetNextItemWidth(-96);
        ImGui::InputTextWithHint("##filter", "Filter by name or PID...", state.filter, sizeof(state.filter));
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(-1, 0)))
            RefreshProcessList(state);

        ImGui::Dummy(ImVec2(0, 4));

        // PadOuterX is what actually makes CellPadding apply to the outer-most
        // edge of the first/last column — without it, borders aside, the table
        // ignores CellPadding on its outer edges and text sits flush against them.
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX;
        // Below the table sit 3 more items (the injection panel, the gap
        // dummy, and the status bar) — each boundary between consecutive
        // items costs one more style.ItemSpacing.y, which is easy to forget
        // and was the actual reason the status bar used to run past the
        // bottom of the window.
        const float itemGap = ImGui::GetStyle().ItemSpacing.y;
        float reservedBottomHeight = ComputeInjectionPanelHeight() + kGapAboveStatusBar + kStatusBarHeight + itemGap * 3.0f;
        ImVec2 tableSize(0, ImGui::GetContentRegionAvail().y - reservedBottomHeight);
        if (tableSize.y < 120.0f)
            tableSize.y = 120.0f;

        if (ImGui::BeginTable("processes", 2, tableFlags, tableSize))
        {
            ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Process Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            std::wstring needle = ToLower(Utf8ToWide(state.filter));

            for (int i = 0; i < static_cast<int>(state.processes.size()); ++i)
            {
                const ProcessInfo& process = state.processes[i];

                if (!needle.empty())
                {
                    bool matchesName = ToLower(process.name).find(needle) != std::wstring::npos;
                    bool matchesPid = std::to_wstring(process.pid).find(needle) != std::wstring::npos;
                    if (!matchesName && !matchesPid)
                        continue;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                std::string pidLabel = std::to_string(process.pid);
                bool selected = (state.selectedIndex == i);
                ImGui::PushID(i);
                if (ImGui::Selectable(pidLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    state.selectedIndex = i;
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(WideToUtf8(process.name).c_str());
            }

            ImGui::EndTable();
        }
    }

    void DrawInjectionPanel(AppState& state, HWND hwnd)
    {
        // Real inner padding (instead of the borderless child's default of
        // none), so the field and buttons don't sit flush against the edges.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kInjectionPanelPadding);
        ImGui::BeginChild("injection_panel", ImVec2(0, ComputeInjectionPanelHeight()),
            ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(kColorTextMuted, "DLL PATH");
        ImGui::Dummy(ImVec2(0, 2));

        ImGui::SetNextItemWidth(-96);
        ImGui::InputTextWithHint("##dllpath", "Path to the .dll to inject...", state.dllPathUtf8, sizeof(state.dllPathUtf8));
        ImGui::SameLine();
        if (ImGui::Button("Browse", ImVec2(-1, 0)))
        {
            std::wstring picked = BrowseForDll(hwnd);
            if (!picked.empty())
            {
                std::string utf8 = WideToUtf8(picked);
                strncpy_s(state.dllPathUtf8, utf8.c_str(), sizeof(state.dllPathUtf8) - 1);
            }
        }

        ImGui::Dummy(ImVec2(0, 6));

        bool hasProcess = state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(state.processes.size());
        bool hasDllPath = state.dllPathUtf8[0] != '\0';
        bool canInject = hasProcess && hasDllPath;

        ImGui::PushStyleColor(ImGuiCol_Button, kColorAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColorAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kColorAccentActive);

        ImGui::BeginDisabled(!canInject);
        if (ImGui::Button("Inject", ImVec2(-1, 38)))
        {
            const ProcessInfo& processInfo = state.processes[state.selectedIndex];
            std::wstring dllPath = Utf8ToWide(state.dllPathUtf8);

            ProcessTarget target = ProcessTarget::FromPid(processInfo.pid);
            if (!target.IsValid())
            {
                SetStatus(state, L"Process not found or access denied.", true);
            }
            else
            {
                std::wstring error;
                bool success = InjectDll(target, dllPath, &error);
                if (success)
                    SetStatus(state, L"Injected into [" + std::to_wstring(target.Pid()) + L"] " + target.Name(), false);
                else
                    SetStatus(state, L"Injection failed: " + error, true);
            }
        }
        ImGui::EndDisabled();

        ImGui::PopStyleColor(3);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // A single always-visible status strip showing the outcome of the last
    // action. Fixed height, no scrolling — replaces the old scrolling log.
    void DrawStatusBar(AppState& state)
    {
        ImGui::Dummy(ImVec2(0, kGapAboveStatusBar));

        // Green by default: an idle "Ready" state is itself a status worth
        // signalling, not just an absence of errors.
        ImVec4 accent = kColorSuccess;
        std::string message = "Ready - select a process and a DLL to inject.";

        if (state.status.set)
        {
            accent = state.status.isError ? kColorDanger : kColorSuccess;
            message = WideToUtf8(state.status.text);
        }

        ImVec4 tint(accent.x, accent.y, accent.z, 0.12f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, tint);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.45f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        // Small, purely horizontal inset — vertical centering is done by hand
        // below so a single line of text always lands in the middle of the
        // fixed-height strip, regardless of font metrics.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kStatusBarHorizontalPadding, 0.0f));

        ImGui::BeginChild("status", ImVec2(0, kStatusBarHeight), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float lineHeight = ImGui::GetTextLineHeight();
        ImGui::SetCursorPosY((kStatusBarHeight - lineHeight) * 0.5f);

        // A small vector dot (not a font glyph) so it renders regardless of font coverage.
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::Bullet();
        ImGui::PopStyleColor();
        // A touch more breathing room than the default item spacing gives.
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + 2.0f);
        ImGui::TextColored(kColorText, "%s", message.c_str());
        ImGui::EndChild();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
}

int RunGuiApp(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"DyloraWindowClass", nullptr };
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Dylora", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
        nullptr, nullptr, wc.hInstance, nullptr);

    // Windows only re-runs WM_NCCALCSIZE (which is what strips the native
    // caption) on frame-changing events such as a maximize/restore toggle —
    // not on plain creation. Without forcing one here, the OS title bar
    // stays visible until the first such event. SWP_FRAMECHANGED forces
    // that recalculation immediately, before the window is ever shown.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    // Windows 11 rounds a normal top-level window's corners automatically,
    // but treats one whose WM_NCCALCSIZE claims the full client rect as a
    // borderless/fullscreen-style window and skips that rounding. Asking
    // for it explicitly restores the native Win11 rounded-corner look.
    const int cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Nothing here needs persisting across launches, and writing/reading it
    // just leaves an imgui.ini next to the exe - disable it entirely.
    io.IniFilename = nullptr;

    ApplyDyloraTheme();
    ImFont* titleFont = LoadInterfaceFont(io);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_deviceContext);

    AppState state;
    RefreshProcessList(state);

    // The UI never animates on its own (no timers, no continuous
    // transitions) - the only thing that ever needs a steady redraw is a
    // blinking caret while a text field is focused. So we wait indefinitely
    // for real input/window messages, and only fall back to a short polling
    // timeout while a text field actually has focus.
    bool running = true;
    bool needsCaretBlinkWake = false;
    while (running)
    {
        DWORD waitTimeout = needsCaretBlinkWake ? 100 : INFINITE;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, waitTimeout, QS_ALLINPUT);

        bool gotMessage = false;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            gotMessage = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;

        // Nothing actually arrived and nothing needs a periodic redraw
        // (no focused text field) - truly nothing to draw. Go straight back
        // to waiting instead of building and presenting an unchanged frame.
        if (!gotMessage && !needsCaretBlinkWake)
            continue;

        // Minimized: nothing is visible, so there is nothing to build or
        // present. Keep waiting for messages without touching the GPU.
        if (IsIconic(hwnd))
            continue;

        if (g_swapChainOccluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            Sleep(10);
            continue;
        }
        g_swapChainOccluded = false;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        // The root window itself has no padding — the title bar draws
        // full-bleed, buttons flush to the corner. Everything below it lives
        // in its own child with the app's normal content padding, unchanged
        // from before.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool open = ImGui::Begin("Dylora##root", nullptr, rootFlags);
        ImGui::PopStyleVar();

        if (open)
        {
            DrawTitleBar(hwnd, titleFont);

            // NoBackground here: this child exists only to apply content
            // padding below the title bar. Without it, its own ChildBg
            // (kColorSurface) paints the whole content area that color,
            // which is exactly the color the injection panel below uses for
            // its own background — masking it completely. Leaving this
            // transparent lets the root window's kColorBackground show
            // through, restoring the contrast the panel had before the
            // title bar introduced this wrapping child.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kContentPaddingX, kContentPaddingY));
            ImGui::BeginChild("content_area", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar();

            DrawProcessList(state);
            DrawInjectionPanel(state, hwnd);
            DrawStatusBar(state);

            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::Render();
        const float clearColor[4] = { kColorBackground.x, kColorBackground.y, kColorBackground.z, 1.0f };
        g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
        g_deviceContext->ClearRenderTargetView(g_renderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_swapChain->Present(1, 0);
        g_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        // Only keep polling on a timeout while a text field is actually
        // focused (its caret needs to blink without new input); otherwise
        // the next loop iteration goes back to waiting indefinitely.
        needsCaretBlinkWake = io.WantTextInput;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

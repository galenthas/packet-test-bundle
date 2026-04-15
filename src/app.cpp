#include "app.h"

#include "app_meta.h"
#include "command_builder.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"
#include "resource.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <filesystem>
#include <iphlpapi.h>
#include <vector>
#include <windows.h>
#include <ws2tcpip.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hWnd = nullptr;
static ImFont* g_uiFont = nullptr;
static ImFont* g_monoFont = nullptr;
static ImFont* g_summaryValueFont = nullptr;

namespace {
constexpr unsigned short kDiscoveryPort = 39391;
constexpr const char* kDiscoveryQuery = "PTB_DISCOVER";
constexpr const char* kDiscoveryReplyPrefix = "PTB_REPLY ";

std::string executableDir() {
    char buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ".";
    return fs::path(buffer).parent_path().string();
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_SIZE && g_pd3dDevice && wParam != SIZE_MINIMIZED) {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
        g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* backBuffer = nullptr;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (backBuffer) {
            g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
            backBuffer->Release();
        }
        return 0;
    }
    if (msg == WM_SYSCOMMAND && (wParam & 0xfff0) == SC_KEYMENU) return 0;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

std::string fmt(double value, int decimals = 2) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), ("%." + std::to_string(decimals) + "f").c_str(), value);
    return buffer;
}

std::string trimCopy(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string wideToUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::string sockaddrToIpv4String(const SOCKADDR* addr) {
    if (!addr || addr->sa_family != AF_INET) return {};
    char buffer[INET_ADDRSTRLEN] = {};
    const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
    if (!InetNtopA(AF_INET, const_cast<IN_ADDR*>(&in->sin_addr), buffer, INET_ADDRSTRLEN)) return {};
    return buffer;
}

bool CompactInputText(const char* id, const char* label, std::string& value, const char* hint = "", size_t capacity = 128, float inputWidth = 220.0f) {
    ImGui::PushID(id);
    std::vector<char> buffer(capacity, 0);
    strncpy_s(buffer.data(), buffer.size(), value.c_str(), buffer.size() - 1);
    ImGui::SetNextItemWidth(inputWidth);
    bool changed = (hint && hint[0] != '\0')
        ? ImGui::InputTextWithHint("##value", hint, buffer.data(), buffer.size())
        : ImGui::InputText("##value", buffer.data(), buffer.size());
    if (changed) value = buffer.data();
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool CompactComboRow(const char* id, const char* label, int* currentItem, const char* const items[], int itemsCount, float inputWidth = 220.0f) {
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(inputWidth);
    const bool changed = ImGui::Combo("##value", currentItem, items, itemsCount);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool CompactIntStepperRow(const char* id, const char* label, int& value, float inputWidth = 220.0f, int step = 1, int minValue = 0) {
    ImGui::PushID(id);
    const float buttonWidth = 20.0f;
    const float valueWidth = inputWidth - (buttonWidth * 2.0f) - 8.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", value);
    ImGui::SetNextItemWidth(valueWidth);
    bool changed = ImGui::InputText("##value", buf, sizeof(buf), ImGuiInputTextFlags_CharsDecimal);
    if (changed) {
        value = std::max(minValue, std::atoi(buf));
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("-", ImVec2(buttonWidth, 0.0f))) {
        value = std::max(minValue, value - step);
        changed = true;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("+", ImVec2(buttonWidth, 0.0f))) {
        value += step;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

void panelHeader(const char* title) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(37, 59, 91, 255));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, p.y + 3.0f));
    ImGui::TextUnformatted(title);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4.0f));
}

void summaryKpi(const char* label, const std::string& value, ImU32 accent) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 24.0f;
    draw->AddRectFilled(start, ImVec2(start.x + width, start.y + height), IM_COL32(15, 20, 29, 255));
    draw->AddRect(start, ImVec2(start.x + width, start.y + height), IM_COL32(54, 70, 98, 255));
    draw->AddRectFilled(start, ImVec2(start.x + 4.0f, start.y + height), accent);
    ImGui::SetCursorScreenPos(ImVec2(start.x + 10.0f, start.y + 4.0f));
    ImGui::TextUnformatted(label);
    ImGui::SameLine(width - 78.0f);
    if (g_summaryValueFont) ImGui::PushFont(g_summaryValueFont);
    ImGui::TextUnformatted(value.c_str());
    if (g_summaryValueFont) ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height + 2.0f));
}

bool coloredButton(const char* label, ImU32 color, const ImVec2& size) {
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(color);
    const ImVec4 hover(base.x + 0.06f, base.y + 0.06f, base.z + 0.06f, base.w);
    const ImVec4 active(base.x - 0.05f, base.y - 0.05f, base.z - 0.05f, base.w);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

void logPanel(const char* id, const char* title, std::deque<std::string>& log, ImFont* font) {
    ImGui::BeginChild(id, ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
    panelHeader(title);
    if (ImGui::BeginPopupContextWindow((std::string(id) + "_menu").c_str())) {
        if (ImGui::MenuItem("Clear")) log.clear();
        if (ImGui::MenuItem("Copy All")) {
            std::string all;
            for (size_t i = 0; i < log.size(); ++i) { all += log[i]; if (i + 1 < log.size()) all += "\r\n"; }
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.03f, 1.0f));
    ImGui::BeginChild((std::string(id) + "_scroll").c_str(), ImVec2(0, 0), false);
    const float maxBefore = ImGui::GetScrollMaxY();
    const float scrollBefore = ImGui::GetScrollY();
    const bool stickToBottom = maxBefore <= 0.0f || scrollBefore >= maxBefore - 4.0f;
    if (font) ImGui::PushFont(font);
    for (const auto& line : log) ImGui::TextUnformatted(line.c_str());
    if (font) ImGui::PopFont();
    if (!log.empty() && stickToBottom) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

std::string bwHint(const AppConfig& cfg) {
    if (!cfg.serverMode && cfg.udp) return "1.00 Mbit (default)";
    if (!cfg.serverMode && cfg.useIperf3) return "0.00 Mbit / unlimited (default)";
    return "N/A";
}
}

App::App() {
    refreshPcapInterfaces();
    initDiscovery();
}

App::~App() {
    shutdownDiscovery();
}

bool App::createWindow() {
    HINSTANCE inst = GetModuleHandle(nullptr);
    HICON largeIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP_ICON));
    HICON smallIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, inst, largeIcon, nullptr, nullptr, nullptr, appmeta::kWindowClassName, smallIcon};
    RegisterClassExW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName, appmeta::kWindowTitle, WS_OVERLAPPEDWINDOW, 100, 100, 1500, 900, nullptr, nullptr, wc.hInstance, nullptr);
    return g_hWnd != nullptr;
}

void App::destroyWindow() { if (g_hWnd) { DestroyWindow(g_hWnd); g_hWnd = nullptr; } UnregisterClassW(appmeta::kWindowClassName, GetModuleHandle(nullptr)); }

bool App::createDeviceD3D(void* hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = (HWND)hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl; const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK) return false;
    createRenderTarget(); return true;
}

void App::cleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }
void App::createRenderTarget() { ID3D11Texture2D* backBuffer = nullptr; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)); if (backBuffer) { g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView); backBuffer->Release(); } }
void App::cleanupDeviceD3D() { cleanupRenderTarget(); if (g_pSwapChain) g_pSwapChain->Release(); if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release(); if (g_pd3dDevice) g_pd3dDevice->Release(); g_pSwapChain = nullptr; g_pd3dDeviceContext = nullptr; g_pd3dDevice = nullptr; }

int App::run() {
    if (!createWindow()) return 1;
    if (!createDeviceD3D(g_hWnd)) { destroyWindow(); return 1; }
    ShowWindow(g_hWnd, SW_SHOWDEFAULT); UpdateWindow(g_hWnd);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImPlot::CreateContext(); ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const fs::path proggy = fs::path(executableDir()) / "_deps" / "imgui-src" / "misc" / "fonts" / "ProggyClean.ttf";
    if (fs::exists(proggy)) {
        ImFontConfig cfg{}; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        g_uiFont = io.Fonts->AddFontFromFileTTF(proggy.string().c_str(), 13.0f, &cfg);
        g_monoFont = io.Fonts->AddFontFromFileTTF(proggy.string().c_str(), 13.0f, &cfg);
        g_summaryValueFont = io.Fonts->AddFontFromFileTTF(proggy.string().c_str(), 15.0f, &cfg);
        if (g_uiFont) io.FontDefault = g_uiFont;
    } else if (fs::exists("C:\\Windows\\Fonts\\lucon.ttf")) {
        ImFontConfig cfg{}; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        g_uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\lucon.ttf", 12.0f, &cfg);
        g_monoFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\lucon.ttf", 12.0f, &cfg);
        g_summaryValueFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\lucon.ttf", 14.0f, &cfg);
        if (g_uiFont) io.FontDefault = g_uiFont;
    } else if (fs::exists("C:\\Windows\\Fonts\\consola.ttf")) {
        ImFontConfig cfg{}; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        g_uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 12.0f, &cfg);
        g_monoFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 12.0f, &cfg);
        g_summaryValueFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f, &cfg);
        if (g_uiFont) io.FontDefault = g_uiFont;
    }
    applyTheme();
    ImGui_ImplWin32_Init(g_hWnd); ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) done = true; }
        if (done) break;
        drainWorkerOutputs();
        tickDiscovery();
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); renderUi(); ImGui::Render();
        const float clearColor[4] = {0.12f, 0.12f, 0.12f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); g_pSwapChain->Present(1, 0);
    }
    stopIperf(); stopPing(); pcapWorker_.stop();
    shutdownDiscovery();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImPlot::DestroyContext(); ImGui::DestroyContext();
    cleanupDeviceD3D(); destroyWindow(); return 0;
}

void App::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin(appmeta::kWindowTitleUtf8, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("iperf:");
    ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(iperfStatus_ == "Running" ? IM_COL32(46,160,67,255) : IM_COL32(207,34,46,255)), "%s", iperfStatus_.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("|");
    ImGui::SameLine(); ImGui::TextUnformatted("ping:");
    ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pingStatus_ == "Running" ? IM_COL32(46,160,67,255) : IM_COL32(207,34,46,255)), "%s", pingStatus_.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("|");
    ImGui::SameLine(); ImGui::TextDisabled("%s", appmeta::kVersionUtf8);
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float leftWidth = 352.0f;
    const float summaryHeight = 176.0f;
    const float cfgHeight = std::min(avail.y - summaryHeight - spacing, 560.0f);
    ImGui::BeginChild("left", ImVec2(leftWidth, avail.y), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("cfg", ImVec2(0, cfgHeight), false, ImGuiWindowFlags_NoScrollbar); drawConfigPanel(); ImGui::EndChild();
    ImGui::BeginChild("sumhost", ImVec2(0, summaryHeight), false, ImGuiWindowFlags_NoScrollbar); drawSummaryPanel(); ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, avail.y), false, ImGuiWindowFlags_NoScrollbar); drawLogAndCharts(); ImGui::EndChild();
    ImGui::End();
}

void App::drawConfigPanel() {
    ImGui::BeginChild("config", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
    const float inputWidth = 228.0f;
    if (ImGui::CollapsingHeader("iperf Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* versions[] = {"iperf3", "iperf2"}; int versionIdx = cfg_.useIperf3 ? 0 : 1;
        if (CompactComboRow("version", "Version", &versionIdx, versions, IM_ARRAYSIZE(versions), inputWidth)) { cfg_.useIperf3 = versionIdx == 0; cfg_.port = cfg_.useIperf3 ? 5201 : 5001; syncPortAndFilter(); }
        const char* modes[] = {"Client", "Server"}; int modeIdx = cfg_.serverMode ? 1 : 0; if (CompactComboRow("mode", "Mode", &modeIdx, modes, IM_ARRAYSIZE(modes), inputWidth)) cfg_.serverMode = modeIdx == 1;
        const char* protocols[] = {"TCP", "UDP"}; int protoIdx = cfg_.udp ? 1 : 0; if (CompactComboRow("protocol", "Protocol", &protoIdx, protocols, IM_ARRAYSIZE(protocols), inputWidth)) { cfg_.udp = protoIdx == 1; syncPortAndFilter(); }
        if (CompactIntStepperRow("port", "Port", cfg_.port, inputWidth, 1, 1)) syncPortAndFilter();
        CompactInputText("server_ip", "Server IP", cfg_.targetIp, "", 128, inputWidth);
        CompactInputText("bind_addr", "Bind Addr", cfg_.bindAddr, "", 128, inputWidth);
    }
    if (ImGui::CollapsingHeader("Test Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        CompactIntStepperRow("duration", "Duration", cfg_.duration, inputWidth, 1, 1);
        CompactIntStepperRow("interval", "Interval", cfg_.interval, inputWidth, 1, 1);
        CompactIntStepperRow("parallel", "Parallel", cfg_.parallel, inputWidth, 1, 1);
        CompactInputText("bandwidth", "Bandwidth (Mbit)", cfg_.bandwidthMbps, bwHint(cfg_).c_str(), 64, inputWidth);
        CompactInputText("packet_len", "Packet Len", cfg_.packetLength, "", 64, inputWidth);
        ImGui::Checkbox("Bidirectional", &cfg_.bidirectional);
    }
    if (ImGui::CollapsingHeader("Multicast / Broadcast")) {
        ImGui::Checkbox("Multicast", &cfg_.multicast);
        CompactInputText("multicast_group", "Multicast Group", cfg_.multicastGroup, "", 128, inputWidth);
        ImGui::Checkbox("Broadcast (iperf2 only)", &cfg_.broadcast);
    }
    if (ImGui::CollapsingHeader("PCAP Cross-check", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable PCAP", &cfg_.pcapEnabled);
        if (!pcapInterfaces_.empty()) {
            std::vector<const char*> labels; for (const auto& iface : pcapInterfaces_) labels.push_back(iface.description.c_str());
            if (cfg_.pcapInterfaceIndex >= (int)labels.size()) cfg_.pcapInterfaceIndex = 0;
            ImGui::Combo("Interface", &cfg_.pcapInterfaceIndex, labels.data(), (int)labels.size());
        } else {
            ImGui::TextWrapped("%s", pcapInterfaceError_.empty() ? "Arayuz listesi alinamadi." : pcapInterfaceError_.c_str());
            if (ImGui::Button("Refresh Interfaces")) refreshPcapInterfaces();
        }
        CompactInputText("pcap_filter", "PCAP Filter", cfg_.pcapFilter, "", 128, inputWidth);
    }
    if (ImGui::CollapsingHeader("Ping", ImGuiTreeNodeFlags_DefaultOpen)) {
        CompactInputText("ping_target", "Target", cfg_.pingTarget, cfg_.pingTarget.empty() ? cfg_.targetIp.c_str() : "", 128, inputWidth);
        if (coloredButton("Start Ping", IM_COL32(24, 119, 242, 255), ImVec2(150, 0))) startPing();
        ImGui::SameLine();
        if (coloredButton("Stop Ping", IM_COL32(220, 38, 53, 255), ImVec2(150, 0))) stopPing();
    }
    ImGui::Spacing();
    if (coloredButton("Start iperf", IM_COL32(40, 167, 69, 255), ImVec2(150, 0))) startIperf();
    ImGui::SameLine();
    if (coloredButton("Stop iperf", IM_COL32(220, 38, 53, 255), ImVec2(150, 0))) stopIperf();
    ImGui::EndChild();
}

void App::drawSummaryPanel() {
    ImGui::BeginChild("summary", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
    panelHeader("Test Summary");
    const bool hasDiff = summary_.pcapCaptured >= 0 && summary_.pcapIperfPackets >= 0;
    const std::string throughput = summary_.throughputMbps > 0.0 ? fmt(summary_.throughputMbps, 2) : "--";
    const std::string captured = summary_.pcapCaptured >= 0 ? std::to_string(summary_.pcapCaptured) : "--";
    const std::string diff = hasDiff ? std::to_string(summary_.pcapCaptured - summary_.pcapIperfPackets) : "--";
    const std::string refPkts = summary_.pcapIperfPackets >= 0 ? std::to_string(summary_.pcapIperfPackets) : "--";
    const ImU32 diffColor = !hasDiff ? IM_COL32(70, 85, 110, 255)
        : ((summary_.pcapCaptured - summary_.pcapIperfPackets) == 0 ? IM_COL32(32, 140, 73, 255)
        : ((summary_.pcapCaptured - summary_.pcapIperfPackets) < 0 ? IM_COL32(185, 54, 67, 255) : IM_COL32(196, 140, 25, 255)));
    summaryKpi("Throughput", throughput, IM_COL32(29, 108, 201, 255));
    summaryKpi("PCAP Captured", captured, IM_COL32(33, 136, 56, 255));
    summaryKpi("Diff", diff, diffColor);

    const float leftLabelX = 8.0f;
    const float leftValueX = 92.0f;
    const float rightLabelX = 168.0f;
    const float rightValueX = 260.0f;
    auto summaryPair = [&](const char* leftLabel, const std::string& leftValue, const char* rightLabel, const std::string& rightValue) {
        ImGui::SetCursorPosX(leftLabelX); ImGui::Text("%s", leftLabel);
        ImGui::SameLine(leftValueX); ImGui::Text("%s", leftValue.c_str());
        ImGui::SameLine(rightLabelX); ImGui::Text("%s", rightLabel);
        ImGui::SameLine(rightValueX); ImGui::Text("%s", rightValue.c_str());
    };
    summaryPair("Jitter", summary_.jitterMs > 0.0 ? fmt(summary_.jitterMs, 2) : "--",
                "Ping Avg", summary_.pingCount > 0 ? fmt(summary_.pingAvgMs, 1) : "--");
    summaryPair("Loss", summary_.packetsTotal > 0 ? fmt(summary_.lossPct, 1) : "--",
                "Ping Min", summary_.pingCount > 0 ? fmt(summary_.pingMinMs, 1) : "--");
    summaryPair("Pkts", summary_.packetsTotal > 0 ? std::to_string(summary_.packetsTotal) : "--",
                "Ping Max", summary_.pingCount > 0 ? fmt(summary_.pingMaxMs, 1) : "--");
    summaryPair("Ref Pkts", refPkts, "Diff", diff);
    ImGui::SameLine(rightValueX + 36.0f);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Diff = PCAP Captured - iperf reference packets");
        ImGui::Text("Mode reference: %s", cfg_.serverMode ? "receiver summary" : "sender summary");
        ImGui::Text("Sender: %s", summary_.pcapSenderPackets >= 0 ? std::to_string(summary_.pcapSenderPackets).c_str() : "--");
        ImGui::Text("Receiver: %s", summary_.pcapReceiverPackets >= 0 ? std::to_string(summary_.pcapReceiverPackets).c_str() : "--");
        ImGui::EndTooltip();
    }
    ImGui::EndChild();
}

void App::drawLogAndCharts() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float iperfWidth = (avail.x - spacing) * 0.60f;
    float topHeight = 256.0f;
    float graphHeight = (avail.y - topHeight - (spacing * 2.0f)) * 0.47f;
    float lastGraphHeight = avail.y - topHeight - graphHeight - (spacing * 2.0f);
    ImGui::BeginChild("top", ImVec2(0, topHeight), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("ipterm", ImVec2(iperfWidth, 0), false, ImGuiWindowFlags_NoScrollbar); logPanel("iplog", "iperf Output", iperfLog_, g_monoFont); ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("pingterm", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar); logPanel("pinglog", "Ping Output", pingLog_, g_monoFont); ImGui::EndChild();
    ImGui::EndChild();

    if (ImGui::BeginChild("ipchart", ImVec2(0, graphHeight), true, ImGuiWindowFlags_NoScrollbar)) {
        panelHeader("iperf Throughput");
        if (ImPlot::BeginPlot("##iperf_plot", ImVec2(-1, -1), ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
            if (!iperfPoints_.empty()) {
                std::vector<double> xs(iperfPoints_.size()), ys(iperfPoints_.size());
                for (size_t i = 0; i < iperfPoints_.size(); ++i) { xs[i] = (double)(i + 1); ys[i] = iperfPoints_[i]; }
                double yMax = *std::max_element(ys.begin(), ys.end());
                int count = (int)ys.size();
                ImPlot::SetupAxes("Sample", "Mbps");
                ImPlot::SetupAxisLimits(ImAxis_X1, count > 80 ? count - 79.0 : 1.0, std::max(80.0, (double)count), ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, std::max(10.0, yMax * 1.15), ImGuiCond_Always);
                ImPlot::PlotLine("iperf", xs.data(), ys.data(), count);
            }
            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("pingchart", ImVec2(0, lastGraphHeight), true, ImGuiWindowFlags_NoScrollbar)) {
        panelHeader("Ping RTT");
        if (ImPlot::BeginPlot("##ping_plot", ImVec2(-1, -1), ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
            if (!pingPoints_.empty()) {
                std::vector<double> ys(pingPoints_.size()); for (size_t i = 0; i < pingPoints_.size(); ++i) ys[i] = pingPoints_[i];
                double yMax = *std::max_element(ys.begin(), ys.end()); int count = (int)pingXs_.size(); double yAxisMax = std::max(10.0, yMax * 1.15);
                ImPlot::SetupAxes("Sample", "ms");
                ImPlot::SetupAxisLimits(ImAxis_X1, count > 80 ? count - 79.0 : 1.0, std::max(80.0, (double)count), ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, yAxisMax, ImGuiCond_Always);
                ImPlot::PlotLine("ping", pingXs_.data(), ys.data(), (int)ys.size());
                if (!pingTimeoutXs_.empty()) {
                    std::vector<double> timeoutYs(pingTimeoutXs_.size(), yAxisMax * 0.92);
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 6.0f, ImVec4(0.95f, 0.26f, 0.21f, 1.0f), 1.5f, ImVec4(0.95f, 0.26f, 0.21f, 1.0f));
                    ImPlot::PlotScatter("timeout", pingTimeoutXs_.data(), timeoutYs.data(), (int)pingTimeoutXs_.size());
                }
            }
            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
}

void App::drainWorkerOutputs() {
    IperfWorkerOut ipOut;
    while (iperfWorker_.output().try_pop(ipOut)) {
        switch (ipOut.type) {
        case IperfWorkerOut::Type::Line:
        case IperfWorkerOut::Type::Error:
            appendLog(iperfLog_, ipOut.line);
            break;
        case IperfWorkerOut::Type::Interval:
            summary_.throughputMbps = ipOut.interval.throughput_mbps;
            summary_.jitterMs = ipOut.interval.jitter_ms;
            if (ipOut.interval.is_udp) {
                summary_.packetsTotal += ipOut.interval.packets_total;
                summary_.packetsLost += ipOut.interval.packets_lost;
                summary_.lossPct = summary_.packetsTotal > 0 ? (double)summary_.packetsLost * 100.0 / (double)summary_.packetsTotal : 0.0;
            }
            iperfPoints_.push_back((float)ipOut.interval.throughput_mbps);
            break;
        case IperfWorkerOut::Type::FinalResult:
            summary_.throughputMbps = ipOut.interval.throughput_mbps;
            summary_.jitterMs = ipOut.interval.jitter_ms;
            summary_.lossPct = ipOut.interval.loss_pct;
            if (ipOut.interval.is_udp) {
                if ((!cfg_.serverMode && ipOut.interval.is_sender_summary) || (cfg_.serverMode && ipOut.interval.is_receiver_summary)) {
                    summary_.packetsTotal = ipOut.interval.packets_total;
                    summary_.packetsLost = ipOut.interval.packets_lost;
                }
                if (ipOut.interval.is_sender_summary) summary_.pcapSenderPackets = ipOut.interval.packets_total;
                if (ipOut.interval.is_receiver_summary) summary_.pcapReceiverPackets = ipOut.interval.packets_total;
                if (cfg_.serverMode) {
                    if (ipOut.interval.is_receiver_summary || summary_.pcapIperfPackets < 0) summary_.pcapIperfPackets = ipOut.interval.packets_total;
                } else {
                    if (ipOut.interval.is_sender_summary || summary_.pcapIperfPackets < 0) summary_.pcapIperfPackets = ipOut.interval.packets_total;
                }
                if (cfg_.serverMode && ipOut.interval.is_receiver_summary && pcapWorker_.running()) {
                    serverPcapStopPending_ = true;
                    serverPcapStopDeadlineTick_ = GetTickCount64() + 1200;
                }
            }
            break;
        case IperfWorkerOut::Type::NewConnection:
            appendLog(iperfLog_, "[info] New iperf connection");
            if (cfg_.serverMode) {
                resetIperfUi();
                resetPcapUi();
                serverPcapActiveTest_ = true;
                serverPcapStopPending_ = false;
                if (cfg_.pcapEnabled && !pcapWorker_.running() && !pcapInterfaces_.empty() && cfg_.pcapInterfaceIndex < (int)pcapInterfaces_.size()) {
                    startPcapCapture(0, false, false);
                }
            }
            break;
        case IperfWorkerOut::Type::Done:
            iperfStatus_ = "Stopped";
            if (pcapWorker_.running() && !cfg_.serverMode) {
                clientPcapStopPending_ = true;
                clientPcapStopDeadlineTick_ = GetTickCount64() + 2500;
            } else if (pcapWorker_.running() && cfg_.serverMode) {
                serverPcapStopPending_ = true;
                serverPcapStopDeadlineTick_ = GetTickCount64() + 1200;
            }
            break;
        }
    }

    PingWorkerOut pingOut;
    while (pingWorker_.output().try_pop(pingOut)) {
        switch (pingOut.type) {
        case PingWorkerOut::Type::Line:
        case PingWorkerOut::Type::Error:
            appendLog(pingLog_, pingOut.line);
            break;
        case PingWorkerOut::Type::Sample:
            ++pingSampleCounter_;
            pingXs_.push_back((double)pingSampleCounter_);
            if (pingOut.sample.timed_out) {
                pingPoints_.push_back(0.0f);
                pingTimeoutXs_.push_back((double)pingSampleCounter_);
            } else {
                pingPoints_.push_back((float)pingOut.sample.rtt_ms);
                if (summary_.pingCount == 0) {
                    summary_.pingMinMs = summary_.pingAvgMs = summary_.pingMaxMs = pingOut.sample.rtt_ms;
                } else {
                    summary_.pingMinMs = std::min(summary_.pingMinMs, pingOut.sample.rtt_ms);
                    summary_.pingMaxMs = std::max(summary_.pingMaxMs, pingOut.sample.rtt_ms);
                    summary_.pingAvgMs = ((summary_.pingAvgMs * summary_.pingCount) + pingOut.sample.rtt_ms) / (summary_.pingCount + 1);
                }
                ++summary_.pingCount;
            }
            break;
        case PingWorkerOut::Type::Done:
            pingStatus_ = "Stopped";
            break;
        }
    }

    PcapWorkerOut pcapOut;
    while (pcapWorker_.output().try_pop(pcapOut)) {
        switch (pcapOut.type) {
        case PcapWorkerOut::Type::Ready:
            pcapWaitingForReady_ = false;
            if (pendingIperfLaunch_) launchIperfNow();
            break;
        case PcapWorkerOut::Type::PacketCount:
            if (!cfg_.serverMode || serverPcapActiveTest_) {
                summary_.pcapCaptured = std::max(summary_.pcapCaptured, std::max(0, pcapOut.packet_count));
            }
            break;
        case PcapWorkerOut::Type::Error:
            pcapWaitingForReady_ = false;
            appendLog(iperfLog_, "[pcap] " + pcapOut.line);
            if (pendingIperfLaunch_) launchIperfNow();
            break;
        case PcapWorkerOut::Type::Done:
            pcapWaitingForReady_ = false;
            if (cfg_.serverMode && iperfStatus_ == "Running" && cfg_.pcapEnabled && !pendingIperfLaunch_
                && !pcapInterfaces_.empty() && cfg_.pcapInterfaceIndex < (int)pcapInterfaces_.size()) {
                serverPcapActiveTest_ = false;
                startPcapCapture(0, false, false);
            }
            break;
        }
    }

    if (clientPcapStopPending_ && pcapWorker_.running() && GetTickCount64() >= clientPcapStopDeadlineTick_) {
        clientPcapStopPending_ = false;
        pcapWorker_.requestStop();
    }
    if (serverPcapStopPending_ && pcapWorker_.running() && GetTickCount64() >= serverPcapStopDeadlineTick_) {
        serverPcapStopPending_ = false;
        pcapWorker_.requestStop();
    }
}

void App::startIperf() {
    if (!cfg_.serverMode && cfg_.targetIp.empty()) { iperfStatus_ = "Error"; return; }
    if (cfg_.pcapEnabled && pcapInterfaces_.empty()) refreshPcapInterfaces();
    pendingIperfCmd_ = buildIperfCommand();
    if (pendingIperfCmd_.empty()) { iperfStatus_ = "Error"; return; }
    pendingIperfUseV3_ = cfg_.useIperf3;
    pendingIperfLaunch_ = false;
    resetIperfUi();
    appendLog(iperfLog_, "$ " + pendingIperfCmd_);
    iperfStatus_ = "Running";
    serverPcapActiveTest_ = false;
    if (cfg_.pcapEnabled && !pcapInterfaces_.empty() && cfg_.pcapInterfaceIndex < (int)pcapInterfaces_.size()) {
        startPcapCapture(cfg_.serverMode ? 0 : cfg_.duration, true, !cfg_.serverMode);
        return;
    }
    launchIperfNow();
}

void App::stopIperf() {
    iperfWorker_.stop();
    pcapWorker_.stop();
    pendingIperfLaunch_ = false;
    clientPcapStopPending_ = false;
    serverPcapStopPending_ = false;
    serverPcapActiveTest_ = false;
    iperfStatus_ = "Stopped";
}

void App::startPing() {
    const std::string target = cfg_.pingTarget.empty() ? cfg_.targetIp : cfg_.pingTarget;
    if (target.empty()) { pingStatus_ = "Error"; return; }
    resetPingUi();
    appendLog(pingLog_, "[Ping] -> " + target);
    pingWorker_.start(target, true, 4);
    pingStatus_ = "Running";
}

void App::stopPing() { pingWorker_.stop(); pingStatus_ = "Stopped"; }

void App::startPcapCapture(int expectedDurationSec, bool launchIperfAfterReady, bool resetUi) {
    if (resetUi) resetPcapUi();
    pendingIperfLaunch_ = false;
    pcapWaitingForReady_ = false;
    pcapReadyWaitStartTick_ = GetTickCount64();
    clientPcapStopPending_ = false;
    serverPcapStopPending_ = false;
    pcapWorker_.start(tsharkPath_, pcapInterfaces_[cfg_.pcapInterfaceIndex].index, cfg_.pcapFilter, expectedDurationSec);
    if (launchIperfAfterReady) launchIperfNow();
}

void App::launchIperfNow() {
    pendingIperfLaunch_ = false;
    pcapWaitingForReady_ = false;
    iperfWorker_.start(pendingIperfCmd_, pendingIperfUseV3_);
}

void App::resetIperfUi() {
    iperfPoints_.clear();
    iperfLog_.clear();
    summary_.throughputMbps = 0.0;
    summary_.jitterMs = 0.0;
    summary_.lossPct = 0.0;
    summary_.packetsTotal = 0;
    summary_.packetsLost = 0;
    summary_.pcapIperfPackets = -1;
    summary_.pcapSenderPackets = -1;
    summary_.pcapReceiverPackets = -1;
}

void App::resetPingUi() {
    pingXs_.clear(); pingPoints_.clear(); pingTimeoutXs_.clear(); pingLog_.clear(); pingSampleCounter_ = 0;
    summary_.pingMinMs = 0.0; summary_.pingAvgMs = 0.0; summary_.pingMaxMs = 0.0; summary_.pingCount = 0;
}

void App::resetPcapUi() {
    summary_.pcapCaptured = 0;
    summary_.pcapIperfPackets = -1;
    summary_.pcapSenderPackets = -1;
    summary_.pcapReceiverPackets = -1;
}

void App::appendLog(std::deque<std::string>& log, const std::string& line) {
    if (line.empty()) return;
    log.push_back(line);
    while (log.size() > 500) log.pop_front();
}

void App::refreshPcapInterfaces() {
    tsharkPath_ = findTshark();
    pcapInterfaces_ = listTsharkInterfaces(tsharkPath_);
    resolvedInterfaceCacheIndex_ = -2;
    resolvedInterfaceCacheSize_ = 0;
    resolvedInterfaceCacheIp_.clear();
    if (!pcapInterfaces_.empty()) {
        pcapInterfaceError_.clear();
        if (cfg_.pcapInterfaceIndex >= (int)pcapInterfaces_.size()) cfg_.pcapInterfaceIndex = 0;
        return;
    }
    if (tsharkPath_.empty()) {
        pcapInterfaceError_ = "Wireshark/tshark bulunamadi";
    } else {
        pcapInterfaceError_ = "Gercek capture adaptoru bulunamadi. Bu makinede Npcap eksik/devre disi olabilir; sadece ETW gibi yardimci kaynaklar gorunuyor olabilir.";
    }
}

void App::initDiscovery() {
    WSADATA wsa{};
    winsockReady_ = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
}

void App::shutdownDiscovery() {
    if (discoverySock_ != INVALID_SOCKET) {
        closesocket(discoverySock_);
        discoverySock_ = INVALID_SOCKET;
    }
    if (winsockReady_) {
        WSACleanup();
        winsockReady_ = false;
    }
}

std::string App::resolveSelectedInterfaceIpv4() const {
    if (cfg_.pcapInterfaceIndex < 0 || cfg_.pcapInterfaceIndex >= (int)pcapInterfaces_.size()) return {};
    if (resolvedInterfaceCacheIndex_ == cfg_.pcapInterfaceIndex && resolvedInterfaceCacheSize_ == pcapInterfaces_.size()) {
        return resolvedInterfaceCacheIp_;
    }
    const auto& iface = pcapInterfaces_[cfg_.pcapInterfaceIndex];
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) {
        resolvedInterfaceCacheIndex_ = cfg_.pcapInterfaceIndex;
        resolvedInterfaceCacheSize_ = pcapInterfaces_.size();
        resolvedInterfaceCacheIp_.clear();
        return {};
    }
    std::vector<unsigned char> buffer(size);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size) != NO_ERROR) {
        resolvedInterfaceCacheIndex_ = cfg_.pcapInterfaceIndex;
        resolvedInterfaceCacheSize_ = pcapInterfaces_.size();
        resolvedInterfaceCacheIp_.clear();
        return {};
    }

    auto matches = [&](const IP_ADAPTER_ADDRESSES* aa) {
        const std::string friendly = wideToUtf8(aa->FriendlyName);
        const std::string descr = wideToUtf8(aa->Description);
        return (!friendly.empty() && (iface.description == friendly || iface.raw_description.find(friendly) != std::string::npos))
            || (!descr.empty() && (iface.description == descr || iface.raw_description.find(descr) != std::string::npos));
    };

    for (auto* aa = addrs; aa; aa = aa->Next) {
        if (!matches(aa)) continue;
        for (auto* ua = aa->FirstUnicastAddress; ua; ua = ua->Next) {
            const std::string ip = sockaddrToIpv4String(ua->Address.lpSockaddr);
            if (!ip.empty() && ip != "127.0.0.1") {
                resolvedInterfaceCacheIndex_ = cfg_.pcapInterfaceIndex;
                resolvedInterfaceCacheSize_ = pcapInterfaces_.size();
                resolvedInterfaceCacheIp_ = ip;
                return ip;
            }
        }
    }
    resolvedInterfaceCacheIndex_ = cfg_.pcapInterfaceIndex;
    resolvedInterfaceCacheSize_ = pcapInterfaces_.size();
    resolvedInterfaceCacheIp_.clear();
    return {};
}

void App::refreshDiscoverySocket() {
    if (!winsockReady_) return;
    const bool wantServer = cfg_.serverMode;
    const std::string bindIp = resolveSelectedInterfaceIpv4();
    if (discoverySock_ != INVALID_SOCKET && discoveryBoundIp_ == bindIp && discoveryBoundAsServer_ == wantServer) return;

    if (discoverySock_ != INVALID_SOCKET) {
        closesocket(discoverySock_);
        discoverySock_ = INVALID_SOCKET;
    }

    discoveryBoundIp_ = bindIp;
    discoveryBoundAsServer_ = wantServer;
    discoveryLastRequestTick_ = 0;
    discoveryLastAnnounceTick_ = 0;

    discoverySock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (discoverySock_ == INVALID_SOCKET) return;

    u_long nonBlocking = 1;
    ioctlsocket(discoverySock_, FIONBIO, &nonBlocking);
    BOOL broadcast = TRUE;
    setsockopt(discoverySock_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(wantServer ? kDiscoveryPort : 0);
    local.sin_addr.s_addr = bindIp.empty() ? htonl(INADDR_ANY) : inet_addr(bindIp.c_str());
    if (bind(discoverySock_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        closesocket(discoverySock_);
        discoverySock_ = INVALID_SOCKET;
    }
}

void App::tickDiscovery() {
    refreshDiscoverySocket();
    if (!winsockReady_ || discoverySock_ == INVALID_SOCKET) return;

    const ULONGLONG now = GetTickCount64();
    if (!cfg_.serverMode && cfg_.targetIp.empty() && now - discoveryLastRequestTick_ >= 1500) {
        discoveryLastRequestTick_ = now;
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(kDiscoveryPort);
        dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(discoverySock_, kDiscoveryQuery, (int)std::strlen(kDiscoveryQuery), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    }

    for (;;) {
        sockaddr_in remote{};
        int remoteLen = sizeof(remote);
        char buffer[256] = {};
        const int rc = recvfrom(discoverySock_, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&remote), &remoteLen);
        if (rc <= 0) break;
        buffer[rc] = '\0';
        const std::string payload = trimCopy(buffer);
        if (cfg_.serverMode) {
            if (payload == kDiscoveryQuery && iperfStatus_ == "Running") {
                const std::string replyIp = resolveSelectedInterfaceIpv4();
                const std::string body = std::string(kDiscoveryReplyPrefix) + replyIp;
                sendto(discoverySock_, body.c_str(), (int)body.size(), 0, reinterpret_cast<sockaddr*>(&remote), remoteLen);
            }
        } else if (payload.rfind(kDiscoveryReplyPrefix, 0) == 0 && cfg_.targetIp.empty()) {
            const std::string discovered = trimCopy(payload.substr(std::strlen(kDiscoveryReplyPrefix)));
            const std::string senderIp = sockaddrToIpv4String(reinterpret_cast<sockaddr*>(&remote));
            const std::string chosenIp = discovered.empty() ? senderIp : discovered;
            if (!chosenIp.empty() && chosenIp != lastDiscoveredIp_) {
                cfg_.targetIp = chosenIp;
                lastDiscoveredIp_ = chosenIp;
                appendLog(iperfLog_, "[discovery] Server IP auto-filled: " + chosenIp);
            }
        }
    }
}

void App::applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.FramePadding = ImVec2(4.0f, 2.0f);
    style.ItemSpacing = ImVec2(4.0f, 3.0f);
    style.WindowPadding = ImVec2(4.0f, 4.0f);
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.07f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.06f, 0.07f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.28f, 0.33f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.22f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.28f, 0.43f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.31f, 0.47f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.25f, 0.39f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.25f, 0.39f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.25f, 0.39f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.31f, 0.47f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.28f, 0.43f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.25f, 0.39f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.31f, 0.47f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.28f, 0.43f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.68f, 0.70f, 0.74f, 1.0f);
}

void App::syncPortAndFilter() {
    if (cfg_.port < 1) cfg_.port = cfg_.useIperf3 ? 5201 : 5001;
    cfg_.pcapFilter = std::string(cfg_.udp ? "udp" : "tcp") + " port " + std::to_string(cfg_.port);
}

std::string App::findIperfBinary() const {
    fs::path exeDir = executableDir();
    fs::path path = exeDir / "bin" / (cfg_.useIperf3 ? "iperf3" : "iperf2") / (cfg_.useIperf3 ? "iperf3.exe" : "iperf.exe");
    return fs::exists(path) ? path.string() : std::string{};
}

std::string App::buildIperfCommand() const {
    const std::string binary = findIperfBinary();
    if (binary.empty()) return {};
    IperfCommandBuilder builder(binary, cfg_.useIperf3);
    if (cfg_.serverMode) builder.serverMode(); else builder.clientMode(cfg_.targetIp);
    builder.port(cfg_.port); builder.interval(cfg_.interval); builder.parallel(cfg_.parallel);
    if (!cfg_.serverMode) builder.duration(cfg_.duration);
    if (cfg_.udp) {
        if (!cfg_.serverMode || !cfg_.useIperf3) builder.udp();
        if (!cfg_.bandwidthMbps.empty()) builder.bandwidth(cfg_.bandwidthMbps + "M");
    } else if (cfg_.useIperf3 && !cfg_.bandwidthMbps.empty()) {
        builder.bandwidth(cfg_.bandwidthMbps + "M");
    }
    if (!cfg_.packetLength.empty()) builder.packetLength(cfg_.packetLength);
    if (!cfg_.bindAddr.empty()) builder.bindAddress(cfg_.bindAddr);
    if (cfg_.bidirectional) builder.bidirectional();
    return builder.build();
}

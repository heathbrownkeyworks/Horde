// Desktop renderer for the exact screen code used by Horde's native UI.
// --shot creates a hidden window and writes a PNG without starting Skyrim.
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "ui/imgui/HordeScreen.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
using Microsoft::WRL::ComPtr;
using namespace Horde::ImGuiUI;
struct Options
{
    int width = 1920, height = 1080, frames = 3;
    std::string shot, screen = "main", fixture, variant, focus, glyphs = "xbox";
    bool controller = false, cursor = false;
};
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<IDXGISwapChain> swapchain;
ComPtr<ID3D11RenderTargetView> target;

void Error(const char *what, HRESULT code)
{
    std::fprintf(stderr, "imgui-preview: %s (HRESULT 0x%08lx)\n", what, static_cast<unsigned long>(code));
}
bool Parse(int argc, char **argv, Options &out)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--controller")
        {
            out.controller = true;
            continue;
        }
        if (arg == "--cursor")
        {
            out.cursor = out.controller = true;
            continue;
        }
        if (i + 1 >= argc)
        {
            std::fprintf(stderr, "imgui-preview: missing value for %s\n", arg.c_str());
            return false;
        }
        const std::string value = argv[++i];
        if (arg == "--shot")
            out.shot = value;
        else if (arg == "--screen")
            out.screen = value;
        else if (arg == "--fixture")
            out.fixture = value;
        else if (arg == "--variant")
            out.variant = value;
        else if (arg == "--focus")
            out.focus = value;
        else if (arg == "--glyphs" && (value == "xbox" || value == "playstation" || value == "generic"))
            out.glyphs = value;
        else if (arg == "--frames")
            out.frames = std::max(1, std::atoi(value.c_str()));
        else if (arg == "--size")
        {
            char *separator = nullptr;
            const long width = std::strtol(value.c_str(), &separator, 10);
            char *end = nullptr;
            const long height = separator && *separator == 'x' ? std::strtol(separator + 1, &end, 10) : 0;
            if (!end || *end || width < 320 || height < 240 || width > 16384 || height > 16384)
            {
                std::fprintf(stderr, "imgui-preview: invalid size %s\n", value.c_str());
                return false;
            }
            out.width = static_cast<int>(width);
            out.height = static_cast<int>(height);
        }
        else
        {
            std::fprintf(stderr, "imgui-preview: unknown option %s\n", arg.c_str());
            return false;
        }
    }
    if (!out.shot.empty() && !std::filesystem::path(out.shot).is_absolute())
    {
        std::fprintf(stderr, "imgui-preview: --shot needs an absolute PNG path\n");
        return false;
    }
    return true;
}
bool LoadFixture(const std::string &path, Model &model)
{
    if (path.empty())
    {
        model = Model::object();
        return true;
    }
    std::ifstream input(path);
    if (!input)
    {
        std::fprintf(stderr, "imgui-preview: cannot open fixture %s\n", path.c_str());
        return false;
    }
    try
    {
        input >> model;
        if (!model.is_object())
        {
            std::fprintf(stderr, "imgui-preview: fixture root must be a JSON object\n");
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "imgui-preview: invalid fixture: %s\n", e.what());
        return false;
    }
}
bool SetScreen(const std::string &name, ScreenState &state)
{
    if (name == "main")
        return true;
    if (name == "dismissed")
    {
        state.dismissed = true;
        state.section = Section::Commands;
        return true;
    }
    if (name == "modal")
    {
        Confirm(state, "Confirm Dismiss", "Dismiss Lydia from your horde? They will return to their home.", "Dismiss",
                {"hordeDismiss", {{"formID", 1001}}});
        return true;
    }
    std::fprintf(stderr, "Unknown screen: %s\n", name.c_str());
    return false;
}
bool SetVariant(const std::string &variant, ScreenState &, const Model &)
{
    return variant.empty();
}
bool CreateTarget()
{
    ComPtr<ID3D11Texture2D> buffer;
    HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(hr))
    {
        Error("GetBuffer failed", hr);
        return false;
    }
    hr = device->CreateRenderTargetView(buffer.Get(), nullptr, &target);
    if (FAILED(hr))
        Error("CreateRenderTargetView failed", hr);
    return SUCCEEDED(hr);
}
bool CreateDevice(HWND window, int width, int height)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selected{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &desc, &swapchain, &device, &selected, &context);
    if (FAILED(hr))
    {
        Error("D3D11CreateDeviceAndSwapChain failed", hr);
        return false;
    }
    return CreateTarget();
}
bool SavePng(const std::string &path)
{
    ComPtr<ID3D11Texture2D> buffer, staging;
    HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(hr))
    {
        Error("capture GetBuffer failed", hr);
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        std::fprintf(stderr, "imgui-preview: unexpected backbuffer format %u\n", desc.Format);
        return false;
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr))
    {
        Error("capture staging texture failed", hr);
        return false;
    }
    context->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        Error("capture Map failed", hr);
        return false;
    }
    if (!mapped.pData || mapped.RowPitch < desc.Width * 4)
    {
        context->Unmap(staging.Get(), 0);
        std::fprintf(stderr, "imgui-preview: invalid mapped row pitch\n");
        return false;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
    {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * desc.Width * 4,
                    static_cast<const unsigned char *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(desc.Width) * 4);
    }
    context->Unmap(staging.Get(), 0);
    const int ok = stbi_write_png(path.c_str(), static_cast<int>(desc.Width), static_cast<int>(desc.Height), 4,
                                  pixels.data(), static_cast<int>(desc.Width) * 4);
    if (!ok)
        std::fprintf(stderr, "imgui-preview: stb_image_write failed for %s\n", path.c_str());
    return ok != 0;
}
LRESULT WINAPI WndProc(HWND window, UINT msg, WPARAM w, LPARAM l)
{
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, msg, w, l))
        return 1;
    if (msg == WM_SIZE && device && w != SIZE_MINIMIZED)
    {
        target.Reset();
        HRESULT hr = swapchain->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
            Error("ResizeBuffers failed", hr);
        else
            CreateTarget();
        return 0;
    }
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, w, l);
}
} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!Parse(argc, argv, options))
        return 2;
    Model model;
    if (!LoadFixture(options.fixture, model))
        return 2;
    ScreenState screen;
    if (!SetScreen(options.screen, screen))
        return 2;
    if (!SetVariant(options.variant, screen, model))
        return 2;
    if (!options.focus.empty() && options.focus != "keyboard" && options.focus != "controller")
    {
        std::fprintf(stderr, "imgui-preview: --focus expects keyboard or controller\n");
        return 2;
    }
    WNDCLASSEXW cls{
        sizeof(cls), CS_CLASSDC,           WndProc, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr,
        nullptr,     L"HordeImguiPreview", nullptr};
    if (!RegisterClassExW(&cls))
    {
        std::fprintf(stderr, "imgui-preview: RegisterClassExW failed\n");
        return 3;
    }
    RECT bounds{0, 0, options.width, options.height};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window =
        CreateWindowW(cls.lpszClassName, L"Horde ImGui Preview", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                      bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window)
    {
        std::fprintf(stderr, "imgui-preview: CreateWindowW failed\n");
        UnregisterClassW(cls.lpszClassName, cls.hInstance);
        return 3;
    }
    // Screenshot mode never shows a window. Interactive mode is explicit by omitting --shot.
    int result = 0;
    if (!CreateDevice(window, options.width, options.height))
        result = 3;
    else
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (options.controller)
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        if (options.focus == "controller")
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        Fonts fonts;
        if (!LoadFonts(io, "assets/fonts", fonts))
            std::fprintf(stderr, "imgui-preview: warning: one or more Horde fonts missing\n");
        if (!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(device.Get(), context.Get()))
        {
            std::fprintf(stderr, "imgui-preview: ImGui backend initialization failed\n");
            result = 3;
        }
        else
        {
            if (options.shot.empty())
            {
                ShowWindow(window, SW_SHOWDEFAULT);
                UpdateWindow(window);
            }
            bool running = true;
            int frame = 0;
            model["_usingGamepad"] = options.controller || options.focus == "controller";
            model["_controllerCursor"] = options.cursor;
            model["_controllerGlyphs"] = options.glyphs;
            while (running)
            {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    if (msg.message == WM_QUIT)
                        running = false;
                }
                if (!running)
                    break;
                if (!target)
                {
                    result = 4;
                    break;
                }
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                // A hidden Win32 window may be capped to the monitor's work area.
                // Capture against the requested backbuffer, including ultrawide sizes.
                if (!options.shot.empty())
                    io.DisplaySize = {static_cast<float>(options.width), static_cast<float>(options.height)};
                // The hidden desktop window has no physical gamepad; advertise the
                // synthetic source after Win32's device poll for this capture frame.
                if (options.focus == "controller")
                    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
                if (!options.focus.empty() && frame == 1)
                    io.AddKeyEvent(options.focus == "controller" ? ImGuiKey_GamepadDpadDown : ImGuiKey_Tab, true);
                if (!options.focus.empty() && frame == 2)
                    io.AddKeyEvent(options.focus == "controller" ? ImGuiKey_GamepadDpadDown : ImGuiKey_Tab, false);
                ImGui::NewFrame();
                const auto actions = DrawHorde(model, screen, fonts, true);
                for (const auto &action : actions.actions)
                {
                    if (action.name == "hordeClose")
                        running = false;
                    std::fprintf(stdout, "%s %s\n", action.name.c_str(), action.data.dump().c_str());
                }
                ImGui::Render();
                const float clear[] = {26 / 255.0f, 19 / 255.0f, 12 / 255.0f, 1.0f};
                ID3D11RenderTargetView *rt = target.Get();
                context->OMSetRenderTargets(1, &rt, nullptr);
                context->ClearRenderTargetView(rt, clear);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                if (!options.shot.empty() && ++frame >= options.frames)
                {
                    if (!SavePng(options.shot))
                        result = 5;
                    break;
                }
                HRESULT hr = swapchain->Present(1, 0);
                if (FAILED(hr))
                {
                    Error("Present failed", hr);
                    result = 4;
                    break;
                }
            }
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext();
    }
    target.Reset();
    swapchain.Reset();
    context.Reset();
    device.Reset();
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    return result;
}

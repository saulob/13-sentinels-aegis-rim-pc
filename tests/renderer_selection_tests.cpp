// Renderer selection tests: the Renderer setting, Auto's fallback, the OpenGL requirements and the places that offer
// the choice. Pure logic plus the sources that must agree with it: no window, no OpenGL or Vulkan context, no game files.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "tsr_options_menu.h"
#include "tsr_renderer.h"
#include "video_core/renderer_opengl/gl_capabilities.h"
#include "video_core/renderer_opengl/gl_primitives.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                                       \
    do {                                                                                                       \
        ++g_checks;                                                                                            \
        if (!(condition)) {                                                                                    \
            ++g_failures;                                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                      \
    } while (0)

std::string ReadAll(const std::string& relative) {
    std::ifstream file(std::string(TSR_SOURCE_DIR) + "/" + relative, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void TestParse() {
    using namespace TsrRenderer;
    CHECK(ParseChoice("vulkan") == Vulkan);
    CHECK(ParseChoice("auto") == Auto);
    CHECK(ParseChoice("opengl") == TsrRenderer::OpenGL);
    CHECK(ParseChoice("") == Vulkan);
    CHECK(ParseChoice("OpenGL") == Vulkan);
    CHECK(ParseChoice("direct3d") == Vulkan);
}

void TestResolve() {
    using namespace TsrRenderer;
    CHECK(Resolve(Vulkan, true, "").backend == Backend::Vulkan);
    CHECK(Resolve(TsrRenderer::OpenGL, true, "").backend == Backend::OpenGL);
    CHECK(Resolve(TsrRenderer::OpenGL, true, "").fallback_reason.empty());
    CHECK(Resolve(Auto, true, "").backend == Backend::Vulkan);
    CHECK(Resolve(Auto, true, "").fallback_reason.empty());

    const auto fallback = Resolve(Auto, false, "No Vulkan graphics driver was found.");
    CHECK(fallback.backend == Backend::OpenGL);
    CHECK(fallback.fallback_reason == "No Vulkan graphics driver was found.");

    // A renderer chosen by name is never replaced, working or not.
    const auto explicit_vulkan = Resolve(Vulkan, false, "No Vulkan graphics driver was found.");
    CHECK(explicit_vulkan.backend == Backend::Vulkan);
    CHECK(explicit_vulkan.fallback_reason.empty());
    CHECK(Resolve(TsrRenderer::OpenGL, false, "anything").backend == Backend::OpenGL);
    CHECK(Resolve(TsrRenderer::OpenGL, false, "anything").fallback_reason.empty());
}

void TestDirect3D11() {
    using namespace TsrRenderer;
    CHECK(ParseChoice("d3d11", true) == Direct3D11);
    CHECK(ParseChoice("d3d11", false) == Vulkan);
    CHECK(ParseChoice("D3D11", true) == Vulkan);
    CHECK(ParseChoice("direct3d11", true) == Vulkan);
    CHECK(ChoiceKeys[Direct3D11] == "d3d11");
    CHECK(std::string(BackendName(Backend::Direct3D11)) == "Direct3D 11 via ANGLE");

    const auto named = Resolve(Direct3D11, false, "No Vulkan graphics driver was found.");
    CHECK(named.backend == Backend::Direct3D11);
    CHECK(named.fallback_reason.empty());
    CHECK(Resolve(Direct3D11, true, "").backend == Backend::Direct3D11);
    CHECK(Resolve(Auto, false, "anything").backend == Backend::OpenGL);
    CHECK(Resolve(Auto, true, "").backend == Backend::Vulkan);
}

void TestDirect3D11Windows() {
    const std::string settings = ReadAll("launcher/settings.h");
    const size_t entry = settings.find("{\"d3d11\", \"Direct3D 11 (experimental)\"}");
    CHECK(entry != std::string::npos);
    CHECK(settings.rfind("#ifdef _WIN32", entry) != std::string::npos);
    CHECK(settings.find("#endif", entry) != std::string::npos);
    const std::string menu = ReadAll("gpu/shim/tsr_options_menu.h");
    const size_t row = menu.find("{\"d3d11\", \"Direct3D 11 (experimental)\"}");
    CHECK(row != std::string::npos);
    CHECK(menu.rfind("#ifdef _WIN32", row) != std::string::npos);
    CHECK(menu.rfind("#ifdef _WIN32", row) > menu.rfind("#endif", row) ||
          menu.rfind("#endif", row) == std::string::npos);

    const std::string angle = ReadAll("gpu/shadps4/video_core/renderer_opengl/gl_angle.cpp");
    CHECK(angle.find("SDL_HINT_OPENGL_ES_DRIVER") != std::string::npos);
    CHECK(angle.find("SDL_HINT_VIDEO_EGL_ALLOW_GETDISPLAY_FALLBACK, \"0\"") != std::string::npos);
    CHECK(angle.find("PlatformAngleTypeD3D11") != std::string::npos);
    CHECK(angle.find("#ifdef _WIN32") != std::string::npos);
    CHECK(angle.find("LoadBesideExecutable(L\"libGLESv2.dll\")") != std::string::npos);
    CHECK(angle.find("LoadBesideExecutable(L\"libEGL.dll\")") != std::string::npos);
    CHECK(angle.find("13 Sentinels: Aegis Rim") != std::string::npos);

    // Native OpenGL keeps its own window path and never asks for ANGLE.
    const std::string window = ReadAll("gpu/shim/window.cpp");
    const size_t native = window.find("if (api == GraphicsApi::OpenGL) {");
    const size_t core = window.find("SDL_GL_CONTEXT_PROFILE_CORE", native);
    CHECK(native != std::string::npos && core != std::string::npos);
    CHECK(window.find("SDL_GL_CONTEXT_PROFILE_ES") == std::string::npos);
    CHECK(window.find("RequestAngleDirect3D11") < native);

    const std::string pin = ReadAll("gpu/angle-runtime.txt");
    CHECK(pin.find("clang64/bin/libGLESv2.dll") != std::string::npos);
    CHECK(pin.find("clang64/bin/libEGL.dll") != std::string::npos);
    const std::string build = ReadAll("tools/build.ps1");
    CHECK(build.find("Sync-AngleRuntime") != std::string::npos);
    CHECK(build.find("Install-GameRuntime $loader $angleRuntime") != std::string::npos);
    CHECK(ReadAll("tools/package.ps1").find("Read-AnglePin") != std::string::npos);
}

// Non-indexed rectangles keep the attribute path that was validated at run time.
void TestRectPathsStaySeparate() {
    using OpenGL::RectListUsesTables;
    CHECK(!RectListUsesTables(true, false, true));
    CHECK(!RectListUsesTables(true, false, false));
    CHECK(!RectListUsesTables(true, true, false));
    CHECK(!RectListUsesTables(false, true, true));
    CHECK(RectListUsesTables(true, true, true));

    const std::string rasterizer = ReadAll("gpu/shadps4/video_core/renderer_opengl/gl_rasterizer.cpp");
    CHECK(rasterizer.find("RectListUsesTables(rect_list, is_indexed, indexed_rect_tables)") != std::string::npos);
    CHECK(rasterizer.find("rect_tables ? PrepareIndexedRectangles(") != std::string::npos);
    CHECK(rasterizer.find("BindVertexInputs(*program, instance_offset, moved_vertices, rect_list)") != std::string::npos);
    CHECK(rasterizer.find("if (rect_list && is_indexed && !rect_tables) {") <
          rasterizer.find("AttachedTargets targets{};\n    if (!BindTargets(*program, targets)) {"));
    CHECK(rasterizer.find("Report(\"indexed rectangle lists are not implemented\");") != std::string::npos);
    size_t calls = 0;
    for (size_t at = rasterizer.find("PrepareIndexedRectangles("); at != std::string::npos;
         at = rasterizer.find("PrepareIndexedRectangles(", at + 1)) {
        ++calls;
    }
    CHECK(calls == 2);
}

void TestLauncherAgrees() {
    const std::string settings = ReadAll("launcher/settings.h");
    for (const auto key : TsrRenderer::ChoiceKeys) {
        CHECK(settings.find("{\"" + std::string(key) + "\", ") != std::string::npos);
    }
    CHECK(settings.find("kRendererKey[] = \"renderer\"") != std::string::npos);
    CHECK(settings.find("kDefaultRenderer[] = \"vulkan\"") != std::string::npos);
    CHECK(settings.find("\"OpenGL (experimental)\"") != std::string::npos);
    CHECK(ReadAll("launcher/settings.cpp").find("{kRendererKey, kDefaultRenderer}") != std::string::npos);

    const std::string shim = ReadAll("gpu/shim/tsr_settings.cpp");
    CHECK(shim.find("key == \"renderer\"") != std::string::npos);
    CHECK(shim.find("TsrRenderer::ParseChoice(value)") != std::string::npos);

    const std::string launcher = ReadAll("launcher/launcher.cpp");
    const size_t renderer = launcher.find("ui.Combo(\"Renderer\", kRendererHelp, kRendererKey, kDefaultRenderer");
    const size_t card = launcher.find("ui.Row(\"Graphics card\"");
    CHECK(renderer != std::string::npos && card != std::string::npos && renderer < card);
    CHECK(launcher.find("{\"Renderer\", \"Vulkan\"}") == std::string::npos);

    // Play checks Vulkan only as far as the saved renderer needs it.
    const std::string gate = ReadAll("gpu/shim/tsr_gpu_list.cpp");
    const size_t check = gate.find("extern \"C\" int tsr_check_renderer(");
    CHECK(check != std::string::npos);
    CHECK(gate.find("case TsrRenderer::OpenGL:", check) < gate.find("case TsrRenderer::Auto:", check));
    CHECK(gate.find("case TsrRenderer::Direct3D11:", check) < gate.find("case TsrRenderer::Auto:", check));
    CHECK(ReadAll("src/loader.c").find("tsr_check_renderer(renderer_settings_file") != std::string::npos);
}

// The in-game Options menu offers the same choices, saved for the next start.
void TestOptionsMenuRow() {
    using namespace TsrOptions;
    const Setting& setting = SettingOf(Id::Renderer);
    CHECK(std::string(setting.key) == "renderer");
    CHECK(setting.page == Page::Graphics);
    CHECK(setting.apply == Apply::Restart);
    CHECK(setting.feature == 0);
    const size_t expected = TsrRenderer::Direct3D11Available ? 4 : 3;
    CHECK(setting.choices.size() == expected);
    CHECK(setting.choices[0].value == "auto" && setting.choices[1].value == "vulkan" &&
          setting.choices[2].value == "opengl");
    const std::string settings = ReadAll("launcher/settings.h");
    for (const Choice& choice : setting.choices) {
        CHECK(TsrRenderer::ChoiceKeys[size_t(TsrRenderer::ParseChoice(choice.value))] == choice.value);
        CHECK(settings.find("{\"" + std::string(choice.value) + "\", \"" + std::string(choice.label) + "\"}") !=
              std::string::npos);
    }
    CHECK(std::string(PageNote(Page::Graphics)).find("Renderer") != std::string::npos);

    TsrSettings::Values values;
    CHECK(Current(values, Id::Renderer) == "vulkan");
    values.renderer = TsrRenderer::OpenGL;
    CHECK(Current(values, Id::Renderer) == "opengl");
    values.renderer = -4;
    CHECK(Current(values, Id::Renderer) == "vulkan");
}

// The game code stays out of renderer choices: only the factory and the window know both.
void TestBoundary() {
    const std::string liverpool = ReadAll("gpu/shadps4/video_core/amdgpu/liverpool.cpp");
    CHECK(liverpool.find("Vulkan::Rasterizer") == std::string::npos);
    CHECK(liverpool.find("OpenGL") == std::string::npos);
    const std::string driver = ReadAll("gpu/shadps4/core/libraries/videoout/driver.cpp");
    CHECK(driver.find("Vulkan::Presenter") == std::string::npos);
    CHECK(driver.find("OpenGL") == std::string::npos);
    for (const char* file : {"gpu/shadps4/core/libraries/videodec/videodec2.cpp",
                             "gpu/shadps4/core/libraries/videodec/videodec2_impl.cpp",
                             "gpu/shadps4/core/libraries/videodec/video_utils.cpp"}) {
        const std::string videodec = ReadAll(file);
        CHECK(!videodec.empty());
        CHECK(videodec.find("Vulkan::") == std::string::npos);
        CHECK(videodec.find("renderer_vulkan") == std::string::npos);
    }
    const std::string factory = ReadAll("gpu/shadps4/video_core/renderer_factory.cpp");
    CHECK(factory.find("make_unique<Vulkan::Presenter>") != std::string::npos);
    CHECK(factory.find("make_unique<OpenGL::Presenter>") != std::string::npos);
    CHECK(factory.find("Active() == TsrRenderer::Backend::Vulkan") != std::string::npos);
}

void TestRequirements() {
    OpenGL::Capabilities caps;
    caps.major = 3;
    caps.minor = 3;
    caps.s3tc = true;
    caps.max_texture_buffer_texels = OpenGL::MinimumRingTexels;
    CHECK(OpenGL::MissingRequirement(caps).empty());

    auto old_context = caps;
    old_context.minor = 2;
    CHECK(OpenGL::MissingRequirement(old_context).find("3.3") != std::string::npos);
    auto no_s3tc = caps;
    no_s3tc.s3tc = false;
    CHECK(OpenGL::MissingRequirement(no_s3tc).find("s3tc") != std::string::npos);
    auto small_buffers = caps;
    small_buffers.max_texture_buffer_texels = 65536;
    CHECK(OpenGL::MissingRequirement(small_buffers).find("texture buffer") != std::string::npos);

    auto newer = caps;
    newer.major = 4;
    newer.minor = 0;
    CHECK(OpenGL::MissingRequirement(newer).empty());
}

void TestGlesRequirements() {
    OpenGL::Capabilities caps;
    caps.gles = true;
    caps.major = 3;
    caps.s3tc = true;
    caps.draw_buffers_blend = true;
    caps.max_texture_size = OpenGL::MinimumGlesTextureSize;
    CHECK(OpenGL::MissingRequirement(caps).empty());

    auto level_10_0 = caps;
    level_10_0.major = 2;
    CHECK(OpenGL::MissingRequirement(level_10_0).find("10_1") != std::string::npos);
    auto no_s3tc = caps;
    no_s3tc.s3tc = false;
    CHECK(OpenGL::MissingRequirement(no_s3tc).find("s3tc") != std::string::npos);
    auto no_blend = caps;
    no_blend.draw_buffers_blend = false;
    CHECK(OpenGL::MissingRequirement(no_blend).find("draw_buffers_indexed") != std::string::npos);
    auto small = caps;
    small.max_texture_size = 2048;
    CHECK(OpenGL::MissingRequirement(small).find("4096") != std::string::npos);

    using OpenGL::IsAngleDirect3D11;
    CHECK(IsAngleDirect3D11("ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 (0x00002786) Direct3D11 vs_5_0 ps_5_0, D3D11-32.0.15)"));
    CHECK(!IsAngleDirect3D11("ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 Direct3D9Ex vs_3_0 ps_3_0)"));
    CHECK(!IsAngleDirect3D11("ANGLE (NVIDIA, Vulkan 1.3.280, NVIDIA GeForce RTX 4070)"));
    CHECK(!IsAngleDirect3D11("NVIDIA GeForce RTX 4070/PCIe/SSE2"));
}

} // namespace

int main() {
    TestParse();
    TestResolve();
    TestDirect3D11();
    TestDirect3D11Windows();
    TestRectPathsStaySeparate();
    TestGlesRequirements();
    TestLauncherAgrees();
    TestOptionsMenuRow();
    TestBoundary();
    TestRequirements();
    std::printf("renderer selection tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

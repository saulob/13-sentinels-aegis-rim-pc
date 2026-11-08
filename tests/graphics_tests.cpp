// Render resolution without a window: the verified render size sites against the real boot images, the patch's
// refusals on synthetic copies, the original 16:9 presentation and the settings the launcher and the Options menu
// share. It shows what the game code and the presenters are given, not how a frame looks.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game_profile.h"
#include "settings.h"
#include "tsr_launcher.h"
#include "tsr_options_menu.h"
#include "tsr_resolution.h"

namespace fs = std::filesystem;
using namespace tsr;

namespace {

int checks = 0;
int failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                                                   \
        }                                                                                                              \
    } while (0)

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<unsigned char> ReadBytes(const fs::path& path) {
    const std::string text = ReadText(path);
    return {text.begin(), text.end()};
}

const GameBuild& Build(const char* version) {
    for (const GameBuild& build : SupportedBuilds()) {
        if (std::string_view(build.version) == version) {
            return build;
        }
    }
    std::printf("FAIL: no build %s\n", version);
    std::exit(1);
}

uint32_t Imm(const std::vector<unsigned char>& image, size_t at) {
    uint32_t value;
    std::memcpy(&value, image.data() + at, 4);
    return value;
}

// The game image the loader runs: the tail of boot.bin, image_bytes long (image.json).
std::vector<unsigned char> GameImage(const GameBuild& build) {
    const fs::path folder = fs::path(TSR_SOURCE_DIR) / "build" / "image" / build.folder;
    const std::string meta = ReadText(folder / "image.json");
    const size_t key = meta.find("\"image_bytes\":");
    if (key == std::string::npos) {
        return {};
    }
    const uint64_t size = std::stoull(meta.substr(key + 14));
    std::vector<unsigned char> boot = ReadBytes(folder / "boot.bin");
    if (boot.size() < size) {
        return {};
    }
    return {boot.end() - std::ptrdiff_t(size), boot.end()};
}

void TestResolutionTable() {
    const struct {
        const char* key;
        int width, height, pro;
    } expected[] = {{"native", 1920, 1080, 0}, {"480p", 854, 480, 0},   {"720p", 1280, 720, 0},
                    {"1440p", 2560, 1440, 1},  {"1800p", 3200, 1800, 1}, {"4K", 3840, 2160, 1}};
    CHECK(TSR_RESOLUTION_COUNT == 6);
    for (int i = 0; i < TSR_RESOLUTION_COUNT; ++i) {
        const TsrResolution& r = tsr_resolutions[i];
        CHECK(std::string(r.key) == expected[i].key && r.width == expected[i].width && r.height == expected[i].height &&
              r.pro_memory == expected[i].pro);
        CHECK(tsr_resolution_index(r.key) == i);
        CHECK(std::abs(r.width * 9 - r.height * 16) <= 16); // every target keeps the game's 16:9 composition
    }
    CHECK(tsr_resolution_index("8K") == 0 && tsr_resolution_index(nullptr) == 0);
}

// Each build: six sceKernelIsNeoMode choices, each `call; cmp eax, 1; mov reg, original; mov reg, Pro value`.
void TestSiteTables() {
    std::set<std::pair<uint32_t, std::string>> offsets;
    for (const char* version : {"1.00", "1.01", "1.02"}) {
        const GameBuild& build = Build(version);
        CHECK(!RestrictedToNativeResolution(&build));
        CHECK(build.resolution_sites.size() == 12);
        int widths = 0, heights = 0;
        for (size_t i = 0; i < build.resolution_sites.size(); ++i) {
            const TsrResolutionSite& site = build.resolution_sites[i];
            offsets.insert({site.offset, version});
            CHECK(site.opcode_length == 1 && site.opcode[0] >= 0xb8 && site.opcode[0] <= 0xbf);
            CHECK(site.height ? (site.value == 1080 || site.value == 2160) : (site.value == 1920 || site.value == 3840));
            (site.height ? heights : widths)++;
            if (i % 2 == 1) {
                const TsrResolutionSite& first = build.resolution_sites[i - 1];
                CHECK(site.offset == first.offset + 5 && site.height == first.height && site.value != first.value);
            }
        }
        CHECK(widths == 6 && heights == 6);
    }
    // The executables differ: no offset is assumed to carry over.
    CHECK(offsets.size() == 36);
    const auto& a = Build("1.00").resolution_sites;
    const auto& c = Build("1.02").resolution_sites;
    CHECK(a[0].offset != c[0].offset);
}

void TestRealImages() {
    for (const char* version : {"1.00", "1.01", "1.02"}) {
        const GameBuild& build = Build(version);
        std::vector<unsigned char> image = GameImage(build);
        if (image.empty()) {
            std::printf("skipped: real boot image %s is not prepared (build\\image\\%s)\n", build.label, build.folder);
            continue;
        }
        const auto sites = build.resolution_sites;
        uint64_t neo_call = 0;
        for (size_t i = 0; i < sites.size(); i += 2) {
            const size_t first = sites[i].offset;
            CHECK(image[first - 3] == 0x83 && image[first - 2] == 0xf8 && image[first - 1] == 0x01);
            CHECK(image[first - 8] == 0xe8);
            int32_t rel;
            std::memcpy(&rel, image.data() + first - 7, 4);
            const uint64_t target = uint64_t(int64_t(first - 3) + rel);
            CHECK(neo_call == 0 || neo_call == target); // one Neo query feeds every choice
            neo_call = target;
            const size_t after = first + 10;
            const bool cmove_next = image[after] == 0x0f && image[after + 1] == 0x44;
            const bool cmove_after_lea = image[after] == 0x48 && image[after + 1] == 0x8d &&
                                         image[after + 7] == 0x0f && image[after + 8] == 0x44;
            CHECK(cmove_next || cmove_after_lea);
        }
        for (const TsrResolutionSite& site : sites) {
            CHECK(image[site.offset] == site.opcode[0] && Imm(image, site.offset + 1) == site.value);
        }
        // No other choice of the same shape exists: the table covers all of them.
        size_t selectors = 0;
        for (size_t at = 0; at + 13 < image.size(); ++at) {
            if (image[at] != 0x83 || image[at + 1] != 0xf8 || image[at + 2] != 0x01 || image[at + 3] < 0xb8 ||
                image[at + 3] > 0xbf || image[at + 8] < 0xb8 || image[at + 8] > 0xbf) {
                continue;
            }
            const uint32_t x = Imm(image, at + 4), y = Imm(image, at + 9);
            if ((x == 1920 && y == 3840) || (x == 3840 && y == 1920) || (x == 1080 && y == 2160) ||
                (x == 2160 && y == 1080)) {
                ++selectors;
            }
        }
        CHECK(selectors == 6);
        // The save thumbnail stays the 1920 x 1080 capture PS4 Pro also uses: never a site.
        const unsigned char thumb[] = {0xbe, 0x80, 0x07, 0x00, 0x00, 0xba, 0x38, 0x04, 0x00, 0x00};
        const auto found = std::search(image.begin(), image.end(), std::begin(thumb), std::end(thumb));
        CHECK(found != image.end());
        const uint32_t thumb_at = uint32_t(found - image.begin());
        CHECK(std::none_of(sites.begin(), sites.end(), [&](const TsrResolutionSite& s) {
            return s.offset >= thumb_at && s.offset < thumb_at + 10;
        }));

        for (const TsrResolution& resolution : tsr_resolutions) {
            std::vector<unsigned char> copy = image;
            char error[256] = "";
            CHECK(tsr_apply_resolution(copy.data(), copy.size(), sites.data(), sites.size(), &resolution, error,
                                       sizeof(error)) == 1);
            size_t changed = 0;
            for (size_t i = 0; i < copy.size(); ++i) {
                changed += copy[i] != image[i];
            }
            for (const TsrResolutionSite& site : sites) {
                CHECK(Imm(copy, site.offset + 1) == uint32_t(site.height ? resolution.height : resolution.width));
                CHECK(copy[site.offset] == site.opcode[0]);
            }
            CHECK(changed <= sites.size() * 4);
            if (std::string_view(resolution.key) == "native") {
                CHECK(changed > 0); // the Pro alternatives become 1920 x 1080: the loader never patches Native
            }
            // A patched image is not the original any more: a second patch refuses it.
            const std::vector<unsigned char> patched = copy;
            if (std::string_view(resolution.key) != "native") {
                CHECK(tsr_apply_resolution(copy.data(), copy.size(), sites.data(), sites.size(), &resolution, error,
                                           sizeof(error)) == 0);
                CHECK(copy == patched);
            }
        }
        std::printf("real boot images %s: 12 render size sites verified, 6 Neo choices, thumbnail untouched\n",
                    build.label);
    }
}

void TestRefusals() {
    const GameBuild& build = Build("1.00");
    const auto sites = build.resolution_sites;
    const uint32_t end = sites.back().offset + 16;
    std::vector<unsigned char> image(end, 0x90);
    for (const TsrResolutionSite& site : sites) {
        image[site.offset] = site.opcode[0];
        std::memcpy(image.data() + site.offset + 1, &site.value, 4);
    }
    const TsrResolution& k4 = tsr_resolutions[5];
    char error[256] = "";
    std::vector<unsigned char> copy = image;
    CHECK(tsr_apply_resolution(copy.data(), copy.size(), sites.data(), sites.size(), &k4, error, sizeof(error)) == 1);

    // One wrong byte anywhere: nothing is written, the original stays.
    for (const size_t index : {size_t(0), sites.size() / 2, sites.size() - 1}) {
        std::vector<unsigned char> broken = image;
        broken[sites[index].offset + 2] ^= 0x01;
        const std::vector<unsigned char> before = broken;
        CHECK(tsr_apply_resolution(broken.data(), broken.size(), sites.data(), sites.size(), &k4, error,
                                   sizeof(error)) == 0);
        CHECK(broken == before);
        CHECK(std::string(error).find("unexpected instruction") != std::string::npos);
    }
    std::vector<unsigned char> wrong_opcode = image;
    wrong_opcode[sites[3].offset] = 0xba;
    const std::vector<unsigned char> before = wrong_opcode;
    CHECK(tsr_apply_resolution(wrong_opcode.data(), wrong_opcode.size(), sites.data(), sites.size(), &k4, error,
                               sizeof(error)) == 0);
    CHECK(wrong_opcode == before);

    // A short image (another executable) is refused before anything is read past it.
    std::vector<unsigned char> short_image(image.begin(), image.begin() + sites.back().offset + 2);
    const std::vector<unsigned char> short_before = short_image;
    CHECK(tsr_apply_resolution(short_image.data(), short_image.size(), sites.data(), sites.size(), &k4, error,
                               sizeof(error)) == 0);
    CHECK(short_image == short_before && std::string(error).find("outside the image") != std::string::npos);

    // Overlapping sites are refused even when each one matches.
    std::vector<TsrResolutionSite> overlapping(sites.begin(), sites.end());
    overlapping.push_back({sites[0].offset + 2, 1, {image[sites[0].offset + 2]}, Imm(image, sites[0].offset + 3), 0});
    std::vector<unsigned char> overlap_image = image;
    CHECK(tsr_apply_resolution(overlap_image.data(), overlap_image.size(), overlapping.data(), overlapping.size(), &k4,
                               error, sizeof(error)) == 0);
    CHECK(overlap_image == image && std::string(error).find("overlapping") != std::string::npos);

    // Every build's own sites against another build's image: refused, unchanged.
    for (const char* other : {"1.01", "1.02"}) {
        std::vector<unsigned char> foreign = image;
        foreign.resize(std::max<size_t>(foreign.size(), Build(other).resolution_sites.back().offset + 16), 0x90);
        const std::vector<unsigned char> foreign_before = foreign;
        const auto other_sites = Build(other).resolution_sites;
        CHECK(tsr_apply_resolution(foreign.data(), foreign.size(), other_sites.data(), other_sites.size(), &k4, error,
                                   sizeof(error)) == 0);
        CHECK(foreign == foreign_before);
    }
}

void TestLoaderFallback() {
    const std::string loader = ReadText(fs::path(TSR_SOURCE_DIR) / "src" / "loader.c");
    const size_t apply = loader.find("tsr_apply_resolution(image,size,game_build.resolution_sites");
    const size_t fallback = loader.find("fallback to Native 1920x1080", apply);
    const size_t neo = loader.find("runtime_tsr_set_neo_mode(render_resolution->pro_memory);");
    const size_t entry_call = loader.find("tsr_startup_phase(\"patches installed\")");
    CHECK(apply != std::string::npos && fallback != std::string::npos && neo != std::string::npos);
    CHECK(apply < fallback && fallback < neo && neo < entry_call);
    CHECK(loader.find("if (!known_build) snprintf(error") != std::string::npos);
    CHECK(loader.find("render_resolution=&tsr_resolutions[0];", fallback) < neo);
    CHECK(loader.find("Render resolution: requested %s (%dx%d), effective %s, main render target %dx%d") !=
          std::string::npos);
    CHECK(loader.find("tsr_render_target") == std::string::npos);
    // The 12 size sites are the only render-size code change; the 0.5 Pro scale A/B showed no visible effect.
    CHECK(loader.find("apply_pro_scale") == std::string::npos);
    CHECK(loader.find("TSR_DEBUG_PRO_SCALE") == std::string::npos);
    CHECK(loader.find("allocate(round_page(size) + hook_bytes);") != std::string::npos);
    CHECK(loader.find("aspect") == std::string::npos);
}

void TestSettings() {
    const fs::path scratch = fs::temp_directory_path() / "tsr-graphics-tests";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
    for (const TsrResolution& r : tsr_resolutions) {
        CHECK(IsValidSetting(kRenderResolutionKey, r.key));
    }
    CHECK(!IsValidSetting(kRenderResolutionKey, "1080p"));
    for (const char* mode : {"windowed", "borderless"}) {
        CHECK(IsValidSetting(kDisplayModeKey, mode));
    }
    const fs::path file = scratch / "tsr.ini";
    CHECK(LauncherSettings(file).Get(kRenderResolutionKey, kNativeResolution) == "native");
    {
        LauncherSettings settings(file);
        settings.Set(kRenderResolutionKey, "1800p");
        CHECK(settings.Save());
    }
    LauncherSettings reloaded(file);
    CHECK(reloaded.Get(kRenderResolutionKey, kNativeResolution) == "1800p");
    CHECK(reloaded.Save());
    CHECK(tsr_render_resolution(file.string().c_str(), nullptr) == tsr_resolution_index("1800p"));
    // The game's own reader of the same file, for every build folder: the choice follows a version change.
    for (const GameBuild& build : SupportedBuilds()) {
        const fs::path image_dir = fs::path(TSR_SOURCE_DIR) / "build" / "image" / build.folder;
        CHECK(tsr_render_resolution(file.string().c_str(), image_dir.string().c_str()) == tsr_resolution_index("1800p"));
    }
    // A file saved by a build that offered Aspect Ratio keeps its lines and every other setting.
    const fs::path old_file = scratch / "old.ini";
    {
        std::ofstream out(old_file);
        out << "render_resolution = 1440p\n" "aspect_ratio = 21:9\n" "display_mode = borderless\n"
               "window_width = 2560\n" "window_height = 1080\n";
    }
    LauncherSettings old_settings(old_file);
    CHECK(old_settings.Get(kRenderResolutionKey, kNativeResolution) == "1440p");
    CHECK(old_settings.Get(kDisplayModeKey, "windowed") == "borderless");
    CHECK(old_settings.Save());
    CHECK(tsr_render_resolution(old_file.string().c_str(), nullptr) == tsr_resolution_index("1440p"));
    CHECK(ReadText(old_file).find("render_resolution = 1440p") != std::string::npos);
    CHECK(LauncherSettings(old_file).Get(kRenderResolutionKey, kNativeResolution) == "1440p");
    fs::remove_all(scratch, ec);
}

void TestMenusAgree() {
    using namespace TsrOptions;
    const Setting& resolution = SettingOf(Id::RenderResolution);
    CHECK(resolution.page == Page::Graphics && resolution.apply == Apply::Restart);
    CHECK(resolution.feature == TsrSettings::RenderResolution);
    CHECK(resolution.choices.size() == TSR_RESOLUTION_COUNT);
    for (const Setting& setting : kSettings) {
        CHECK(std::string(setting.key) != "aspect_ratio");
        CHECK(std::string(setting.label).find("Aspect") == std::string::npos);
        CHECK(std::string(setting.help).find("Ultrawide") == std::string::npos);
    }
    for (const Choice& choice : resolution.choices) {
        CHECK(tsr_resolution_index(std::string(choice.value).c_str()) != 0 || choice.value == "native");
    }
    CHECK(resolution.choices[0].value == "480p" && resolution.choices[2].value == "native" &&
          resolution.choices[5].value == "4K");

    const std::string launcher = ReadText(fs::path(TSR_SOURCE_DIR) / "launcher" / "launcher.cpp");
    CHECK(launcher.find("are not available for it") == std::string::npos);
    CHECK(launcher.find("kRestricted") == std::string::npos);
    const size_t graphics = launcher.find("void PageGraphics(");
    CHECK(launcher.find("ui.Combo(\"Render resolution\"", graphics) < launcher.find("ui.Toggle(\"Anti-aliasing\"", graphics));
    for (const char* gone : {"Aspect Ratio", "AspectRatio", "kAspect", "aspect_ratio", "Ultrawide"}) {
        CHECK(launcher.find(gone) == std::string::npos);
    }
    CHECK(launcher.find("ui.Combo(\"Texture filtering\"", graphics) != std::string::npos);
    CHECK(launcher.find("ui.Combo(\"Renderer\"") < graphics);
}

// The weight each source texel of a row gets when `in` texels are reduced to `out` pixels: one bilinear tap per pixel
// (the presenters before SampleArea) or SampleArea's taps, as written in post_process.frag. A zero means a one-texel
// stroke in that column vanishes from the picture.
std::vector<double> TexelWeights(int in, int out, bool area) {
    std::vector<double> weights(size_t(in), 0.0);
    const double footprint = double(in) / out;
    const int taps = area ? std::clamp(int(std::ceil(footprint - 0.001)), 1, 8) : 1;
    const double spacing = footprint / taps;
    for (int i = 0; i < out; ++i) {
        const double centre = (i + 0.5) * footprint;
        for (int t = 0; t < taps; ++t) {
            const double at = (taps == 1 ? centre : centre - 0.5 * footprint + (t + 0.5) * spacing) - 0.5;
            const int left = int(std::floor(at));
            const double fraction = at - left;
            weights[size_t(std::clamp(left, 0, in - 1))] += (1.0 - fraction) / taps;
            weights[size_t(std::clamp(left + 1, 0, in - 1))] += fraction / taps;
        }
    }
    return weights;
}

size_t DroppedTexels(const std::vector<double>& weights) {
    return size_t(std::count_if(weights.begin(), weights.end(), [](double w) { return w < 1e-9; }));
}

// The fragmented title text at 1440p (logs 019/020): the 2560x1440 image reaches the 1153x649 picture of a 1514x649
// window through the post-process pass, a 2.22x reduction; Native 1080p in the same window is 1.67x.
void TestDownsampleFilter() {
    CHECK(DroppedTexels(TexelWeights(1920, 1153, false)) == 0);
    CHECK(DroppedTexels(TexelWeights(1080, 649, false)) == 0);
    const size_t dropped_x = DroppedTexels(TexelWeights(2560, 1153, false));
    const size_t dropped_y = DroppedTexels(TexelWeights(1440, 649, false));
    CHECK(dropped_x > 0 && dropped_y > 0);
    std::printf("single bilinear tap 2560x1440 -> 1153x649: %zu of 2560 columns and %zu of 1440 rows unsampled\n",
                dropped_x, dropped_y);

    const int reductions[][2] = {{2560, 1153}, {1440, 649}, {3200, 1153}, {3840, 1153}, {2160, 649}, {3840, 1920},
                                 {1920, 1153}, {1080, 649}, {3840, 854}, {2160, 480}};
    for (const auto& [in, out] : reductions) {
        const std::vector<double> weights = TexelWeights(in, out, true);
        const double mean = double(out) / in;
        const auto [low, high] = std::minmax_element(weights.begin(), weights.end());
        CHECK(DroppedTexels(weights) == 0);
        CHECK(*low >= 0.5 * mean && *high <= 1.5 * mean); // a thin stroke keeps a steady weight wherever it falls
    }
    // 1:1 and upscaling keep the single tap, so Native 1080p in a 1080p picture is unchanged.
    for (const auto& [in, out] : {std::pair{1920, 1920}, std::pair{1080, 1080}, std::pair{1280, 1920},
                                  std::pair{854, 1514}}) {
        CHECK(std::clamp(int(std::ceil(double(in) / out - 0.001)), 1, 8) == 1);
    }

    const fs::path video = fs::path(TSR_SOURCE_DIR) / "gpu" / "shadps4" / "video_core";
    const std::string vulkan = ReadText(video / "host_shaders" / "post_process.frag");
    const std::string opengl = ReadText(video / "renderer_opengl" / "gl_presenter.cpp");
    const auto function = [](const std::string& text) {
        const size_t start = text.find("vec4 SampleArea(sampler2D tex, vec2 uv) {");
        const size_t end = text.find("\n}\n", start);
        return start == std::string::npos || end == std::string::npos ? std::string{} : text.substr(start, end - start);
    };
    CHECK(!function(vulkan).empty() && function(vulkan) == function(opengl));
    CHECK(function(vulkan).find("ceil(footprint - 0.001)), ivec2(1), ivec2(8))") != std::string::npos);
    CHECK(vulkan.find("vec4 color_linear = SampleArea(texSampler, uv);") != std::string::npos);
    CHECK(opengl.find("color = vec4(SampleArea(source, uv).rgb, 1.0);") != std::string::npos);
    CHECK(vulkan.find("texture(texSampler, uv)") == std::string::npos);
}

void TestBackends() {
    const fs::path video = fs::path(TSR_SOURCE_DIR) / "gpu" / "shadps4" / "video_core";
    const std::string vulkan = ReadText(video / "renderer_vulkan" / "vk_presenter.cpp");
    const std::string opengl = ReadText(video / "renderer_opengl" / "gl_presenter.cpp");
    CHECK(vulkan.find("MakeImageBlitFit(frame->width, frame->height, extent.width, extent.height)") != std::string::npos);
    CHECK(vulkan.find("FitImage(") != std::string::npos && opengl.find("FitInside(") != std::string::npos);
    CHECK(vulkan.find("Framing") == std::string::npos && opengl.find("Framing") == std::string::npos);
    CHECK(vulkan.find("aspect_ratio") == std::string::npos && opengl.find("aspect_ratio") == std::string::npos);
    // ANGLE's minimum texture size covers the largest render target.
    CHECK(ReadText(video / "renderer_opengl" / "gl_capabilities.h").find("MinimumGlesTextureSize = 4096") !=
          std::string::npos);
}

} // namespace

int main() {
    TestResolutionTable();
    TestSiteTables();
    TestRealImages();
    TestRefusals();
    TestLoaderFallback();
    TestDownsampleFilter();
    TestSettings();
    TestMenusAgree();
    TestBackends();
    std::printf("graphics: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

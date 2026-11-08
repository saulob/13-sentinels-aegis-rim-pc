// Where assets/launcher/header-art.png goes in the launcher's header, and how it gets there.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tsr {

inline constexpr float kHeaderArtAspect = 3840.0f / 432.0f;
inline constexpr int kHeaderArtStripWidth = 128;
inline constexpr float kHeaderFade = 0.35f;            // of the header, over its gradient

struct HeaderArtRect {
    float min_x, min_y, max_x, max_y;
};

struct HeaderArtStrip {
    float min_x, max_x, min_u, max_u;
};

// Full header height, whole pixels, flush with the header's top and right; only the left is cut when the window is narrow.
HeaderArtRect PlaceHeaderArt(float top, float right, float bottom, float aspect);

// SDL's software renderer, the launcher's on Windows, drops triangles whose texture width times area overflows an int.
std::vector<HeaderArtStrip> SplitHeaderArt(const HeaderArtRect& rect);

// Area-averaged RGBA at the size it is shown: that renderer samples without filtering. Empty on bad input.
std::vector<uint8_t> ScaleArtRgba(const uint8_t* rgba, int width, int height, int out_width, int out_height,
                                  float alpha_gain = 1.0f);

} // namespace tsr

// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D texSampler;

layout (push_constant) uniform settings {
    float gamma;
    bool hdr;
    bool srgb_input;
} pp;

const float cutoff = 0.0031308, a = 1.055, b = 0.055, d = 12.92;
vec3 gamma(vec3 rgb) {
    return mix(
        a * pow(rgb, vec3(1.0 / (2.4 + 1.0 - pp.gamma))) - b,
        d * rgb / pp.gamma,
        lessThan(rgb, vec3(cutoff))
    );
}

// Exact inverse of gamma() at unit gamma, for buffers that are sRGB encoded but must be sampled
// through a UNORM view because Vulkan has no sRGB variant of the 10-bit format.
vec3 degamma(vec3 rgb) {
    return mix(
        pow(max(rgb + b, 0.0) / a, vec3(2.4)),
        rgb / d,
        lessThan(rgb, vec3(d * cutoff))
    );
}

// One bilinear tap per pixel skips source texels once the picture is reduced more than 2x, so thin
// strokes such as small text break apart; average taps spaced at most one texel apart instead.
vec4 SampleArea(sampler2D tex, vec2 uv) {
    vec2 size = vec2(textureSize(tex, 0));
    vec2 footprint = abs(vec2(dFdx(uv.x), dFdy(uv.y))) * size;
    ivec2 taps = clamp(ivec2(ceil(footprint - 0.001)), ivec2(1), ivec2(8));
    if (taps.x * taps.y == 1) return textureLod(tex, uv, 0.0);
    vec2 spacing = footprint / vec2(taps) / size;
    vec2 first = uv - 0.5 * footprint / size + 0.5 * spacing;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < taps.y; ++y)
        for (int x = 0; x < taps.x; ++x)
            sum += textureLod(tex, first + vec2(x, y) * spacing, 0.0);
    return sum / float(taps.x * taps.y);
}

void main() {
    vec4 color_linear = SampleArea(texSampler, uv);
    if (pp.hdr) {
        color = color_linear;
    } else {
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        color = vec4(gamma(color_linear.rgb), color_linear.a);
    }
}

#pragma once
#include "camera.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace camview {
inline bool sameRate(const Format& a, const Format& b) {
    return a.fpsDenominator && b.fpsDenominator &&
        std::uint64_t(a.fpsNumerator) * b.fpsDenominator ==
        std::uint64_t(b.fpsNumerator) * a.fpsDenominator;
}
inline unsigned mjpgFourcc() {
    return unsigned('M') | (unsigned('J') << 8) | (unsigned('P') << 16) | (unsigned('G') << 24);
}
inline int closestFormat(const std::vector<Format>& formats, int width, int height,
                         double fps, unsigned fourcc, bool requireSize = false) {
    int best = -1;
    double bestSize = std::numeric_limits<double>::infinity();
    double bestRate = std::numeric_limits<double>::infinity();
    int bestPixel = 1;
    for (size_t i = 0; i < formats.size(); ++i) {
        const auto& f = formats[i];
        if (f.width <= 0 || f.height <= 0 || !f.fpsNumerator || !f.fpsDenominator) continue;
        if (requireSize && (f.width != width || f.height != height)) continue;
        const double dx = double(f.width) - width, dy = double(f.height) - height;
        const double size = dx * dx + dy * dy;
        const double rate = std::abs(f.fps() - fps);
        const int pixel = f.fourcc == fourcc ? 0 : 1;
        if (best < 0 || size < bestSize || (size == bestSize &&
            (rate < bestRate || (rate == bestRate && pixel < bestPixel)))) {
            best = int(i); bestSize = size; bestRate = rate; bestPixel = pixel;
        }
    }
    return best;
}
}

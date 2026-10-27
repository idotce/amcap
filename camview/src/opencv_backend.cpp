#include "camera.h"
#include <opencv2/videoio.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace camview {
class OpenCVCapture final : public Capture {
    cv::VideoCapture capture_;
    CaptureInfo info_;
public:
    void open(const Device& device, const Format& format) override {
#ifdef _WIN32
        const bool ok = capture_.open(device.index, cv::CAP_DSHOW);
#elif defined(__linux__)
        const bool ok = capture_.open(device.path, cv::CAP_V4L2);
#else
        const bool ok = capture_.open(device.index, cv::CAP_AVFOUNDATION);
#endif
        if (!ok) throw std::runtime_error("OpenCV 无法打开设备，请检查权限或是否被其他程序占用");
        if (format.width) {
#ifdef _WIN32
            if (format.fourcc == unsigned(cv::VideoWriter::fourcc('N', 'V', '1', '2')))
                throw std::runtime_error("OpenCV 4.6 DirectShow 不支持选择 NV12，请使用 MF 后端");
            // In OpenCV 4.6, setting FPS rebuilds the graph without preserving FourCC.
            // Apply FPS first and FourCC last, at the requested dimensions.
            const bool fps = capture_.set(cv::CAP_PROP_FPS, format.fps());
            const bool width = capture_.set(cv::CAP_PROP_FRAME_WIDTH, format.width);
            const bool height = capture_.set(cv::CAP_PROP_FRAME_HEIGHT, format.height);
            const bool pixel = !format.fourcc || capture_.set(cv::CAP_PROP_FOURCC, format.fourcc);
            if (!pixel || (format.fourcc && unsigned(capture_.get(cv::CAP_PROP_FOURCC)) != format.fourcc))
                throw std::runtime_error("OpenCV/DirectShow 未应用所选像素格式，请尝试 MF 后端");
#else
#ifndef __APPLE__
            if (format.fourcc) capture_.set(cv::CAP_PROP_FOURCC, format.fourcc);
#endif
            const bool width = capture_.set(cv::CAP_PROP_FRAME_WIDTH, format.width);
            const bool height = capture_.set(cv::CAP_PROP_FRAME_HEIGHT, format.height);
            const bool fps = capture_.set(cv::CAP_PROP_FPS, format.fps());
#endif
            if (!width || !height || !fps ||
                int(capture_.get(cv::CAP_PROP_FRAME_WIDTH)) != format.width ||
                int(capture_.get(cv::CAP_PROP_FRAME_HEIGHT)) != format.height ||
                std::abs(capture_.get(cv::CAP_PROP_FPS) - format.fps()) > 0.1)
                throw std::runtime_error("OpenCV 未能应用所选分辨率／帧率，请尝试其他格式或 MF 后端");
        }
        // DirectShow reports the requested FPS, not measured hardware delivery.
        info_.reportedFps = capture_.get(cv::CAP_PROP_FPS);
    }
    bool read(Frame& output) override {
        cv::Mat bgr;
        const auto start = std::chrono::steady_clock::now();
        const bool received = capture_.read(bgr) && !bgr.empty();
        info_.readMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (received) {
            output.cols = bgr.cols; output.rows = bgr.rows;
            output.pixels = std::make_shared<std::vector<unsigned char>>(size_t(bgr.cols) * bgr.rows * 3);
            for (int y = 0; y < bgr.rows; ++y)
                std::memcpy(output.pixels->data() + size_t(y) * bgr.cols * 3, bgr.ptr(y), size_t(bgr.cols) * 3);
        }
        return received;
    }
    CaptureInfo info() const override { return info_; }
};
extern "C" __declspec(dllexport) Capture* createOpenCVCapture() { return new OpenCVCapture(); }
}

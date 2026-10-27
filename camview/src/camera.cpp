#include "camera.h"
#include <opencv2/videoio.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#endif

namespace camview {
std::string Format::label() const {
    std::ostringstream out;
    out << width << " x " << height << "  " << std::fixed << std::setprecision(3) << fps() << " FPS  ";
    for (int i = 0; i < 4; ++i) {
        const char c = char((fourcc >> (i * 8)) & 0xff);
        out << ((c >= 32 && c <= 126) ? c : '?');
    }
    return out.str();
}
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
    bool read(cv::Mat& bgr) override {
        const auto start = std::chrono::steady_clock::now();
        const bool received = capture_.read(bgr) && !bgr.empty();
        info_.readMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return received;
    }
    CaptureInfo info() const override { return info_; }
};
std::unique_ptr<Capture> makeOpenCV() { return std::make_unique<OpenCVCapture>(); }
Camera::~Camera() { stop(); }
void Camera::start(Device device, Format format, Backend backend) {
    stop();
    { std::lock_guard<std::mutex> lock(mutex_); state_ = {}; changes_.clear(); }
    stopping_ = false;
    worker_ = std::thread(&Camera::run, this, std::move(device), format, backend);
}
void Camera::stop() {
    stopping_ = true;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    state_.running = false;
}
void Camera::set(unsigned id, int value, bool automatic) {
    std::lock_guard<std::mutex> lock(mutex_);
    changes_.push_back({id, value, automatic});
}
Snapshot Camera::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_; // Published frames are immutable and reference counted.
}
void Camera::run(Device device, Format format, Backend backend) {
#ifdef _WIN32
    // OpenCV's DirectShow backend calls CoInitialize (STA) internally.
    const DWORD apartment = backend == Backend::OpenCV ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED;
    const HRESULT com = CoInitializeEx(nullptr, apartment);
    if (FAILED(com)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.error = "无法初始化 COM";
        return;
    }
#endif
    try {
        auto capture = makeOpenCV();
#ifdef _WIN32
        if (backend == Backend::MediaFoundation) capture = makeMediaFoundation();
#else
        (void)backend;
#endif
        capture->open(device, format);
        std::unique_ptr<Controls> controls;
        std::vector<Control> initialControls;
        std::string controlNotice;
        try {
            controls = openControls(device);
            initialControls = controls->read();
        } catch (const std::exception& e) {
            controlNotice = std::string("预览可用，控制接口不可用：") + e.what();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.running = true;
            state_.controls = std::move(initialControls);
            ++state_.controlsRevision;
            state_.notice = controlNotice;
            if (state_.notice.empty() && state_.controls.empty()) state_.notice = "当前平台或驱动未提供可读控制项";
        }
        auto previous = std::chrono::steady_clock::now();
        auto lastFrame = previous;
        unsigned count = 0;
        double fps = 0;
        double readMs = 0, convertMs = 0, publishMs = 0, controlsMs = 0;
        double firstTimestamp = -1, lastTimestamp = -1;
        unsigned timestampCount = 0, emptyReads = 0;
        const auto started = previous;
        std::ofstream metrics;
        try {
            const auto directory = std::filesystem::current_path() / "_temp";
            std::filesystem::create_directories(directory);
            const auto path = directory / "capture-metrics.csv";
            metrics.open(path, std::ios::app);
            if (metrics) {
                metrics << "\n# session " << device.name << " " << format.label() << '\n';
                metrics << "elapsed_s,backend,width,height,requested_fps,reported_fps,received_fps,source_timestamp_fps,read_ms,convert_ms,publish_ms,controls_ms,empty_reads\n";
                metrics << std::fixed << std::setprecision(3);
            }
        } catch (const std::exception&) { /* Diagnostics must not prevent capture. */ }
        while (!stopping_) {
            const auto controlsStart = std::chrono::steady_clock::now();
            std::vector<Change> changes;
            { std::lock_guard<std::mutex> lock(mutex_); changes.swap(changes_); }
            if (controls && !changes.empty()) {
                std::string error;
                for (const auto& change : changes) {
                    try { controls->set(change.id, change.value, change.automatic); }
                    catch (const std::exception& e) { error += std::string(e.what()) + "\n"; }
                }
                auto current = controls->read();
                std::lock_guard<std::mutex> lock(mutex_);
                state_.controls = std::move(current);
                ++state_.controlsRevision;
                state_.notice = error;
            }
            controlsMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - controlsStart).count();
            cv::Mat frame;
            const bool received = capture->read(frame);
            const auto info = capture->info();
            readMs += info.readMs;
            convertMs += info.convertMs;
            if (!received) {
                ++emptyReads;
                if (std::chrono::steady_clock::now() - lastFrame > std::chrono::seconds(5))
                    throw std::runtime_error("连续 5 秒没有收到图像，设备可能已断开");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            lastFrame = std::chrono::steady_clock::now();
            ++count;
            if (info.timestampSeconds >= 0) {
                if (firstTimestamp < 0) firstTimestamp = info.timestampSeconds;
                lastTimestamp = info.timestampSeconds;
                ++timestampCount;
            }
            const double seconds = std::chrono::duration<double>(lastFrame - previous).count();
            if (seconds >= 1.0) fps = count / seconds;
            const auto publishStart = std::chrono::steady_clock::now();
            const int width = frame.cols, height = frame.rows;
            {
                std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
                // Both MF and OpenCV/DirectShow allocate into a fresh local Mat for each read.
                state_.bgr = std::move(frame);
#else
                state_.bgr = frame.clone();
#endif
                state_.measuredFps = fps;
                ++state_.sequence;
            }
            publishMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - publishStart).count();
            if (seconds >= 1.0) {
                const double sourceFps = timestampCount > 1 && lastTimestamp > firstTimestamp ?
                    (timestampCount - 1) / (lastTimestamp - firstTimestamp) : 0;
                if (metrics) {
                    metrics << std::chrono::duration<double>(lastFrame - started).count() << ','
                            << (backend == Backend::MediaFoundation ? "MF" : "OpenCV") << ','
                            << width << ',' << height << ',' << format.fps() << ',' << info.reportedFps << ','
                            << fps << ',' << sourceFps << ',' << readMs / count << ',' << convertMs / count << ','
                            << publishMs / count << ',' << controlsMs / count << ',' << emptyReads << '\n';
                    metrics.flush();
                }
                count = 0; previous = lastFrame;
                readMs = convertMs = publishMs = controlsMs = 0;
                firstTimestamp = lastTimestamp = -1; timestampCount = emptyReads = 0;
            }
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.error = e.what();
    }
    { std::lock_guard<std::mutex> lock(mutex_); state_.running = false; }
#ifdef _WIN32
    CoUninitialize();
#endif
}
}

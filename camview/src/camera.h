#pragma once
#ifndef _WIN32
#include <opencv2/core.hpp>
#endif
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camview {
#ifdef _WIN32
struct Frame {
    int cols = 0, rows = 0;
    std::shared_ptr<std::vector<unsigned char>> pixels;
    bool empty() const { return !pixels || pixels->empty(); }
};
#else
using Frame = cv::Mat;
#endif
enum class Backend { OpenCV, MediaFoundation };
struct Device {
    std::string name;
    std::string path;
    int index = 0;
    // Windows uses the symbolic link to match MF and DirectShow devices.
    std::string mfPath;
};
struct Format {
    int width = 0;
    int height = 0;
    unsigned fpsNumerator = 0;
    unsigned fpsDenominator = 1;
    unsigned fourcc = 0;
    int nativeIndex = -1;
    double fps() const { return fpsDenominator ? double(fpsNumerator) / fpsDenominator : 0; }
    std::string label() const;
};
struct Control {
    unsigned id = 0;
    std::string group;
    std::string name;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    int defaultValue = 0;
    int value = 0;
    bool supportsAuto = false;
    bool supportsManual = true;
    bool automatic = false;
    bool writable = true;
    std::vector<std::pair<int, std::string>> choices;
};
class Controls {
public:
    virtual ~Controls() = default;
    virtual std::vector<Control> read() = 0;
    virtual void set(unsigned id, int value, bool automatic) = 0;
};
std::vector<Device> enumerateDevices();
std::vector<Format> enumerateFormats(const Device& device, Backend backend);
std::unique_ptr<Controls> openControls(const Device& device);
struct CaptureInfo {
    double reportedFps = 0;
    double readMs = 0;
    double convertMs = 0;
    double timestampSeconds = -1;
};
class Capture {
public:
    virtual ~Capture() = default;
    virtual void open(const Device& device, const Format& format) = 0;
    virtual bool read(Frame& bgr) = 0;
    virtual CaptureInfo info() const = 0;
};
std::unique_ptr<Capture> makeOpenCV();
bool openCVAvailable(bool retry = false);
#ifdef _WIN32
std::unique_ptr<Capture> makeMediaFoundation();
std::vector<Format> mfFormats(const Device& device);
#endif
struct Snapshot {
    Frame bgr;
    double measuredFps = 0;
    std::string error;
    std::string notice;
    std::vector<Control> controls;
    unsigned long long sequence = 0;
    unsigned long long controlsRevision = 0;
    bool running = false;
};
class Camera {
public:
    ~Camera();
    void start(Device device, Format format, Backend backend);
    void stop();
    void set(unsigned id, int value, bool automatic);
    Snapshot snapshot();
private:
    struct Change { unsigned id; int value; bool automatic; };
    void run(Device device, Format format, Backend backend);
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    std::mutex mutex_;
    Snapshot state_;
    std::vector<Change> changes_;
};
}

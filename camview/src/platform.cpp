#include "camera.h"
#include <algorithm>
#include <stdexcept>
#include <numeric>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#elif defined(__linux__)
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#endif

namespace camview {
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
static std::string utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
class ComScope {
    HRESULT hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
public:
    ComScope() {
        if (FAILED(hr_) && hr_ != RPC_E_CHANGED_MODE) throw std::runtime_error("COM 初始化失败");
    }
    ~ComScope() { if (SUCCEEDED(hr_)) CoUninitialize(); }
};
static ComPtr<IEnumMoniker> monikers() {
    ComPtr<ICreateDevEnum> enumerator;
    ComPtr<IEnumMoniker> result;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&enumerator))))
        throw std::runtime_error("无法枚举 DirectShow 设备");
    const HRESULT hr = enumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &result, 0);
    if (FAILED(hr)) throw std::runtime_error("无法枚举视频输入设备");
    return result;
}
static std::string property(IMoniker* moniker, const wchar_t* name) {
    ComPtr<IPropertyBag> bag;
    if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) return {};
    VARIANT value;
    VariantInit(&value);
    std::string result;
    if (SUCCEEDED(bag->Read(name, &value, nullptr)) && value.vt == VT_BSTR) result = utf8(value.bstrVal);
    VariantClear(&value);
    return result;
}
std::vector<Device> enumerateDevices() {
    ComScope com;
    auto list = monikers();
    std::vector<Device> result;
    if (!list) return result;
    ComPtr<IMoniker> moniker;
    int index = 0;
    while (list->Next(1, &moniker, nullptr) == S_OK) {
        auto path = property(moniker.Get(), L"DevicePath");
        result.push_back({property(moniker.Get(), L"FriendlyName"), path, index++, path});
        moniker.Reset();
    }
    return result;
}
static ComPtr<IBaseFilter> bindFilter(const Device& device) {
    auto list = monikers();
    if (list) {
        ComPtr<IMoniker> moniker;
        int index = 0;
        while (list->Next(1, &moniker, nullptr) == S_OK) {
            const bool match = device.path.empty() ? index == device.index :
                property(moniker.Get(), L"DevicePath") == device.path;
            ++index;
            if (match) {
                ComPtr<IBaseFilter> filter;
                if (FAILED(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter))))
                    throw std::runtime_error("无法访问摄像头控制接口");
                return filter;
            }
            moniker.Reset();
        }
    }
    throw std::runtime_error("摄像头已移除");
}
class WindowsControls final : public Controls {
    ComPtr<IBaseFilter> filter_;
    ComPtr<IAMVideoProcAmp> proc_;
    ComPtr<IAMCameraControl> camera_;
public:
    explicit WindowsControls(const Device& device) : filter_(bindFilter(device)) {
        filter_.As(&proc_);
        filter_.As(&camera_);
    }
    std::vector<Control> read() override {
        static const char* procNames[] = {"亮度", "对比度", "色调", "饱和度", "清晰度", "伽马", "彩色启用", "白平衡", "逆光补偿", "增益"};
        static const char* cameraNames[] = {"水平转动", "垂直转动", "旋转", "变焦", "曝光", "光圈", "焦距"};
        std::vector<Control> result;
        for (int group = 0; group < 2; ++group) {
            const int count = group ? 7 : 10;
            for (int id = 0; id < count; ++id) {
                long low = 0, high = 0, step = 0, def = 0, caps = 0, value = 0, flags = 0;
                HRESULT hr = E_NOINTERFACE;
                if (!group && proc_) hr = proc_->GetRange(id, &low, &high, &step, &def, &caps);
                if (group && camera_) hr = camera_->GetRange(id, &low, &high, &step, &def, &caps);
                if (FAILED(hr)) continue;
                if (!group) hr = proc_->Get(id, &value, &flags);
                else hr = camera_->Get(id, &value, &flags);
                if (FAILED(hr)) continue;
                Control c;
                c.id = unsigned(group * 100 + id);
                c.group = group ? "Camera Control" : "ProCamp";
                c.name = group ? cameraNames[id] : procNames[id];
                c.minimum = low; c.maximum = high; c.step = std::max(1L, step);
                c.defaultValue = def; c.value = value;
                c.supportsAuto = (caps & 1) != 0;
                c.supportsManual = (caps & 2) != 0;
                c.automatic = (flags & 1) != 0;
                result.push_back(c);
            }
        }
        return result;
    }
    void set(unsigned id, int value, bool automatic) override {
        const long flags = automatic ? 1 : 2;
        HRESULT hr = E_NOINTERFACE;
        if (id < 100 && proc_) hr = proc_->Set(long(id), value, flags);
        if (id >= 100 && camera_) hr = camera_->Set(long(id - 100), value, flags);
        if (FAILED(hr)) throw std::runtime_error("驱动拒绝设置参数，请检查自动模式或设备占用");
    }
};
std::unique_ptr<Controls> openControls(const Device& device) { return std::make_unique<WindowsControls>(device); }
std::vector<Format> enumerateFormats(const Device& device, Backend backend) {
    ComScope com;
    if (backend == Backend::MediaFoundation) return mfFormats(device);
    auto filter = bindFilter(device);
    ComPtr<IAMVideoControl> videoControl;
    filter.As(&videoControl);
    ComPtr<IEnumPins> pins;
    if (FAILED(filter->EnumPins(&pins))) throw std::runtime_error("无法枚举 DirectShow 视频引脚");
    ComPtr<IPin> pin;
    std::vector<Format> formats;
    while (pins->Next(1, &pin, nullptr) == S_OK) {
        PIN_DIRECTION direction{};
        ComPtr<IAMStreamConfig> config;
        bool capturePin = SUCCEEDED(pin->QueryDirection(&direction)) && direction == PINDIR_OUTPUT;
        ComPtr<IKsPropertySet> properties;
        if (capturePin && SUCCEEDED(pin.As(&properties))) {
            GUID category{};
            DWORD returned = 0;
            if (SUCCEEDED(properties->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0,
                                          &category, sizeof(category), &returned)))
                capturePin = category == PIN_CATEGORY_CAPTURE;
        }
        if (capturePin && SUCCEEDED(pin.As(&config))) {
            int count = 0, bytes = 0;
            if (SUCCEEDED(config->GetNumberOfCapabilities(&count, &bytes)) && bytes > 0) {
                std::vector<BYTE> caps(size_t(bytes), 0);
                for (int index = 0; index < count; ++index) {
                    struct MediaType {
                        AM_MEDIA_TYPE* value = nullptr;
                        ~MediaType() {
                            if (!value) return;
                            CoTaskMemFree(value->pbFormat);
                            if (value->pUnk) value->pUnk->Release();
                            CoTaskMemFree(value);
                        }
                    } media;
                    if (config->GetStreamCaps(index, &media.value, caps.data()) != S_OK || !media.value) continue;
                    const auto* type = media.value;
                    if (type->majortype != MEDIATYPE_Video || !type->pbFormat) continue;
                    // The OpenCV 4.6 DirectShow subtype lookup does not include NV12.
                    if (type->subtype == MEDIASUBTYPE_NV12) continue;
                    LONG width = 0, height = 0;
                    REFERENCE_TIME defaultInterval = 0;
                    if (type->formattype == FORMAT_VideoInfo && type->cbFormat >= sizeof(VIDEOINFOHEADER)) {
                        const auto* header = reinterpret_cast<const VIDEOINFOHEADER*>(type->pbFormat);
                        width = header->bmiHeader.biWidth; height = std::abs(header->bmiHeader.biHeight);
                        defaultInterval = header->AvgTimePerFrame;
                    } else if (type->formattype == FORMAT_VideoInfo2 && type->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
                        const auto* header = reinterpret_cast<const VIDEOINFOHEADER2*>(type->pbFormat);
                        width = header->bmiHeader.biWidth; height = std::abs(header->bmiHeader.biHeight);
                        defaultInterval = header->AvgTimePerFrame;
                    }
                    if (width <= 0 || height <= 0) continue;
                    std::vector<LONGLONG> intervals;
                    if (defaultInterval > 0) intervals.push_back(defaultInterval);
                    if (videoControl) {
                        LONGLONG* rates = nullptr;
                        long size = 0;
                        if (SUCCEEDED(videoControl->GetFrameRateList(pin.Get(), index, SIZE{width, height}, &size, &rates)) && rates)
                            for (long i = 0; i < size; ++i) if (rates[i] > 0) intervals.push_back(rates[i]);
                        CoTaskMemFree(rates);
                    }
                    for (const auto interval : intervals) {
                        const auto divisor = std::gcd<LONGLONG>(10000000, interval);
                        const auto denominator = interval / divisor;
                        if (denominator > std::numeric_limits<unsigned>::max()) continue;
                        Format format{int(width), int(height), unsigned(10000000 / divisor), unsigned(denominator), type->subtype.Data1, -1};
                        const bool duplicate = std::any_of(formats.begin(), formats.end(), [&](const Format& old) {
                            return old.width == format.width && old.height == format.height && old.fourcc == format.fourcc &&
                                old.fpsNumerator == format.fpsNumerator && old.fpsDenominator == format.fpsDenominator;
                        });
                        if (!duplicate) formats.push_back(format);
                    }
                }
            }
        }
        if (!formats.empty()) break;
        pin.Reset();
    }
    return formats;
}
#elif defined(__linux__)
static int controlIoctl(int fd, unsigned long request, void* value) {
    int result;
    do { result = ioctl(fd, request, value); } while (result < 0 && errno == EINTR);
    return result;
}
struct File {
    int fd;
    explicit File(const std::string& path, int mode = O_RDWR) : fd(::open(path.c_str(), mode | O_NONBLOCK)) {}
    ~File() { if (fd >= 0) ::close(fd); }
};
std::vector<Device> enumerateDevices() {
    std::vector<Device> devices;
    for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
        const auto name = entry.path().filename().string();
        if (name.rfind("video", 0) != 0) continue;
        File file(entry.path().string());
        if (file.fd < 0) continue;
        v4l2_capability caps{};
        if (controlIoctl(file.fd, VIDIOC_QUERYCAP, &caps) < 0) continue;
        const auto flags = caps.capabilities & V4L2_CAP_DEVICE_CAPS ? caps.device_caps : caps.capabilities;
        if (!(flags & V4L2_CAP_VIDEO_CAPTURE)) continue;
        devices.push_back({reinterpret_cast<const char*>(caps.card), entry.path().string(), 0, {}});
    }
    std::sort(devices.begin(), devices.end(), [](const Device& a, const Device& b) { return a.path < b.path; });
    return devices;
}
std::vector<Format> enumerateFormats(const Device& device, Backend) {
    File file(device.path);
    if (file.fd < 0) throw std::runtime_error("无法访问设备：" + std::string(std::strerror(errno)));
    std::vector<Format> formats;
    v4l2_fmtdesc pixel{};
    pixel.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (; controlIoctl(file.fd, VIDIOC_ENUM_FMT, &pixel) == 0; ++pixel.index) {
        v4l2_frmsizeenum size{};
        size.pixel_format = pixel.pixelformat;
        for (; controlIoctl(file.fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
            // A continuous range is not a discrete mode table; do not invent entries.
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) continue;
            v4l2_frmivalenum interval{};
            interval.pixel_format = pixel.pixelformat;
            interval.width = size.discrete.width;
            interval.height = size.discrete.height;
            for (; controlIoctl(file.fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0; ++interval.index) {
                if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE || !interval.discrete.numerator) continue;
                formats.push_back({int(interval.width), int(interval.height), interval.discrete.denominator,
                                   interval.discrete.numerator, pixel.pixelformat, -1});
            }
        }
    }
    return formats;
}
class LinuxControls final : public Controls {
    File file_;
public:
    explicit LinuxControls(const Device& device) : file_(device.path) {
        if (file_.fd < 0) throw std::runtime_error("无法打开 V4L2 控制接口");
    }
    std::vector<Control> read() override {
        std::vector<Control> result;
        v4l2_queryctrl query{};
        query.id = V4L2_CTRL_FLAG_NEXT_CTRL;
        while (controlIoctl(file_.fd, VIDIOC_QUERYCTRL, &query) == 0) {
            const unsigned id = query.id;
            const bool supported = query.type == V4L2_CTRL_TYPE_INTEGER || query.type == V4L2_CTRL_TYPE_BOOLEAN ||
                query.type == V4L2_CTRL_TYPE_MENU || query.type == V4L2_CTRL_TYPE_INTEGER_MENU;
            if (!(query.flags & V4L2_CTRL_FLAG_DISABLED) && supported) {
                v4l2_control value{};
                value.id = id;
                if (controlIoctl(file_.fd, VIDIOC_G_CTRL, &value) == 0) {
                    Control c;
                    c.id = id;
                    c.group = V4L2_CTRL_ID2CLASS(id) == V4L2_CTRL_CLASS_CAMERA ? "Camera Control" : "ProCamp";
                    c.name = reinterpret_cast<const char*>(query.name);
                    c.minimum = query.minimum; c.maximum = query.maximum;
                    c.step = std::max(1, int(query.step)); c.defaultValue = query.default_value;
                    c.value = value.value;
                    c.writable = !(query.flags & (V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_GRABBED));
                    if (query.type == V4L2_CTRL_TYPE_MENU || query.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
                        for (int i = query.minimum; i <= query.maximum; ++i) {
                            v4l2_querymenu menu{};
                            menu.id = id; menu.index = unsigned(i);
                            if (controlIoctl(file_.fd, VIDIOC_QUERYMENU, &menu) == 0) {
                                const auto text = query.type == V4L2_CTRL_TYPE_INTEGER_MENU ? std::to_string(menu.value) :
                                    std::string(reinterpret_cast<const char*>(menu.name));
                                c.choices.emplace_back(i, text);
                            }
                        }
                    }
                    result.push_back(std::move(c));
                }
            }
            query.id = id | V4L2_CTRL_FLAG_NEXT_CTRL;
        }
        return result;
    }
    void set(unsigned id, int value, bool) override {
        v4l2_control control{};
        control.id = id; control.value = value;
        if (controlIoctl(file_.fd, VIDIOC_S_CTRL, &control) < 0)
            throw std::runtime_error("V4L2 设置失败：" + std::string(std::strerror(errno)));
    }
};
std::unique_ptr<Controls> openControls(const Device& device) { return std::make_unique<LinuxControls>(device); }
#else
#ifndef __APPLE__
std::vector<Device> enumerateDevices() { return {}; }
std::vector<Format> enumerateFormats(const Device&, Backend) { return {}; }
#endif
class EmptyControls final : public Controls {
public:
    std::vector<Control> read() override { return {}; }
    void set(unsigned, int, bool) override { throw std::runtime_error("此平台尚未实现 UVC 控制"); }
};
std::unique_ptr<Controls> openControls(const Device&) { return std::make_unique<EmptyControls>(); }
#endif
}

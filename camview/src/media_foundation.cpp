#include "camera.h"
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <sstream>
#include <stdexcept>

namespace camview {
using Microsoft::WRL::ComPtr;
static void check(HRESULT result, const char* message) {
    if (FAILED(result)) {
        std::ostringstream text;
        text << message << " (HRESULT 0x" << std::hex << unsigned(result) << ')';
        throw std::runtime_error(text.str());
    }
}
class MfRuntime {
public:
    MfRuntime() { check(MFStartup(MF_VERSION), "MF 初始化失败"); }
    ~MfRuntime() { MFShutdown(); }
};
static std::wstring wide(const std::string& value) {
    if (value.empty()) throw std::runtime_error("此设备没有 MF 符号链接，请使用 OpenCV 后端");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), size);
    return result;
}
static ComPtr<IMFMediaSource> createSource(const Device& device) {
    ComPtr<IMFAttributes> attrs;
    check(MFCreateAttributes(&attrs, 2), "无法创建 MF 属性");
    check(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "无法配置视频源");
    check(attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, wide(device.mfPath).c_str()), "无法指定视频设备");
    ComPtr<IMFMediaSource> source;
    check(MFCreateDeviceSource(attrs.Get(), &source), "MF 无法打开设备");
    return source;
}
struct Source {
    ComPtr<IMFMediaSource> value;
    ~Source() { if (value) value->Shutdown(); }
};
static ComPtr<IMFSourceReader> createReader(IMFMediaSource* source) {
    ComPtr<IMFAttributes> attrs;
    check(MFCreateAttributes(&attrs, 1), "无法创建读取属性");
    check(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "无法启用 RGB 转换");
    ComPtr<IMFSourceReader> reader;
    check(MFCreateSourceReaderFromMediaSource(source, attrs.Get(), &reader), "无法创建 MF Source Reader");
    check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "无法关闭默认流");
    check(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE), "无法选择视频流");
    return reader;
}
std::vector<Format> mfFormats(const Device& device) {
    MfRuntime runtime;
    Source source{createSource(device)};
    auto reader = createReader(source.value.Get());
    std::vector<Format> result;
    for (DWORD index = 0; ; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
        if (hr == MF_E_NO_MORE_TYPES) break;
        check(hr, "无法枚举 MF 格式");
        UINT32 width = 0, height = 0, num = 0, den = 0;
        GUID subtype{};
        if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
            FAILED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den)) || !den ||
            FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) continue;
        Format format{int(width), int(height), num, den, subtype.Data1, int(index)};
        const auto duplicate = std::find_if(result.begin(), result.end(), [&](const Format& old) {
            return old.width == format.width && old.height == format.height &&
                   old.fpsNumerator == num && old.fpsDenominator == den && old.fourcc == format.fourcc;
        });
        if (duplicate == result.end()) result.push_back(format);
    }
    return result;
}
class MfCapture final : public Capture {
    MfRuntime runtime_;
    Source source_;
    ComPtr<IMFSourceReader> reader_;
    int width_ = 0;
    int height_ = 0;
    LONG stride_ = 0;
    CaptureInfo info_;
    void updateFormat() {
        ComPtr<IMFMediaType> type;
        check(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type), "无法读取实际格式");
        UINT32 width = 0, height = 0;
        GUID subtype{};
        check(type->GetGUID(MF_MT_SUBTYPE, &subtype), "无法读取像素格式");
        if (subtype != MFVideoFormat_RGB32) throw std::runtime_error("MF 输出不是 RGB32");
        check(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height), "无法读取实际尺寸");
        width_ = int(width); height_ = int(height);
        UINT32 numerator = 0, denominator = 0;
        info_.reportedFps = SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) && denominator ?
            double(numerator) / denominator : 0;
        UINT32 stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) stride_ = LONG(stride);
        else check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride_), "无法计算图像步长");
    }
public:
    void open(const Device& device, const Format& format) override {
        source_.value = createSource(device);
        reader_ = createReader(source_.value.Get());
        if (format.nativeIndex >= 0) {
            ComPtr<IMFMediaType> native;
            check(reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, DWORD(format.nativeIndex), &native), "格式已失效");
            check(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, native.Get()), "设备拒绝所选格式");
        }
        ComPtr<IMFMediaType> output;
        check(MFCreateMediaType(&output), "无法创建 RGB 媒体类型");
        check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "无法设置视频类型");
        check(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "无法设置 RGB32");
        check(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.Get()), "MF 无法转换为 RGB32，请换格式");
        updateFormat();
        if (format.fpsNumerator) {
            ComPtr<IMFMediaType> current;
            check(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current), "无法核对帧率");
            UINT32 numerator = 0, denominator = 0;
            check(MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &numerator, &denominator), "无法读取实际帧率");
            if (!denominator || std::abs(double(numerator) / denominator - format.fps()) > 0.1)
                throw std::runtime_error("MF 实际帧率与所选格式不符");
        }
        if (format.width && (format.width != width_ || format.height != height_))
            throw std::runtime_error("MF 实际尺寸与所选格式不符");
    }
    bool read(cv::Mat& bgr) override {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const auto readStart = std::chrono::steady_clock::now();
        check(reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample), "MF 采集失败");
        const auto convertStart = std::chrono::steady_clock::now();
        info_.readMs = std::chrono::duration<double, std::milli>(convertStart - readStart).count();
        info_.convertMs = 0;
        info_.timestampSeconds = -1;
        if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))
            throw std::runtime_error("MF 视频流已结束或设备断开");
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) updateFormat();
        if (!sample) return false;
        info_.timestampSeconds = double(timestamp) / 10000000.0;
        ComPtr<IMFMediaBuffer> buffer;
        check(sample->ConvertToContiguousBuffer(&buffer), "无法获取 MF 图像缓冲区");
        cv::Mat bgra(height_, width_, CV_8UC4);
        ComPtr<IMF2DBuffer> surface;
        if (SUCCEEDED(buffer.As(&surface))) {
            BYTE* row = nullptr;
            LONG pitch = 0;
            check(surface->Lock2D(&row, &pitch), "无法锁定二维图像");
            if (std::abs(pitch) < width_ * 4) { surface->Unlock2D(); throw std::runtime_error("MF 图像步长无效"); }
            for (int y = 0; y < height_; ++y) std::memcpy(bgra.ptr(y), row + ptrdiff_t(y) * pitch, size_t(width_) * 4);
            surface->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD length = 0;
            check(buffer->Lock(&data, nullptr, &length), "无法锁定图像缓冲区");
            const auto stride = size_t(std::abs(stride_));
            if (stride < size_t(width_) * 4 || length < stride * size_t(height_)) {
                buffer->Unlock();
                throw std::runtime_error("MF 图像缓冲区长度不足");
            }
            const BYTE* first = stride_ < 0 ? data + stride * size_t(height_ - 1) : data;
            for (int y = 0; y < height_; ++y) std::memcpy(bgra.ptr(y), first + ptrdiff_t(y) * stride_, size_t(width_) * 4);
            buffer->Unlock();
        }
        cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        info_.convertMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - convertStart).count();
        return true;
    }
    CaptureInfo info() const override { return info_; }
};
std::unique_ptr<Capture> makeMediaFoundation() { return std::make_unique<MfCapture>(); }
}

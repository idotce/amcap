#include "camera.h"
#include "format_selection.h"
#ifndef _WIN32
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <share.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB
#endif
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;

class GlfwRuntime {
public:
    GlfwRuntime() {
        glfwSetErrorCallback([](int code, const char* message) {
            std::fprintf(stderr, "GLFW %d: %s\n", code, message);
        });
        if (!glfwInit()) throw std::runtime_error("无法初始化 GLFW，请检查显卡驱动与桌面显示环境");
    }
    ~GlfwRuntime() { glfwTerminate(); }
};

void loadFont() {
    auto& io = ImGui::GetIO();
    std::vector<fs::path> candidates;
#ifdef _WIN32
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(directory, MAX_PATH);
    if (length && length < MAX_PATH) {
        const auto fonts = fs::path(directory) / "Fonts";
        candidates = {fonts / "msyh.ttc", fonts / "simhei.ttf", fonts / "simsun.ttc"};
    }
#elif defined(__APPLE__)
    candidates = {"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Light.ttc"};
#else
    candidates = {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                  "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"};
#endif
    for (const auto& path : candidates) {
        if (fs::exists(path) && io.Fonts->AddFontFromFileTTF(path.u8string().c_str(), 18.0f, nullptr,
                                                           io.Fonts->GetGlyphRangesChineseSimplifiedCommon()))
            return;
    }
    io.Fonts->AddFontDefault();
    std::fprintf(stderr, "No Chinese font found; install a CJK font in one of the documented system locations.\n");
}

class ImGuiRuntime {
    bool glfwReady_ = false;
    bool rendererReady_ = false;
    void shutdown() {
        if (rendererReady_) ImGui_ImplOpenGL3_Shutdown();
        if (glfwReady_) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
public:
    ImGuiRuntime(GLFWwindow* window, const char* glsl) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        try {
            auto& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_ViewportsEnable;
            // Parameter windows stay independent even when moved over the main window.
            io.ConfigViewportsNoAutoMerge = true;
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            loadFont();
            ImGui::StyleColorsDark();
            auto& style = ImGui::GetStyle();
            style.WindowRounding = 0;
            style.FrameRounding = 4;
            style.WindowPadding = ImVec2(10, 10);
            glfwReady_ = ImGui_ImplGlfw_InitForOpenGL(window, true);
            if (!glfwReady_) throw std::runtime_error("无法初始化 ImGui 窗口后端");
            rendererReady_ = ImGui_ImplOpenGL3_Init(glsl);
            if (!rendererReady_) throw std::runtime_error("无法初始化 ImGui OpenGL 渲染器");
        } catch (...) { shutdown(); throw; }
    }
    ~ImGuiRuntime() { shutdown(); }
};

class VideoTexture {
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
public:
    ~VideoTexture() { clear(); }
    void clear() {
        if (id_) glDeleteTextures(1, &id_);
        id_ = 0; width_ = 0; height_ = 0;
    }
    void update(const camview::Frame& bgr) {
#ifdef _WIN32
        std::vector<unsigned char> rgb(*bgr.pixels);
        for (size_t i = 0; i + 2 < rgb.size(); i += 3) std::swap(rgb[i], rgb[i + 2]);
        const int cols = bgr.cols, rows = bgr.rows;
        const auto* data = rgb.data();
#else
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        const int cols = rgb.cols, rows = rgb.rows;
        const auto* data = rgb.data;
#endif
        if (!id_) {
            glGenTextures(1, &id_);
            glBindTexture(GL_TEXTURE_2D, id_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else glBindTexture(GL_TEXTURE_2D, id_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if (width_ != cols || height_ != rows) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cols, rows, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
            width_ = cols; height_ = rows;
        } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGB, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
};

#ifdef _WIN32
std::vector<unsigned char> encodePng(const camview::Frame& image) {
    using Microsoft::WRL::ComPtr;
    struct ComScope {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) throw std::runtime_error("无法初始化照片编码器");
    auto check = [](HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("PNG 编码失败"); };
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IStream> memory;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &memory));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(memory.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(UINT(image.cols), UINT(image.rows)));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat24bppBGR || image.pixels->size() > UINT_MAX)
        throw std::runtime_error("PNG 图像格式或尺寸不受支持");
    check(frame->WritePixels(UINT(image.rows), UINT(image.cols * 3), UINT(image.pixels->size()), image.pixels->data()));
    check(frame->Commit());
    check(encoder->Commit());
    STATSTG stat{};
    check(memory->Stat(&stat, STATFLAG_NONAME));
    if (stat.cbSize.HighPart) throw std::runtime_error("PNG 图像过大");
    HGLOBAL global = nullptr;
    check(GetHGlobalFromStream(memory.Get(), &global));
    const auto* data = static_cast<const unsigned char*>(GlobalLock(global));
    if (!data) throw std::runtime_error("无法读取 PNG 图像");
    std::vector<unsigned char> result(data, data + stat.cbSize.LowPart);
    GlobalUnlock(global);
    return result;
}
#endif

// Exclusive creation avoids overwriting captures, including simultaneous processes.
fs::path saveCapture(const camview::Frame& bgr) {
#ifdef _WIN32
    auto encoded = encodePng(bgr);
#else
    std::vector<uchar> encoded;
    if (!cv::imencode(".png", bgr, encoded)) throw std::runtime_error("PNG 编码失败");
#endif
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &seconds)) throw std::runtime_error("无法读取本地时间");
#else
    if (!localtime_r(&seconds, &local)) throw std::runtime_error("无法读取本地时间");
#endif
    char date[32]{};
    std::strftime(date, sizeof(date), "%Y%m%d_%H%M%S", &local);
    char base[64]{};
    std::snprintf(base, sizeof(base), "capture_%s_%03d", date, int(milliseconds));
    const auto directory = fs::current_path();
    for (unsigned suffix = 0; ; ++suffix) {
        const std::string name = std::string(base) + (suffix ? "_" + std::to_string(suffix) : "") + ".png";
        const auto path = directory / fs::u8path(name);
        int fd = -1;
#ifdef _WIN32
        const errno_t error = _wsopen_s(&fd, path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                                       _SH_DENYRW, _S_IREAD | _S_IWRITE);
        if (error == EEXIST) continue;
        if (error) throw std::runtime_error("无法创建照片，请检查当前目录写入权限");
        FILE* file = _wfdopen(fd, L"wb");
        if (!file) { _close(fd); fs::remove(path); throw std::runtime_error("无法打开照片文件"); }
#else
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 && errno == EEXIST) continue;
        if (fd < 0) throw std::runtime_error("无法创建照片，请检查当前目录写入权限");
        FILE* file = fdopen(fd, "wb");
        if (!file) { ::close(fd); fs::remove(path); throw std::runtime_error("无法打开照片文件"); }
#endif
        const bool written = std::fwrite(encoded.data(), 1, encoded.size(), file) == encoded.size();
        const bool closed = std::fclose(file) == 0;
        if (!written || !closed) { fs::remove(path); throw std::runtime_error("照片写入失败"); }
        return path;
    }
}

class Application {
    camview::Camera camera_;
    VideoTexture texture_;
    camview::Snapshot state_;
    std::vector<camview::Device> devices_;
    std::vector<camview::Format> formats_;
    std::unordered_map<unsigned, int> edits_;
    int deviceIndex_ = -1;
    int formatIndex_ = -1;
#ifdef _WIN32
    int backendIndex_ = 1;
#else
    int backendIndex_ = 0;
#endif
    bool opening_ = false;
    bool parametersOpen_ = false;
    bool focusParameters_ = false;
    unsigned long long sequence_ = 0;
    unsigned long long revision_ = 0;
    std::string status_;
    std::string lastError_;
    camview::Backend backend() const {
        return backendIndex_ == 1 ? camview::Backend::MediaFoundation : camview::Backend::OpenCV;
    }
    void stop() {
        camera_.stop(); opening_ = false;
        state_ = camera_.snapshot();
        status_ = "已停止";
    }
    void loadFormats() {
        formats_.clear(); formatIndex_ = -1;
        if (deviceIndex_ < 0) return;
        try {
            formats_ = camview::enumerateFormats(devices_.at(size_t(deviceIndex_)), backend());
            formatIndex_ = camview::closestFormat(formats_, 640, 480, 30.0, camview::mjpgFourcc());
        } catch (const std::exception& e) { status_ = e.what(); }
    }
    void scan() {
#ifdef _WIN32
        camview::openCVAvailable(true);
#endif
        stop(); devices_.clear(); deviceIndex_ = -1;
        try {
            devices_ = camview::enumerateDevices();
            if (!devices_.empty()) deviceIndex_ = 0;
            status_ = devices_.empty() ? "未发现摄像头，请检查连接与系统相机权限" : "请选择格式并打开摄像头";
        } catch (const std::exception& e) { status_ = e.what(); }
        loadFormats();
    }
    void start() {
        if (deviceIndex_ < 0) return;
        const auto format = formatIndex_ < 0 ? camview::Format{} : formats_.at(size_t(formatIndex_));
        camera_.start(devices_.at(size_t(deviceIndex_)), format, backend());
        opening_ = true; state_ = {}; sequence_ = 0; revision_ = 0;
        edits_.clear(); lastError_.clear(); texture_.clear();
        status_ = "正在打开摄像头…";
    }
    void changeSource() {
        const bool reopen = opening_ || state_.running;
        stop(); loadFormats();
        if (reopen) start();
    }
    void photograph() {
        if (state_.bgr.empty()) { status_ = "尚未收到图像"; return; }
        try { status_ = "已保存：" + saveCapture(state_.bgr).u8string(); }
        catch (const std::exception& e) { status_ = e.what(); }
    }
    void toolbar() {
        ImGui::SetNextItemWidth(170);
        const char* name = deviceIndex_ < 0 ? "未发现摄像头" : devices_.at(size_t(deviceIndex_)).name.c_str();
        if (ImGui::BeginCombo("##device", name)) {
            for (size_t i = 0; i < devices_.size(); ++i) {
                ImGui::PushID(int(i));
                if (ImGui::Selectable(devices_[i].name.c_str(), deviceIndex_ == int(i))) {
                    deviceIndex_ = int(i); changeSource();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(155);
#ifdef _WIN32
        if (ImGui::BeginCombo("##backend", backendIndex_ == 1 ? "Media Foundation" : "OpenCV")) {
            const bool available = camview::openCVAvailable();
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(available ? "OpenCV" : "OpenCV（不可用）", backendIndex_ == 0)) {
                backendIndex_ = 0; changeSource();
            }
            ImGui::EndDisabled();
            if (ImGui::Selectable("Media Foundation", backendIndex_ == 1)) { backendIndex_ = 1; changeSource(); }
            ImGui::EndCombo();
        }
#else
        ImGui::BeginDisabled(); ImGui::Combo("##backend", &backendIndex_, "OpenCV\0"); ImGui::EndDisabled();
#endif
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        const auto selected = formatIndex_ < 0 ? camview::Format{} : formats_.at(size_t(formatIndex_));
        const auto resolution = selected.width ? std::to_string(selected.width) + " x " + std::to_string(selected.height) : "设备默认";
        if (ImGui::BeginCombo("##resolution", resolution.c_str())) {
            for (size_t i = 0; i < formats_.size(); ++i) {
                const auto& f = formats_[i];
                if (f.width <= 0 || f.height <= 0) continue;
                bool seen = false;
                for (size_t j = 0; j < i; ++j)
                    if (formats_[j].width == f.width && formats_[j].height == f.height) { seen = true; break; }
                if (seen) continue;
                const auto label = std::to_string(f.width) + " x " + std::to_string(f.height);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(label.c_str(), f.width == selected.width && f.height == selected.height)) {
                    const int next = camview::closestFormat(formats_, f.width, f.height, selected.fps(), selected.fourcc, true);
                    if (next >= 0 && next != formatIndex_) {
                        formatIndex_ = next;
                        if (opening_ || state_.running) start();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105);
        char fpsLabel[32];
        if (formatIndex_ >= 0) std::snprintf(fpsLabel, sizeof(fpsLabel), "%.3f FPS", selected.fps());
        else std::snprintf(fpsLabel, sizeof(fpsLabel), "默认帧率");
        if (ImGui::BeginCombo("##fps", fpsLabel)) {
            for (size_t i = 0; i < formats_.size(); ++i) {
                const auto& f = formats_[i];
                if (f.width != selected.width || f.height != selected.height || !f.fpsDenominator || !f.fpsNumerator) continue;
                bool seen = false;
                for (size_t j = 0; j < i; ++j)
                    if (formats_[j].width == f.width && formats_[j].height == f.height && camview::sameRate(formats_[j], f)) { seen = true; break; }
                if (seen) continue;
                char label[32]; std::snprintf(label, sizeof(label), "%.3f FPS", f.fps());
                ImGui::PushID(int(i));
                if (ImGui::Selectable(label, camview::sameRate(f, selected))) {
                    int next = int(i);
                    for (size_t j = 0; j < formats_.size(); ++j)
                        if (formats_[j].width == f.width && formats_[j].height == f.height &&
                            camview::sameRate(formats_[j], f) && formats_[j].fourcc == selected.fourcc) { next = int(j); break; }
                    if (next != formatIndex_) {
                        formatIndex_ = next;
                        if (opening_ || state_.running) start();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        auto pixelLabel = [](unsigned fourcc) {
            std::string name;
            for (int n = 0; n < 4; ++n) {
                const char c = char((fourcc >> (n * 8)) & 0xff);
                name += c >= 32 && c <= 126 ? c : '?';
            }
            return name;
        };
        const auto pixel = formatIndex_ < 0 ? std::string("默认格式") : pixelLabel(selected.fourcc);
        if (ImGui::BeginCombo("##pixel", pixel.c_str())) {
            for (size_t i = 0; i < formats_.size(); ++i) {
                const auto& f = formats_[i];
                if (f.width != selected.width || f.height != selected.height || !camview::sameRate(f, selected)) continue;
                bool seen = false;
                for (size_t j = 0; j < i; ++j)
                    if (formats_[j].width == f.width && formats_[j].height == f.height &&
                        camview::sameRate(formats_[j], f) && formats_[j].fourcc == f.fourcc) { seen = true; break; }
                if (seen) continue;
                const auto label = pixelLabel(f.fourcc);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(label.c_str(), f.fourcc == selected.fourcc) && int(i) != formatIndex_) {
                    formatIndex_ = int(i);
                    if (opening_ || state_.running) start();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); if (ImGui::Button("刷新设备")) scan();
        ImGui::SameLine(); ImGui::BeginDisabled(deviceIndex_ < 0 || opening_ || state_.running);
        if (ImGui::Button("打开")) start();
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::BeginDisabled(!opening_ && !state_.running);
        if (ImGui::Button("停止")) stop();
        ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button("拍照")) photograph();
        ImGui::SameLine(); if (ImGui::Button("参数")) { parametersOpen_ = true; focusParameters_ = true; }
    }
    void preview() {
        const auto start = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y - ImGui::GetFrameHeightWithSpacing()));
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), IM_COL32(20, 22, 26, 255));
        if (texture_.id()) {
            const float scale = std::min(size.x / texture_.width(), size.y / texture_.height());
            const ImVec2 imageSize(texture_.width() * scale, texture_.height() * scale);
            const ImVec2 pos(start.x + (size.x - imageSize.x) / 2, start.y + (size.y - imageSize.y) / 2);
            draw->AddImage(static_cast<ImTextureID>(texture_.id()), pos, ImVec2(pos.x + imageSize.x, pos.y + imageSize.y));
        }
        char info[128]{};
        std::string overlay;
        if (texture_.id()) {
            std::snprintf(info, sizeof(info), "%d x %d  |  %.1f FPS", texture_.width(), texture_.height(), state_.measuredFps);
            overlay = info;
            if (!state_.running) overlay += "  已停止";
        } else overlay = opening_ ? "等待图像…" : "未打开摄像头";
        const ImVec2 pos(start.x + 12, start.y + 10);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) draw->AddText(ImVec2(pos.x + dx, pos.y + dy), IM_COL32_BLACK, overlay.c_str());
        draw->AddText(pos, IM_COL32_WHITE, overlay.c_str());
        ImGui::Dummy(size);
        ImGui::TextUnformatted(status_.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", status_.c_str());
    }
    void setValue(const camview::Control& control, int value) {
        const long long low = control.minimum;
        const long long clamped = std::clamp(static_cast<long long>(value), low, static_cast<long long>(control.maximum));
        const int normalized = int(low + ((clamped - low) / std::max(1, control.step)) * std::max(1, control.step));
        camera_.set(control.id, normalized, false);
        edits_[control.id] = normalized;
    }
    void controls(const char* group) {
        bool found = false;
        for (const auto& control : state_.controls) {
            if (control.group != group) continue;
            found = true;
            ImGui::PushID(int(control.id));
            ImGui::SeparatorText(control.name.c_str());
            ImGui::Text("范围 %d ～ %d  步长 %d  默认 %d", control.minimum, control.maximum, control.step, control.defaultValue);
            if (control.choices.empty()) {
                ImGui::BeginDisabled(!state_.running || !control.writable || !control.supportsManual || control.automatic);
                ImGui::SetNextItemWidth(210);
                auto& edit = edits_[control.id];
                const int step = std::max(1, control.step);
                const int fastStep = int(std::min(static_cast<long long>(step) * 10, static_cast<long long>(std::numeric_limits<int>::max())));
                const bool changed = ImGui::InputInt("##value", &edit, step, fastStep);
                if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive())) setValue(control, edit);
                ImGui::EndDisabled();
            } else {
                ImGui::BeginDisabled(!state_.running || !control.writable);
                const char* selected = "未知值";
                for (const auto& choice : control.choices) if (choice.first == control.value) selected = choice.second.c_str();
                ImGui::SetNextItemWidth(210);
                if (ImGui::BeginCombo("##choice", selected)) {
                    for (const auto& choice : control.choices)
                        if (ImGui::Selectable(choice.second.c_str(), choice.first == control.value)) camera_.set(control.id, choice.first, false);
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
            }
            if (control.supportsAuto) {
                ImGui::SameLine();
                ImGui::BeginDisabled(!state_.running || !control.writable || !control.supportsManual);
                bool automatic = control.automatic;
                if (ImGui::Checkbox("自动", &automatic)) camera_.set(control.id, control.value, automatic);
                ImGui::EndDisabled();
            }
            ImGui::SameLine(); ImGui::BeginDisabled(!state_.running || !control.writable);
            if (ImGui::Button("默认")) camera_.set(control.id, control.defaultValue, control.automatic);
            ImGui::EndDisabled(); ImGui::PopID();
        }
        if (!found) ImGui::TextUnformatted(state_.running ? "驱动未提供此类控制项" : "打开摄像头后读取驱动支持的控制项");
    }
    void parameters() {
        if (!parametersOpen_) return;
        const auto* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(500, 650), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(main->Pos.x + 70, main->Pos.y + 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(440, 300), ImVec2(10000, 10000));
        if (focusParameters_) { ImGui::SetNextWindowFocus(); focusParameters_ = false; }
        if (ImGui::Begin("摄像头参数", &parametersOpen_, ImGuiWindowFlags_NoDocking)) {
            ImGui::TextUnformatted("默认按钮恢复默认数值，保持当前自动／手动模式");
            ImGui::BeginDisabled(!state_.running || state_.controls.empty());
            if (ImGui::Button("恢复所有可写参数的默认数值"))
                for (const auto& control : state_.controls)
                    if (control.writable) camera_.set(control.id, control.defaultValue, control.automatic);
            ImGui::EndDisabled();
            if (ImGui::BeginTabBar("##groups")) {
                if (ImGui::BeginTabItem("ProCamp")) { controls("ProCamp"); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Camera Control")) { controls("Camera Control"); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
public:
    Application() { scan(); }
    ~Application() { camera_.stop(); }
    ImVec2 initialWindowSize() const {
        const auto& style = ImGui::GetStyle();
        const char* buttons[] = {"刷新设备", "打开", "停止", "拍照", "参数"};
        float width = 170 + 155 + 120 + 105 + 100 + style.ItemSpacing.x * 9 + style.WindowPadding.x * 2;
        for (const auto* button : buttons)
            width += ImGui::CalcTextSize(button).x + style.FramePadding.x * 2;
        width = std::ceil(width);
        float ratio = 480.0f / 640.0f;
        if (formatIndex_ >= 0) {
            const auto& format = formats_.at(size_t(formatIndex_));
            if (format.width > 0 && format.height > 0) ratio = float(format.height) / format.width;
        }
        const float previewHeight = (width - style.WindowPadding.x * 2) * ratio;
        const float height = std::ceil(previewHeight + style.WindowPadding.y * 2 +
                                       ImGui::GetFrameHeightWithSpacing() * 2 + style.ItemSpacing.y + 1);
        return ImVec2(width, height);
    }
    void poll() {
        state_ = camera_.snapshot();
        if (state_.running && opening_) {
            opening_ = false;
            status_ = "预览中";
        }
        if (state_.sequence != sequence_) {
            sequence_ = state_.sequence;
            if (!state_.bgr.empty()) texture_.update(state_.bgr);
        }
        if (state_.controlsRevision != revision_) {
            revision_ = state_.controlsRevision;
            for (const auto& control : state_.controls) edits_[control.id] = control.value;
            if (!state_.notice.empty()) status_ = state_.notice;
        }
        if (!state_.error.empty() && lastError_ != state_.error) {
            lastError_ = state_.error; status_ = state_.error; opening_ = false;
        }
    }
    void draw() {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::Begin("CamView", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
        toolbar(); ImGui::Separator(); preview();
        ImGui::End();
        parameters();
    }
};

int run() {
    GlfwRuntime runtime;
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    const char* glsl = "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    const char* glsl = "#version 130";
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(1280, 800, "CamView - UVC 摄像头", nullptr, nullptr), glfwDestroyWindow);
    if (!window) throw std::runtime_error("无法创建 OpenGL 窗口，请检查显卡驱动是否支持 OpenGL 3.0");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(1);
    ImGuiRuntime imgui(window.get(), glsl);
    Application application;
    bool sized = false;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        glfwMakeContextCurrent(window.get());
        application.poll();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        if (!sized) {
            const auto size = application.initialWindowSize();
            glfwSetWindowSizeLimits(window.get(), int(size.x), 300, GLFW_DONT_CARE, GLFW_DONT_CARE);
            glfwSetWindowSize(window.get(), int(size.x), int(size.y));
            sized = true;
        }
        application.draw();
        ImGui::Render();
        int width = 0, height = 0;
        glfwGetFramebufferSize(window.get(), &width, &height);
        if (width > 0 && height > 0) {
            glViewport(0, 0, width, height);
            glClearColor(0.06f, 0.07f, 0.09f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        GLFWwindow* previous = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows(); ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(previous);
        if (width > 0 && height > 0) glfwSwapBuffers(window.get());
        else glfwWaitEventsTimeout(0.03);
    }
    return 0;
}
int entry() {
    try { return run(); }
    catch (const std::exception& e) {
#ifdef _WIN32
        const int length = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
        std::wstring message(size_t(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, message.data(), length);
        MessageBoxW(nullptr, message.c_str(), L"CamView", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "CamView: %s\n", e.what());
#endif
        return 1;
    }
}
}
#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return entry(); }
#else
int main() { return entry(); }
#endif

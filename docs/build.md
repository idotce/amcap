# 构建与使用

CamView 使用 C++17、Dear ImGui、GLFW、OpenGL 和 OpenCV，不再依赖 wxWidgets 或 Qt。ImGui 主界面尚未完整编译、链接或运行实测；Windows 采集核心已使用实际摄像头测试。

## 依赖

- Dear ImGui：固定 `v1.91.9b-docking`，必需源码已放入 `third_party/imgui`，约 3.4 MB；直接编译核心及 GLFW／OpenGL3 后端，不含 demo。
- GLFW：固定 3.4，CMake 下载约 1.1 MB 的源码 ZIP 并校验 SHA-256，在 `camview/_build/_deps/` 中静态构建；不安装系统级软件。离线可通过 `-DFETCHCONTENT_SOURCE_DIR_GLFW=D:/deps/glfw-3.4` 指定完整源码目录。
- OpenGL：由操作系统和显卡驱动提供，Windows／Linux 要求 OpenGL 3.0，macOS 使用 OpenGL 3.2 Core。
- OpenCV：使用 `camview/third_party/opencv` 中的官方 4.6.0 Windows x64 Release 动态包，不再使用 cmake_third。
- 中文字体：优先使用系统已有字体，Windows 微软雅黑／黑体／宋体，macOS 苹方／黑体，Linux Noto CJK／文泉驿。缺少这些字体时会退回默认字体，中文无法正常显示；不自动安装字体。

依赖来源与说明见 `camview/third_party/README.md`。旧构建目录中的 wxWidgets 缓存和产物不参与当前构建，不需要删除构建目录。

## Windows

使用 MSVC、Windows SDK 和项目内 OpenCV 4.6.0 Windows x64 Release 动态库。主程序、ImGui、GLFW 使用 `/MD`；目标机器需要 Visual C++ 运行库。Debug 动态库未提供，Windows 生成项目只包含 Release、RelWithDebInfo、MinSizeRel。

进入 `camview/` 后运行 `python build.py` 选择 `4`，重新配置、构建和安装；可执行文件及许可证直接放在 `camview/_install/` 根目录，不使用 `bin`、`share` 等子目录。该脚本日志在 `camview/_temp/build.log`。以下 CMake 命令均从 `camview/` 目录运行，Windows 优先使用 `C:\cmake\bin\cmake.exe`：

```powershell
C:\cmake\bin\cmake.exe -S . -B _build -A x64
```

经用户要求进行完整编译／安装时，使用已有脚本或下列命令：

```powershell
C:\cmake\bin\cmake.exe --build _build --config Release
C:\cmake\bin\cmake.exe --install _build --config Release --prefix _install
```

构建和安装会将可选 OpenCV 后端、OpenCV world、MSMF、FFmpeg DLL 与程序放在同一目录，并安装许可证。Windows 缺少 OpenCV 插件或主 DLL 时仍可使用 MF，补齐后点击刷新设备重试；MF、COM、WIC、OpenGL 由操作系统提供。

## Linux / macOS

需要在 `camview/third_party/opencv` 内提供目标平台对应的 OpenCV 动态包及 CMake 配置；目前只包含 Windows x64 动态库，Linux 和 macOS 均未验证。

GLFW 使用系统窗口和图形开发环境。Linux 配置为 X11 后端（原生 Wayland 不支持本应用所需的独立多视口窗口），需有 X11 和 OpenGL 开发库；macOS 需要 Xcode 工具链。macOS 从生成的应用包运行，相机权限用途说明保留在 Info.plist 中。

```sh
cmake -S . -B _build
# 用户要求完整编译后执行：
cmake --build _build
cmake --install _build --prefix _install
```

## 使用

- 顶部同一行选择摄像头、后端、分辨率、帧率、像素格式，然后点击“打开”。三个格式下拉框按设备支持的组合联动，默认优先匹配 640×480 30 FPS MJPG；正常预览状态只显示“预览中”。摄像头只显示名称，选择其他设备／后端／格式时，正在运行的采集会重开；切换失败会显示状态，不自动回退原格式。
- 初始窗口宽度按第一排控件计算，高度按默认选中格式的画面比例计算（无可用格式时按 4:3）；预览按原始比例显示，纹理仅在新图像到达时更新。左上角白字黑色描边，无底框，显示实际收到的尺寸及约一秒窗口统计的接收帧率。首次帧率统计完成前为 0 FPS。
- 点击“参数”打开独立系统窗口，默认关闭，内有 ProCamp 和 Camera Control 分页。可以独立移动、调整大小、关闭并重新打开；关闭参数窗口不停止采集，主预览始终占满主窗口内容区。
- 参数只显示驱动实际支持的项目、范围、步长、默认值及当前值。数值离开编辑框／确认后提交，按驱动范围与步长对齐，随后读取驱动结果；自动模式、菜单控制项和不可写状态同样由驱动决定。默认按钮仅恢复默认数值，保留当前自动／手动模式。
- “拍照”自动将当前图像保存到进程当前工作目录，不弹保存窗口。名称如 `capture_20261027_103015_123.png`，同名追加序号，使用独占创建避免覆盖；状态栏显示完整路径。PNG 不含左上角信息文字。

## 验证与限制

VS 2019 x64 配置／生成及 Windows 四个应用源文件的 MSVC `/Zs` 检查通过；生成项目中的应用、ImGui 和 GLFW 使用 `/MD`；仅 OpenCV 后端插件链接 OpenCV 导入库。未验证动态库链接、程序运行及 Linux/macOS。

未执行 ImGui 主应用完整编译、链接、安装或图形界面实测；多窗口打开／关闭、跨显示器 DPI、中文字体、纹理方向与颜色、中文目录拍照仍需实测。Windows 采集核心的独立测试工具已编译、链接并接入实际摄像头，结果见下文；这不等于主应用完成验证。

原有采集限制继续保留：

- Windows OpenCV 使用 DirectShow 自己的 IAMStreamConfig／IAMVideoControl 格式与帧率枚举，不再借用 MF 表。OpenCV 4.6 DirectShow 的像素格式映射不包含 NV12，因此 OpenCV 列表不显示 NV12；需要 NV12 时使用 MF。参数仍通过独立 DirectShow 控制对象访问，兼容性取决于驱动。
- Linux 暂只列出离散尺寸／帧率，没有连续／步进范围编辑器。macOS 列出格式帧率范围端点，尚未实现通用 UVC ProCamp／默认值控制，OpenCV 路径不保证源像素子类型。
- 枚举和停止采集仍可能受慢驱动影响而阻塞 UI。五秒无帧检测只在底层读取返回后生效，不能中断卡住的同步 read。
- 暂无热插拔自动恢复、视频录制、厂商扩展单元或持久化预设。

实现依据：[Dear ImGui GLFW/OpenGL3 示例](https://github.com/ocornut/imgui/blob/v1.91.9b-docking/examples/example_glfw_opengl3/main.cpp)、[GLFW 构建说明](https://www.glfw.org/docs/3.4/build_guide.html)。

## 1080p60 低帧率排查

2026-10-27 在 Windows 上使用实际 Owl3D Shift 摄像头测量：

- 驱动报告 1920×1080 的 NV12／MJPG 支持 60、30 FPS，YUY2 仅支持 5 FPS。
- 自动曝光开启时，无界面 MF/NV12 与 OpenCV/MJPG 采集均约 50 FPS，MF 帧时间戳间隔也约 50 FPS。
- **打开摄像头后**关闭自动曝光、保持该设备当前值 `-7`，完整采集线程 MF/NV12 测得约 59.95 FPS，OpenCV/MJPG 约 59.98 FPS。该数值仅是这台相机的测试结果，不在程序中写死，不对其他相机通用。
- 测试临时更改曝光模式后均恢复原值及自动模式。实际使用时，可在“参数 → Camera Control → 曝光”取消“自动”，再按画面亮度调整。打开／重建采集源可能重新初始化驱动状态，需以打开后的实际读回为准。
- OpenCV 4.6 DirectShow 设置 FPS 会重建图，原来的“FourCC → 分辨率 → FPS”顺序可能丢失所选像素格式。现改为 FPS → 分辨率 → FourCC，并校验像素格式读回。OpenCV 的 FPS 属性来自其请求值，不能当作实际传输帧率。
- 历史测试中额外 `Mat::clone` 约 3.03 ms；目前 Windows 已改为共享 BGR 帧缓冲，新的格式转换与复制性能尚未实测。

程序运行时不再创建 `_temp` 或写采集诊断日志；已有日志不会自动删除。构建脚本仍在其项目目录的 `_temp/build.log` 记录构建输出。

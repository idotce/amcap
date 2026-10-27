### 2026-10-27 14:25 Agent:Codex - AMCap 预览上限调整

# 需求
预览显示上限由 1920×1080 改为 800×600。

# 结论
- 修改：amcap.cpp 调整预览显示上限，仍等比例居中，不修改实际采集分辨率。
- 验证：Windows VS2019 Win32 MSVC /Zs、提取实际函数的横屏／竖屏／低分辨率缩放计算和 git diff --check 通过；未完整编译或实测摄像头。

### 2026-10-27 14:22 Agent:Codex - AMCap 预览显示尺寸上限

# 需求
切换摄像头分辨率时预览最大 1920×1080，不限制实际采集分辨率。

# 结论
- 修改：amcap.cpp 自动调整窗口和手动缩放时将渲染预览限制在 1920×1080 内，等比例缩放居中；修正窗口尺寸调整后传给预览的客户区尺寸，不修改摄像头格式。
- 验证：Windows VS2019 Win32 MSVC /Zs 通过；从实际缩放函数提取的轻量 C++ 测试覆盖 3840×2160、4096×2160、1080×1920、640×480、7680×1080，通过；git diff --check 通过。
- 状态：未完整编译、安装或进行 GUI／摄像头实测。

### 2026-10-27 14:16 Agent:Codex - CamView 使用 Release OpenCV 动态库

# 需求
CamView 不再依赖 cmake_third，下载动态 OpenCV 至 third_party；用户同意 /MD，仅保留 Release 并清除 Debug 库以减小 GitHub 体积。

# 结论
- 修改：camview/third_party/opencv 放置官方 OpenCV 4.6.0 Windows x64 Release headers、导入库、world、MSMF、FFmpeg DLL 及许可证；下载包 SHA256 为 3fb046e14dc5b50719a86ea0395b5b1e3299e2343111ebd2e31828aa31d6d477。Debug 文件清除，目录约 92 MiB，world DLL 约 61 MiB。
- 构建：camview/CMakeLists.txt 使用目录内动态库，应用／ImGui／GLFW /MD，自动复制运行 DLL 并平铺安装许可证；仅提供 Windows x64 Release 类型，非 Windows 须自行提供动态 OpenCV 包；修正迁移后的 ImGui 目录。
- 验证：Windows VS2019 x64 用 camview/build.py configure 通过，检查生成项目链接导入库、DLL 复制、/MD 配置与无 Debug；四个应用源文件 MSVC /Zs 通过。未完整编译、安装、运行或实测摄像头。
- 状态：GitHub 单文件超过 50 MB 可能警告；目标机器需 Visual C++ 运行库。Linux/macOS 的动态库及运行路径未提供或验证。

### 2026-10-27 14:03 Agent:Codex - AMCap 中文界面和预览状态

# 需求
AMCap 界面翻译为简体中文；状态栏显示当前分辨率、帧率等。

# 结论
- 修改：amcap.rc 菜单、对话框，amcap.cpp 动态菜单、主要错误及状态提示，crossbar.cpp 输入名称翻译；资源指定 UTF-8、简体中文，CMake 仅 C++ 启用 /utf-8。设备名称和驱动属性页仍由驱动提供。
- 预览状态：每秒从采集 pin 读实际配置的分辨率、帧率；枚举渲染器 IQualProp 显示开始预览以来的实际平均渲染帧率，不支持时明确显示不可用；录像时保留采集统计。
- 验证：Windows VS2019 Win32 /MT 的 cl /Zs 和 rc 资源编译通过，git diff --check 通过；没有完整编译、安装或 GUI／摄像头实测。
- 状态：图形界面排版、中文显示及真实设备的帧率统计仍待实测；用户需使用 build.py 构建。

### 2026-10-27 13:56 Agent:Codex - AMCap 配置文件移至程序目录

# 需求
配置保存在可执行文件目录的 amcap.ini。

# 结论
- 修改：amcap/amcap.cpp 用 GetModuleFileName 取得程序目录，所有 [annie] 配置项通过 GetPrivateProfileInt/String 和 WritePrivateProfileString 读写该目录 amcap.ini；路径获取或保存失败时提示；原 win.ini 不自动迁移。修正 CaptureFile 读取缓冲区字符数。
- 验证：Windows VS2019 Win32 Release /MT 条件下 cl /Zs 语法检查通过；git diff --check 通过；未完整编译、安装或实测读写。
- 状态：配置目录需可写；默认预览的上次修改也尚未重新编译。

### 2026-10-27 13:54 Agent:Codex - 启动默认预览与配置位置

# 需求
AMCap 启动默认预览；确认配置保存位置。

# 结论
- 修改：amcap/amcap.cpp 启动时将预览设为开启，菜单仍可切换，不再读取历史 WantPreview 值作为启动状态。
- 配置：原代码通过 WriteProfileString 写 win.ini 的 [annie]；本机只读检查发现 C:\Users\Administrator\AppData\Local\VirtualStore\Windows\win.ini 有该段，位置可能因虚拟化及注册表映射而异。用户确认上一轮程序可以使用。
- 验证：检查默认值及菜单切换逻辑、git diff --check 通过；未重新编译、安装或实测 GUI。

### 2026-10-27 13:52 Agent:Codex - AMCap 启动格式与目录迁移

# 需求
使用 build.py 编译 AMCap，删除旧 bat；适配 CamView 移动后的目录；启动选择最接近 640×480、30 FPS 的可用格式。

# 结论
- 修改：用户同意修复 BaseClasses/ctlutil.h 一处无效声明；删除 AMCap 根目录七个 bat；新增独立 CMake、build.py 的 Win32 生成器设置和构建目录忽略规则。CamView 的 CMake 适配移动后的 cmake_third 和共享 third_party 路径。
- 修改：AMCap 从设备枚举的媒体类型和可查询帧率中按分辨率、帧率选择；无保存设置时目标为 640×480、30 FPS，有保存设置时仍以保存值为目标；驱动拒绝时保留当前格式，读取实际格式并保护空接口及零帧间隔。
- 验证：Windows VS 2019 Win32 Release /MT 的 AMCap build.py 完整构建及安装，修改后增量构建及安装通过；构建与安装文件 SHA-256 一致，--version 正常。CamView 移动后配置通过。git diff --check 通过。
- 状态：用户报告直接打开出错，但未提供具体错误；未对修改后的 GUI 和摄像头进行实测，不能确认此前错误已解决。

### 2026-10-27 13:41 Agent:Codex - 分析 AMCap 并准备独立 CMake

# 需求
用户下载 AMCap 源码，询问摄像头是否由 MF 打开，要求不用原有 bat，提供 CMake 编译。

# 结论
- 结论：`amcap/` 为独立 Git 仓库及 Windows VS 2013 Win32 工程，源码使用 DirectShow：设备枚举、IBaseFilter、FilterGraph、CaptureGraphBuilder2 和 RenderStream；未找到 MF 采集调用。
- 修改：新增 `amcap/CMakeLists.txt` 独立构建入口，按原工程源文件清单构建应用及 DirectShow BaseClasses，默认 `/MT`（可选 `/MD`），使用 Windows SDK／ATL；构建目录生成 rev.h，不调用或修改原有 bat，不改原有源码。添加 `docs/amcap.md`、更新 `docs/AGENTS.md`、`docs/bugs.md`、`docs/todo.md`。
- 验证：Windows `C:/cmake/bin/cmake.exe -S .../amcap -B .../_build/amcap -G "Visual Studio 16 2019" -A Win32` 配置／生成通过；生成项目与原项目的应用 5、BaseClasses 29 个源文件一致，Release 都是 MultiThreaded（/MT）；从构建目录运行资源编译成功。尝试完整 Release 编译在 BaseClasses 的 `ctlutil.h:278` 报 C2143／C4430，尚未生成 AMCap 可执行文件或进行摄像头实测。
- 状态：头文件中的 `(LONG) operator=(LONG);` 属于无效 C++ 声明；已准备 `_temp/amcap-ctlutil-fix.patch`，拟修改为 SDK 7.1 中的 `LONG operator=(LONG);`，`git apply --check` 通过。BaseClasses 为微软第三方源码，按用户 AGENTS.md 的 Safety / Permissions 要求，修改前必须取得明确同意；当前未应用补丁，后续编译是否有其他错误尚未知。日志在 `_temp/amcap-configure.log`、`_temp/amcap-build.log`、`_temp/amcap-resource.log`。

### 2026-10-27 12:04 Agent:Codex - ImGui 迁移、1080p60 排查与平铺安装

# 需求
用户要求改用 ImGui（不用 wxWidgets），参数作为独立窗口；收窄摄像头／分辨率选择，状态栏只显示“预览中”；调查 MF 1080p60 四十多帧、OpenCV NV12 打不开且 1080p60 仅 5 帧；安装直接到目录根，不使用 bin／share。

# 结论
- 修改：`src/main.cpp` 改为 Dear ImGui + GLFW + OpenGL3，独立系统参数窗口含 ProCamp／Camera Control 标签，主预览不缩小；保留名称选择、白字黑轮廓、自动命名／独占保存 PNG。设备／格式宽度固定 170／260，预览提示简化。
- 依赖／安装：`third_party/imgui` 保存固定 v1.91.9b-docking 原样 16 文件约 3.4 MB；GLFW 3.4 固定 SHA-256 FetchContent 静态构建。CMake 移除 wxWidgets，保持 cmake_third OpenCV；应用／ImGui／GLFW Release 均 `/MT`。安装 EXE／应用包及许可证平铺于 `_install/` 根，不删除已有旧产物。
- 根因／修复：旧 OpenCV 设置顺序 FPS 放最后，使图重建丢掉 MJPG 变成 YUY2，而 getFPS 仍报 60。实机旧顺序四个 set 均成功、FourCC=YUY2，预热后实测 4.999 FPS。改为 FPS → 尺寸 → FourCC 并校验读回，OpenCV 用 DirectShow 专属格式／帧率枚举，NV12 不在该版本 DirectShow 类型映射内，滤除并明确走 MF。OpenCV 工作线程 STA、MF MTA。
- 性能：独立 CPU 基准 1080p clone 约 3.03 ms；Windows 每次 read 输出到独立 Mat，发布改为移动引用。`CaptureInfo` 和每秒 `_temp/capture-metrics.csv` 记录目标／后端报告／接收 FPS、MF 时间戳节奏、读／转／发布／控制耗时，不把 OpenCV 报告 FPS 当实际出帧。
- 实机：Windows MSVC 19.29、唯一摄像头 Owl3D Shift。1080p NV12／MJPG 支持 60／30，YUY2 仅 5；自动曝光直接 MF／OpenCV 约 50 FPS。打开后取消曝光自动，保持本设备 -7，完整 Camera 工作线程 MF/NV12 59.95、OpenCV/MJPG 59.98 FPS，旧帧快照不变。第一次在打开前设置曝光的试验低于 55 FPS；读回确认打开后又为自动模式，改为与 UI 一致的“打开后设置”，保持同一 55 FPS 断言通过。试验后恢复 -7、自动开启，另行读回确认，未保存摄像头图像。
- 验证：VS 2019 配置／生成、生成项目静态运行库／无 wxWidgets／平铺安装规则检查通过；Windows 四应用源文件与 ImGui 核心／后端 `/Zs` 通过，Linux main／camera／platform 以实际 GLFW3.4 和 cmake_third 通用头文件 `-fsyntax-only` 通过。项目 diff／whitespace 检查通过，未格式化第三方源码。
- 文档：更新 `docs/AGENTS.md`、`docs/build.md`、`docs/bugs.md`、`docs/todo.md`，依赖来源 `third_party/README.md`。临时测试与日志都在 `_temp/`，关键证据 `old-order-probe.log`、`camera-probe-manual.log`、`core-camera-test.log`、`core-camera-test/_temp/capture-metrics.csv`。
- 状态：独立测试工具已编译、链接及真机验证采集；未执行完整 ImGui 主应用编译、链接、安装或 GUI 实测。参数窗口／DPI／纹理／中文路径拍照仍待实测。Linux 缺少 cmake_third 预编译包、macOS 通用 UVC 控制未实现，保留既有边界。

### 2026-10-27 10:33 Agent:Codex - 简化界面、自动拍照与信息描边

# 需求
用户反馈程序可使用，要求摄像头只显示名称、分辨率选择同一行、拍照自动以 capture_日期时间 命名保存当前目录、ProCamp／Camera Control 可以隐藏；后续要求左上角无黑底、文字加轮廓。

# 结论
- 修改：`src/main.cpp` 设备列表只使用名称，内部索引／路径匹配不变；将格式选择移到顶栏同一行，设置选择框最小宽度并压缩按钮宽度。
- 修改：参数页和恢复默认按钮统一放入独立 panel，通过“隐藏参数／显示参数”整体切换，重新布局使预览区扩展，保留当前参数与控件状态。
- 修改：去掉拍照保存对话框，PNG 自动保存到 `wxGetCwd()`，文件名 `capture_YYYYMMDD_HHMMSS_mmm.png`；碰到已有文件时追加序号，使用禁止覆盖的 Create 模式并检查写入／关闭结果，状态栏显示完整路径。
- 修改：左上角去掉矩形黑底，透明背景下先绘制八方向 1 像素黑色轮廓，再绘制白字，保留文字位置与字体。
- 文档：同步 `docs/build.md` 使用方式及 `docs/AGENTS.md` 界面约定。
- 验证：现有 VS 2019 MSVC 19.29、实际 wxWidgets 生成头文件和 cmake_third OpenCV，四个应用源文件 `/Zs /std:c++17 /EHsc /utf-8 /W4 /permissive- /MT` Windows 语法检查通过（退出码 0），仍有既有第三方 C4996／MF 枚举 C4245 警告；修改前备份与当前源码已用 `git diff --no-index` 检查。日志 `_temp/ui-windows-syntax.log`。
- 状态：完成源码修改；遵循轻量验证限制，未重新执行完整编译、链接或 GUI／拍照运行实测。用户“可以使用”指修改前版本，不等同于本次改动已验证。macOS／Linux 本次未验证。
- 补充验证：本次描边修改后再次通过现有 VS 2019 MSVC Windows `/Zs` 检查，退出码 0；改前／改后 diff 及 whitespace 检查通过，日志 `_temp/outline-windows-syntax.log`。未完整编译或实测显示效果。

### 2026-10-27 10:11 Agent:Codex - 修复 Windows 缺少 wxWidgets 的配置流程

# 需求
用户构建菜单 all 在 OpenCV 已找到后，因找不到 wxWidgets core/base 失败；修复依赖准备流程，保留 OpenCV 来自 `../cmake_third` 和 Windows `/MT`。

# 结论
- 修改：`CMakeLists.txt` 先查找本地 wxWidgets，Windows 缺失时默认 FetchContent 获取固定 3.2.8.1 官方完整 ZIP，SHA-256 固定；设置静态库／静态 CRT，关闭示例／测试／语言包／WebView／媒体控件及开发文件安装；依赖目录排除默认 all，应用依赖仍正常构建。增加 C 语言支持与兼容版本的 CMP0135 设置。
- 修改：`build.py` 新日志改为 `_temp/build.log` 并创建日志目录，保留用户已有根目录日志；`src/camera.cpp` 显式包含 `objbase.h`，修复精简 Windows 头文件配置下的 COM 声明错误。未改动第三方源码和预编译库。更新 `docs/build.md`、`docs/AGENTS.md`、`docs/todo.md`、`docs/bugs.md`。
- 验证：使用 `C:/cmake/bin/cmake.exe -S D:/Project/camview -B D:/Project/camview/_build`，VS 2019 + MSVC 19.29 + Windows SDK 10.0.26100.0，完成实际网络下载、哈希校验、wxWidgets 配置／生成及重复配置，均通过。生成项目中应用、wxWidgets 及其内置依赖的 Release 运行库均为 MultiThreaded（`/MT`）；ALL_BUILD 只直接依赖应用和 ZERO_CHECK，未拉入无关 wxWidgets 模块。
- 验证：`vcvars64.bat` 后执行 MSVC `cl /Zs /std:c++17 /EHsc /utf-8 /W4 /permissive- /MT`，使用实际生成的 wxWidgets setup 头文件和 cmake_third OpenCV，对 `main.cpp camera.cpp platform.cpp media_foundation.cpp` 做 Windows 语法检查。首次发现 COM 声明缺失，修复后退出码 0；仍有第三方头文件 C4996 和 MF 枚举索引 C4245 警告。Python AST、diff whitespace 检查通过。
- 状态：此次配置错误已解决，可重新运行 `python build.py` 选择 4。未执行完整编译、链接、安装或 GUI／摄像头硬件验证；macOS 与 Linux 状态不变。检查日志及命令脚本均在 `_temp/`。

### 2026-10-27 09:59 Agent:Codex - 接入 cmake_third OpenCV

# 需求
按用户要求改用 `../cmake_third` 中的 OpenCV，保留静态链接要求，不修改第三方源码或预编译库。

# 结论
- 修改：`CMakeLists.txt` 直接引入 `../cmake_third/cmake/opencv.cmake`；按其 sample 链接 `OpenCV::World` 与 `${OPENCV_LIBRARIES}`；移除通用 `find_package(OpenCV)`、旧变量与 `OpenCV_STATIC`。检查头文件和主库在 `OPENCV_ROOT` 内，避免共享模块回退到系统路径。
- 文档：更新 `docs/build.md`、`docs/AGENTS.md` 和 `docs/todo.md`，说明 Release `/MT` 包、Debug 依赖限制和 Linux 产物缺失；不再要求 `OpenCV_DIR`。
- 验证：使用 Windows `C:\cmake\bin\cmake.exe` 对 `_temp/opencv-probe` 执行纯依赖定位配置（LANGUAGES NONE），找到 `D:/Project/cmake_third/prebuilt/windows/x64/opencv/x64/vc16/staticlib/opencv_world460.lib` 和同包头文件，导出 26 个显式链接项，配置／生成通过；未编译或链接程序。
- 验证：GCC 使用该 Windows 包中平台通用的 OpenCV 4.6 头文件，对 `src/camera.cpp src/platform.cpp` 的 Linux 分支执行 `-fsyntax-only`，通过；不能据此认定 Windows 分支已编译通过。CMake 文件 whitespace 检查通过。
- 状态：Linux `cmake -S . -B _build` 按预期因 `prebuilt/linux/x86_64/opencv` 缺失而失败，未回退系统 OpenCV；需在第三方仓库补齐对应平台包后继续 Linux 配置。未修改第三方仓库、未执行完整编译，wxWidgets 和硬件验证仍待完成。检查日志在 `_temp/cmake-third-*.log`。

### 2026-10-27 09:52 Agent:Codex - CamView 首版源码

# 需求
C++ 多平台 UVC 摄像头程序；不用 Qt 6；可选 OpenCV／MF 打开；Windows `/MT` 静态运行库；ProCamp／Camera Control 参数及默认数值；分辨率／帧率表与切换；左上角显示实际分辨率和帧率。

# 结论
- 修改：从空目录建立 CMake + wxWidgets 3.2 + OpenCV 4 项目。`src/main.cpp` 提供界面、模式切换、驱动参数编辑／默认值恢复及 PNG 拍照；`camera.cpp` 提供后台采集与 OpenCV 后端；`platform.cpp` 提供 DirectShow 控制／Windows 枚举和 V4L2 格式／控制；`media_foundation.cpp` 提供原生 MF；`macos.mm` 提供 AVFoundation 枚举。
- 修改：MSVC 配置 Release `/MT`、Debug `/MTd`，请求静态 wxWidgets／OpenCV；依赖必须由用户提供匹配版本，尚未核对静态依赖或链接结果。
- 验证：WSL/Linux GCC 11.4，`g++ -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only -I/usr/include/opencv4 src/camera.cpp src/platform.cpp` 通过。新文件 `git diff --no-index --check` 检查通过。
- 验证：`cmake -S . -B _build` 在查找 wxWidgets 3.2 时失败，环境缺少依赖。未执行完整编译；GUI、Windows、macOS 均未编译验证；无 `/dev/video*`，未做硬件实测。
- 状态：首版源码已建立，尚不是已验证的可运行交付。macOS 通用 UVC 参数控制缺失；Windows OpenCV 格式表借用 MF，需实测并改为 DirectShow 专属枚举；枚举及停止等待可能阻塞 UI，采集调用卡住不能被超时检测中断。默认恢复只恢复数值、保留当前自动模式。详见 `docs/build.md`、`docs/todo.md`。
- 环境：初始目录为空且不在 Git 仓库中，未初始化仓库。临时检查输出在 `_temp/`。

# 已确认问题

## AMCap 启动格式可能被设备拒绝

- 现象：用户报告直接打开出错，未提供具体弹窗；无法确认是否与格式设置有关。
- 原因：原代码将默认 640×360、15 FPS 直接写入当前媒体类型并调用 SetFormat，该组合可能不受驱动支持。
- 修改：默认目标改为 640×480、30 FPS，从驱动枚举的格式和可查询帧率中选择最接近项，失败时保留当前格式；保留已保存的用户选择。
- 验证：Windows VS 2019 Win32 Release 增量编译及安装通过；未实测 GUI 和摄像头，原报错是否解决尚未知。

### 2026-10-27 Agent:Codex - Windows 配置找不到 wxWidgets（历史：当前已迁移 ImGui）

- 现象：用户在构建菜单选择 all，OpenCV 成功定位，随后 `find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)` 报头文件及库缺失。
- 原因：此前仅提供依赖查找，机器和 `cmake_third` 中均无 wxWidgets，没有准备该依赖的流程。
- 修改：保留本地库查找；Windows 缺失时通过 FetchContent 下载固定 wxWidgets 3.2.8.1 官方完整 ZIP 并校验 SHA-256，以静态库和静态 CRT 随项目构建。可关闭自动获取，或用标准 FetchContent 源码目录变量进行离线配置。不修改第三方源码或预编译库。
- 验证：Windows VS 2019 完成实际下载、校验、配置和生成，重复配置通过；生成项目中应用与 wxWidgets 的 Release 运行库均为 MultiThreaded（`/MT`）。完整编译与 GUI／硬件运行尚未验证，检查结果见 `docs/session.md`。

### 2026-10-27 Agent:Codex - Windows 采集线程缺少 COM 声明

- 现象：MSVC `/Zs` 检查 `src/camera.cpp` 报 `COINIT_MULTITHREADED`、`CoInitializeEx`、`CoUninitialize` 未声明。
- 原因：启用 `WIN32_LEAN_AND_MEAN` 时，仅包含 `windows.h` 不会提供所需 COM 声明。
- 修改：在 Windows 分支显式包含 `objbase.h`。
- 验证：VS 2019 MSVC 19.29 对四个 Windows 应用源码的 `/Zs` 检查通过；仍有第三方头文件 C4996、MF 流索引枚举 C4245 警告，未进行完整编译／链接／运行验证。

### 2026-10-27 Agent:Codex - OpenCV 1080p60 实际只有 5 FPS

- 现象：用户选择 1080p60 后，OpenCV 只有约 5 FPS，NV12 无法打开。
- 复现：Windows + Owl3D Shift + cmake_third OpenCV 4.6，按旧顺序 FourCC(MJPG) → 宽 → 高 → FPS 设置，四次 set 全部返回成功，但 FourCC 读回为 YUY2、FPS 属性仍为 60；预热后实际测得 4.999 FPS。驱动的 1080p YUY2 本来只支持 5 FPS。
- 原因：OpenCV 4.6 DirectShow 的 FPS setter 重建采集图时没有保留像素格式，其 FPS getter 又是请求值。此前程序还借用 MF 格式表；该版本 DirectShow 支持的类型映射不含 NV12。
- 修改：OpenCV 改为 DirectShow IAMStreamConfig／IAMVideoControl 格式枚举，滤除不支持的 NV12；改用 FPS → 分辨率 → FourCC 顺序并检查 FourCC 实际读回。NV12 使用 MF。OpenCV 线程改用 STA，与上游内部 CoInitialize 一致；不修改第三方源码。
- 验证：改后完整采集线程选择实际支持的 MJPG 1080p60，打开后手动曝光 -7，测得 59.98 FPS；Windows 语法检查通过。独立采集测试已编译、链接并接入硬件，ImGui 主应用尚未完整编译或运行实测。

### 2026-10-27 Agent:Codex - MF 1080p60 低帧率的曝光与处理排查

- 现象：用户 MF 1080p60 左上角显示四十多帧。
- 测量：同机 Owl3D Shift，自动曝光时无界面 MF/NV12 和 OpenCV/MJPG 均约 50 FPS，MF 媒体时间戳约 50 FPS；完整 MF 采集线程自动曝光启动期间也出现约 38～49 FPS。打开后取消曝光自动、保持当前 -7 时，MF/NV12 约 59.95 FPS，OpenCV/MJPG 约 59.98 FPS。不要把全部差距笼统归为界面性能或 USB 带宽。
- 修改：Windows 移除发布时额外 Mat::clone（1080p 独立基准约 3.03 ms），将每次独立输出的 Mat 移动发布；Linux/macOS 保留复制。增加每秒采集耗时／帧率／MF 时间戳诊断 `_temp/capture-metrics.csv`，记录失败不阻止采集。
- 验证：持有旧快照跨越多帧后内容不变；两后端完整采集线程手动曝光试验均超过 59.9 FPS。实机对照后已恢复曝光 -7、自动模式开启，并再次读取确认。打开源可能重新初始化自动曝光，试验／实际操作应在打开后设置；曝光值不在程序中写死。
- 限制：这是该设备、该环境的采集路径测试，不能代表其他相机；独立 ImGui 参数窗口、预览渲染、拍照等图形行为尚未运行验证。

### 2026-10-27 Agent:Codex - AMCap BaseClasses 无效声明阻止编译

- 现象：独立 CMake 在 VS 2019 Win32 下配置通过，但构建 BaseClasses 预编译头时报 C2143／C4430，指向 `amcap/BaseClasses/ctlutil.h:278` 的 `(LONG) operator=(LONG);`。
- 原因：该源码中的赋值运算符声明不符合 C++ 语法；Windows SDK 7.1 的对应 DirectShow BaseClasses 版本使用 `LONG operator=(LONG);`。
- 修改：用户明确同意后，已将该声明修正为 `LONG operator=(LONG);`；原始补丁保存在 `_temp/amcap-ctlutil-fix.patch`。
- 验证：VS 2019 Win32 CMake 配置／生成、原工程源文件清单对照、Release `/MT` 设置和资源编译通过；随后执行 `amcap/build.py all` 完整编译并安装成功，`--version` 退出码 0。用户报告直接打开出错，运行问题另行排查。构建日志 `amcap/_temp/build.log`。

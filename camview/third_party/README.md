# 界面依赖源码

## Dear ImGui

- 来源：https://github.com/ocornut/imgui/tree/v1.91.9b-docking
- 固定版本：`v1.91.9b-docking`。使用 docking 分支的多视口功能创建独立系统参数窗口，没有启用停靠功能。
- 本目录保存上游原样文件：四个核心 `.cpp`、必要头文件、GLFW／OpenGL3 后端及 OpenGL 加载头，共 16 个文件，约 3.4 MB。不包含 demo，不需要额外 ImGui 二进制库。
- 上游许可证：`imgui/LICENSE.txt`。不修改这些第三方源码，业务代码在 `src/` 中。

## GLFW

GLFW 用于跨平台系统窗口、输入和 OpenGL 上下文。CMake 固定下载 3.4 源码 ZIP，校验 SHA-256，并在 `_build/_deps/` 中构建静态库；源码 ZIP 约 1.1 MB。不开启示例、测试、文档或 GLFW 安装。MSVC 使用动态 CRT `/MD`，与主程序保持一致。

- 来源：https://github.com/glfw/glfw/tree/3.4
- 离线可通过 `-DFETCHCONTENT_SOURCE_DIR_GLFW=完整源码目录` 使用预先解压的同版本源码。
- 不再下载或使用 wxWidgets。已有 `_build` 中旧的 wxWidgets 缓存不参与当前构建，不需要清理整个构建目录。

## OpenCV

- 来源：OpenCV 官方 4.6.0 Windows 包 https://github.com/opencv/opencv/releases/download/4.6.0/opencv-4.6.0-vc14_vc15.exe
- 下载包 SHA-256：`3fb046e14dc5b50719a86ea0395b5b1e3299e2343111ebd2e31828aa31d6d477`。
- `opencv/` 保存 x64 Release 的头文件、world 导入库及 DLL、MSMF 和 FFmpeg 插件及原始许可证，不包含 Debug 库，约 92 MiB。
- 主程序使用 `/MD`；构建和安装时复制动态库，目标机器需 Visual C++ 运行库。Linux/macOS 需另行提供对应动态库，尚未验证。

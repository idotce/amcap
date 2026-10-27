# AMCap 源码与 CMake

用户提供的 `amcap/` 是独立 Git 仓库。它基于 Windows DirectShow，使用 `CLSID_VideoInputDeviceCategory` 枚举设备，绑定 `IBaseFilter`，通过 `IGraphBuilder`、`ICaptureGraphBuilder2::RenderStream` 建立预览；没有找到使用 Media Foundation 采集的代码。这是 Windows 桌面程序，原 VS 2013 工程只提供 Win32 配置。

`amcap/CMakeLists.txt` 是独立工程，不依赖 OpenCV、wxWidgets 或 CamView。它按原 `.vcxproj` 构建 AMCap 五个 C++ 文件和附带的 29 个 DirectShow BaseClasses 文件，编译 `amcap.rc`，链接原工程中的 Windows 库；版本头文件 `rev.h` 在构建目录生成，Git 不可用时使用 `unknown`。保留原 BaseClasses 的 `/Gz` 和 Unicode 设置，默认采用 `/MT`，Debug 使用 `/MTd`；如需原工程的动态 CRT，可显式指定 `-DAMCAP_STATIC_RUNTIME=OFF`。

用户同意后已修复 `BaseClasses/ctlutil.h` 的一处无效声明，并删除 `amcap/` 内的原有 bat。原 `.vcxproj` 仍含已删除的 bat 预构建命令，应使用新的 CMake／build.py 入口。

进入 `amcap/` 后运行 `python build.py`，选择 4；脚本在 Windows 使用 VS 生成器时选择原工程的 Win32 架构，生成 `_build/`、`_install/`、`_temp/`。安装的 EXE 直接位于 `amcap/_install/amcap.exe`。

从工作区根目录也可使用 CMake：

```powershell
C:\cmake\bin\cmake.exe -S amcap -B amcap/_build -G "Visual Studio 16 2019" -A Win32
C:\cmake\bin\cmake.exe --build amcap/_build --config Release
C:\cmake\bin\cmake.exe --install amcap/_build --config Release --prefix amcap/_install
```

实际使用 Windows Python 3.13、VS 2019、Windows SDK 10.0.26100 和 ATL，执行 `amcap/build.py all` 完成配置、完整 Release 构建和安装；生成 Win32 `/MT` 程序 218,112 字节（213 KiB），并成功运行 `--version`。用户报告直接打开出错，具体错误尚在排查；已通过只读窗口检查确认当前 AMCap 进程存在且识别了 Owl3D Shift，不等同于预览功能已验证。日志位于 `amcap/_temp/build.log` 和工作区 `_temp/`。

启动时默认以 640×480、30 FPS 为目标，从设备报告的格式及可查询帧率中选择最接近项；存在保存设置时优先使用保存值作为目标，设置被拒绝时保留设备当前格式。修改后 Windows Win32 Release 增量构建及安装通过，但未进行 GUI 和摄像头实测；用户所述启动错误的具体内容尚未确认。

用户确认上一轮版本可以使用。启动预览已改为每次开启，菜单仍可切换；该修改未重新编译或实测。配置使用 Windows profile API，关闭时写入 win.ini 的 [annie]，本机该段存在于 `C:\Users\Administrator\AppData\Local\VirtualStore\Windows\win.ini`；实际位置受虚拟化或注册表映射影响。

当前源码中的 AMCap [annie] 配置已改为可执行文件同目录的 `amcap.ini`，关闭时保存；不自动迁移原系统 win.ini，目录需可写。Windows MSVC Win32 语法检查通过，尚未重新编译及运行验证。

当前源码的 AMCap 菜单、对话框及提示已翻译为简体中文；预览状态每秒显示采集分辨率、设置帧率和 IQualProp 实际平均渲染帧率（不可用时明示），录像时保留采集统计。驱动属性页和设备名称由驱动决定。Windows MSVC /Zs 和资源编译通过；完整构建及 GUI／摄像头实测待完成。

预览显示宽度限制为当前显示器逻辑宽度的 3/4，高度按画面比例缩放并居中，不限制实际采集分辨率；Win32 MSVC 语法与缩放计算轻量测试通过，GUI 和摄像头仍待实测。

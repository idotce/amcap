#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
desktop 通用构建脚本（CMake 项目）

支持平台: Windows / macOS / Linux

用法:
    python build.py               # 交互菜单
    python build.py configure     # 仅配置
    python build.py build         # 仅构建
    python build.py install       # 仅安装
    python build.py all           # 配置 + 构建 + 安装
    python build.py clean         # 清理（删除构建 / 安装目录）

说明:
    - 所有配置集中在下方 CONFIG 中，复制到其他 CMake 项目时按需修改即可；
    - 支持单配置生成器（Makefile/Ninja）与多配置生成器（VS/Xcode）；
    - generator / cmake_path / toolchain_file 等字段支持按平台配置：
      写成字符串表示所有平台共用，写成
      {"windows": ..., "macos": ..., "linux": ...} 字典则按当前平台取值，
      提交一份配置即可在 Windows / macOS / Linux 上免改。
"""

import atexit
import locale
import os
import platform
import shutil
import subprocess
import sys
import time

# ================================ 配置区 ================================
CONFIG = {
    # 构建 / 安装目录（相对本脚本所在目录）
    "build_dir": "_build",
    "install_dir": "_install",

    # 日志文件（相对本脚本目录），所有输出同时写入该文件
    "log_file": "_temp/build.log",

    # 构建类型: Debug / Release / RelWithDebInfo / MinSizeRel
    "build_type": "Release",

    # CMake 生成器: 留空自动选择（Windows 会选本机已装的最新 VS 版本）。
    # 可按平台分别指定（提交后各平台免改），例如:
    #   "generator": {"windows": "Visual Studio 17 2022", "macos": "Xcode", "linux": "Ninja"}
    # 常见取值: "Ninja" / "Visual Studio 17 2022" / "Unix Makefiles" / "Xcode"
    "generator": {
        "windows": "",
        "macos": "Xcode",
        "linux": "Ninja",
    },

    # CMake 可执行文件（留空使用 PATH 中的 cmake，可填绝对路径）。
    # 按平台分别指定；配置为路径但文件不存在时自动回退到 PATH 中的 cmake
    "cmake_path": {
        "windows": "C:/cmake/bin/cmake.exe",
        "macos": "cmake",
        "linux": "cmake",
    },

    # 交叉编译工具链文件（如 vcpkg.cmake、NDK android.toolchain.cmake），留空不使用。
    # 按平台分别指定，例如:
    #   "toolchain_file": {"windows": "", "macos": "", "linux": "/opt/ndk/android.toolchain.cmake"}
    "toolchain_file": {
        "windows": "",
        "macos": "",
        "linux": "",
    },

    # 附加 CMake 参数（字符串列表）
    "extra_cmake_args": [],

    # 并行编译数，0 = 交给构建系统决定
    "jobs": 0,

    # 代理（构建过程中下载依赖时使用），留空不使用
    "proxy": {
        "http": "",    # 例如 "http://127.0.0.1:7890"
        "https": "",
    },
}
# ========================================================================

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(SCRIPT_DIR, CONFIG["build_dir"])
INSTALL_DIR = os.path.join(SCRIPT_DIR, CONFIG["install_dir"])
LOG_PATH = os.path.join(SCRIPT_DIR, CONFIG.get("log_file", "_temp/build.log"))


class Tee:
    """同时输出到终端与日志文件的流包装。"""

    def __init__(self, stream, logfile):
        self._stream = stream
        self._logfile = logfile

    def write(self, data):
        self._stream.write(data)
        self._logfile.write(data)
        self._logfile.flush()
        return len(data)

    def flush(self):
        self._stream.flush()
        self._logfile.flush()

    def fileno(self):
        return self._stream.fileno()

    def isatty(self):
        return self._stream.isatty()


def setup_log():
    """用 Tee 包装 sys.stdout / sys.stderr，所有输出同时写入 _temp/build.log。"""
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    logfile = open(LOG_PATH, "a", encoding="utf-8", errors="replace")

    def _mark(text):
        logfile.write("\n===== %s %s =====\n"
                      % (text, time.strftime("%Y-%m-%d %H:%M:%S")))
        logfile.flush()

    _mark("构建日志开始")
    sys.stdout = Tee(sys.stdout, logfile)
    sys.stderr = Tee(sys.stderr, logfile)

    def _finish():
        _mark("构建日志结束")
        sys.stdout.flush()
        sys.stderr.flush()
        sys.stdout = sys.__stdout__
        sys.stderr = sys.__stderr__
        logfile.close()

    atexit.register(_finish)


def decode_output_line(line):
    """把子进程输出的一行字节解码为文本。

    先按 UTF-8 尝试（cmake 等输出 UTF-8）；解码失败则回退本地 ANSI
    代码页（中文 Windows 上 MSBuild / cl.exe 等输出 GBK），避免乱码。
    """
    line = line.rstrip(b"\r")
    try:
        return line.decode("utf-8")
    except UnicodeDecodeError:
        enc = locale.getpreferredencoding(False) or "gbk"
        return line.decode(enc, errors="replace")


def run_cmd(cmd, env=None, cwd=None):
    """执行命令，输出实时回显并同时写入 _temp/build.log。

    Windows 下同一构建可能混有 UTF-8（cmake）与本地 ANSI（MSBuild）输出，
    故按原始字节逐行读取并尝试解码，保证两种编码都能正常显示。
    """
    proc = subprocess.Popen(
        cmd, env=env, cwd=cwd,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    assert proc.stdout is not None
    buf = b""
    for chunk in iter(lambda: proc.stdout.read(65536), b""):
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            sys.stdout.write(decode_output_line(line) + "\n")
            sys.stdout.flush()
    if buf:
        sys.stdout.write(decode_output_line(buf) + "\n")
        sys.stdout.flush()
    return proc.wait()


def platform_key():
    """返回当前平台的短 key（windows / macos / linux）。"""
    if os.name == "nt":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def config_value(key):
    """解析 CONFIG 中可能按平台配置的字段。

    值可以是字符串（所有平台共用），也可以是
    {"windows": ..., "macos": ..., "linux": ...} 形式的字典
    （按当前平台取对应值，缺省平台回退为空字符串）。
    """
    value = CONFIG.get(key) or ""
    if isinstance(value, dict):
        value = value.get(platform_key(), "") or ""
    return value


def log(msg):
    print("[build] " + msg)


def error(msg):
    print("[build][ERROR] " + msg, file=sys.stderr)


def run(cmd):
    env = os.environ.copy()
    proxy = CONFIG.get("proxy") or {}
    if proxy.get("http"):
        env["http_proxy"] = env["HTTP_PROXY"] = proxy["http"]
    if proxy.get("https"):
        env["https_proxy"] = env["HTTPS_PROXY"] = proxy["https"]
    log("执行: " + " ".join(cmd))
    return run_cmd(cmd, env=env, cwd=SCRIPT_DIR)


def detected_generator():
    """从 CMakeCache.txt 读取实际使用的生成器（兼容生成器留空自动选择的情况）。"""
    cache = os.path.join(BUILD_DIR, "CMakeCache.txt")
    if os.path.isfile(cache):
        try:
            with open(cache, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    if line.startswith("CMAKE_GENERATOR:"):
                        return line.split("=", 1)[1].strip()
        except OSError:
            pass
    return ""


def is_multi_config():
    gen = config_value("generator") or detected_generator()
    return "Visual Studio" in gen or "Xcode" in gen


def cmake_bin():
    path = config_value("cmake_path") or "cmake"
    # 配置的是路径且文件不存在时，回退到 PATH 中的 cmake
    if os.path.sep in path or "/" in path or "\\" in path:
        if not os.path.isfile(path):
            return "cmake"
    return path


def cmake_base():
    return [cmake_bin()]


def configure():
    if not os.path.isdir(BUILD_DIR):
        os.makedirs(BUILD_DIR, exist_ok=True)
    args = cmake_base() + ["-S", SCRIPT_DIR, "-B", BUILD_DIR]
    gen = config_value("generator") or ""
    if gen:
        args += ["-G", gen]
    toolchain = config_value("toolchain_file") or ""
    if toolchain:
        args += ["-DCMAKE_TOOLCHAIN_FILE=" + toolchain]
    if not is_multi_config():
        args += ["-DCMAKE_BUILD_TYPE=" + CONFIG.get("build_type", "Release")]
    args += CONFIG.get("extra_cmake_args") or []
    return run(args)


def build():
    args = cmake_base() + ["--build", BUILD_DIR]
    if is_multi_config():
        args += ["--config", CONFIG.get("build_type", "Release")]
    jobs = CONFIG.get("jobs") or 0
    if jobs > 0:
        args += ["-j", str(jobs)]
    return run(args)


def install():
    args = cmake_base() + ["--install", BUILD_DIR, "--prefix", INSTALL_DIR]
    if is_multi_config():
        args += ["--config", CONFIG.get("build_type", "Release")]
    return run(args)


def all_in_one():
    ret = configure()
    if ret != 0:
        return ret
    ret = build()
    if ret != 0:
        return ret
    return install()


def clean():
    for d in (BUILD_DIR, INSTALL_DIR):
        if os.path.isdir(d):
            log("删除目录: " + d)
            shutil.rmtree(d, ignore_errors=True)
    return 0


ACTIONS = [
    ("configure", "配置 (configure)", configure),
    ("build", "构建 (build)", build),
    ("install", "安装 (install)", install),
    ("all", "配置 + 构建 + 安装 (all)", all_in_one),
    ("clean", "清理构建 / 安装目录 (clean)", clean),
]


def print_menu():
    print()
    print("========== desktop 构建菜单 ==========")
    for i, (key, desc, func) in enumerate(ACTIONS, 1):
        print("  %d. %s" % (i, desc))
    print("  x. 退出")
    print("======================================")


def interactive():
    while True:
        print_menu()
        choice = input("请选择: ").strip().lower()
        if choice in ("x", "q", "exit", "quit"):
            print("再见。")
            break
        if choice.isdigit() and 1 <= int(choice) <= len(ACTIONS):
            key, desc, func = ACTIONS[int(choice) - 1]
            ret = func()
            print("返回码: %d" % ret)
        else:
            print("无效选项，请重新输入。")


def print_config():
    print("---- 常用配置（如需修改：编辑 build.py 顶部 CONFIG 或环境变量）----")
    print("  cmake: %s" % cmake_bin())
    gen = config_value("generator") or "自动选择"
    print("  生成器: %s" % gen)
    print("  构建类型: %s" % CONFIG.get("build_type", "Release"))
    toolchain = config_value("toolchain_file") or "无（本机构建）"
    print("  工具链: %s" % toolchain)
    print("  构建目录: %s" % BUILD_DIR)
    print("  安装目录: %s" % INSTALL_DIR)
    proxy = CONFIG.get("proxy") or {}
    if proxy.get("http"):
        print("  代理 http: %s" % proxy["http"])
    if proxy.get("https"):
        print("  代理 https: %s" % proxy["https"])
    else:
        print("  代理: 无")


def main():
    setup_log()
    print("desktop 构建脚本 | 平台: %s | Python: %s"
          % (platform.system(), platform.python_version()))
    print_config()
    if len(sys.argv) > 1:
        action = sys.argv[1].lower()
        for key, desc, func in ACTIONS:
            if action == key:
                sys.exit(func() or 0)
        print("未知操作: %s" % action)
        print("可用操作: " + ", ".join(k for k, _, _ in ACTIONS))
        sys.exit(1)
    interactive()


if __name__ == "__main__":
    main()

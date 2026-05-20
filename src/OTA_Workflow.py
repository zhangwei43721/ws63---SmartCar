#!/usr/bin/env python3
"""一键 OTA: 编译 -> 打包 -> 自动发现并推送

固定使用 HiSpark SDK 自带的 Python 3.11 (含 distutils)，
并把 HiSpark 工具链 (cmake/ninja/riscv-gcc) 注入 PATH，
避免在普通 PowerShell 里报 "FileNotFoundError: cmake"。
"""

import os
import subprocess
import sys

TOOLS = r"C:\HiSpark_Tools\tools"
PY = rf"{TOOLS}\python\python.exe"

EXTRA_PATHS = [
    rf"{TOOLS}\python",
    rf"{TOOLS}\python\Scripts",
    rf"{TOOLS}\Windows\cc_riscv32_win_env",
    rf"{TOOLS}\Windows\cc_riscv32_win_env\bin",
    rf"{TOOLS}\Windows\ninja",
    rf"{TOOLS}\cfbb\thirdparty\ccache",
    rf"{TOOLS}\Windows\cc_riscv32_musl_fp_win\bin",
    rf"{TOOLS}\Windows\cc_riscv32_musl_win\bin",
    rf"{TOOLS}\Windows\gn",
    r"C:\Windows\System32",
]

STEPS = [
    [PY, "build.py", "ws63-liteos-app"],
    [PY, "build/config/target_config/ws63/build_ws63_update.py", "--pkt", "app"],
    [PY, "tools/ota_sender.py", "output/ws63/upgrade/update.fwpkg"],
]

env = os.environ.copy()
env["PATH"] = os.pathsep.join(EXTRA_PATHS) + os.pathsep + env.get("PATH", "")
env["PYTHONIOENCODING"] = "utf-8"
env["PYTHONUNBUFFERED"] = "1"

for cmd in STEPS:
    print(f"\n>>> {' '.join(cmd)}", flush=True)
    if subprocess.call(cmd, env=env) != 0:
        sys.exit(1)

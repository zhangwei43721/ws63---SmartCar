import os
import sys
import argparse
import re
from datetime import datetime

# ================= 配置部分 =================
BASE_PATH = "application/samples/peripheral"
AUTHOR_NAME = "SkyForever"
LICENSE_YEAR = "2024-2034"

# ================= 模板部分 =================
TEMPLATE_C = """/**
 ****************************************************************************************************
 * @file        {module_name}_example.c
 * @author      {author}
 * @version     V1.0
 * @date        {date}
 * @brief       {description}
 * @license     Copyright (c) {license_year}
 ****************************************************************************************************
 */

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "gpio.h"
#include "app_init.h"

#define TASK_STACK_SIZE 0x1000
#define TASK_PRIORITY    24

static void *{module_name}_task(const char *arg)
{
    unused(arg);

    printf("{module_name} init\\n");

    while (1) {
        // {description} implementation
        osal_msleep(1000);
    }
    return NULL;
}

static void {module_name}_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler){module_name}_task,
                                      NULL,
                                      "{module_name}_task",
                                      TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, TASK_PRIORITY);
    }
    osal_kthread_unlock();
}

app_run({module_name}_entry);
"""

TEMPLATE_CMAKE_SIMPLE = """
set(SOURCES "${{SOURCES}}" "${{CMAKE_CURRENT_SOURCE_DIR}}/{module_name}_example.c" PARENT_SCOPE)
"""

TEMPLATE_CMAKE_COMPLEX = """
set(HEADER_LIST ${{CMAKE_CURRENT_SOURCE_DIR}}/bsp_include)

set(SOURCES_LIST
    ${{CMAKE_CURRENT_SOURCE_DIR}}/bsp_src/bsp_{module_name}.c
    ${{CMAKE_CURRENT_SOURCE_DIR}}/{module_name}_example.c
)

set(SOURCES "${{SOURCES}}" "${{SOURCES_LIST}}" PARENT_SCOPE)
set(PUBLIC_HEADER "${{PUBLIC_HEADER}}" ${{HEADER_LIST}} PARENT_SCOPE)
"""

TEMPLATE_KCONFIG = """
config SAMPLE_SUPPORT_{upper_name}
    bool
    prompt "Support {title_name} Sample."
    default n
    depends on ENABLE_PERIPHERAL_SAMPLE
    help
        This option means support {title_name} Sample.

if SAMPLE_SUPPORT_{upper_name}
menu "{title_name} Sample Configuration"
    config {upper_name}_PARAM
        int "{title_name} parameter"
        default 100
        help
            This is a parameter for {module_name}.
endmenu
endif
"""

# ================= 工具类：汉化 ArgumentParser =================

class ChineseArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        """
        重写错误处理方法，将英文错误信息转换为中文
        """
        # 翻译 "the following arguments are required"
        if "the following arguments are required" in message:
            # 提取缺失的参数名
            missing_args = re.search(r': (.+)', message)
            args_str = missing_args.group(1) if missing_args else "未知"
            self.print_usage(sys.stderr)
            sys.stderr.write(f"\n错误: 缺少必要参数: {args_str}\n")
        
        # 翻译 "unrecognized arguments"
        elif "unrecognized arguments" in message:
            invalid_args = re.search(r': (.+)', message)
            args_str = invalid_args.group(1) if invalid_args else "未知"
            self.print_usage(sys.stderr)
            sys.stderr.write(f"\n错误: 无法识别的参数: {args_str}\n")
            
        # 其他错误直接翻译或输出
        else:
            self.print_usage(sys.stderr)
            sys.stderr.write(f"\n错误: {message}\n")
            
        sys.exit(2)

# ================= 功能逻辑 =================

def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)
        print(f"[创建目录] {path}")

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"[创建文件] {path}")

def inject_to_file(file_path, marker, content_to_insert, insert_after=True):
    if not os.path.exists(file_path):
        print(f"[错误] 找不到文件: {file_path}")
        return

    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    new_lines = []
    inserted = False
    
    if any(content_to_insert.strip() in line for line in lines):
        print(f"[跳过] 内容已存在于 {file_path}")
        return

    for line in lines:
        new_lines.append(line)
        if marker in line and not inserted:
            if insert_after:
                new_lines.append(content_to_insert + "\n")
            else:
                new_lines.insert(-1, content_to_insert + "\n")
            inserted = True
    
    if not inserted and "Kconfig" in file_path:
        new_lines.append("\n" + content_to_insert + "\n")
        inserted = True

    if inserted:
        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
        print(f"[更新] 已修改 {file_path}")
    else:
        print(f"[警告] 在 {file_path} 中未找到标记 '{marker}'，请手动修改。")

def main():
    # 使用自定义的汉化解析器
    parser = ChineseArgumentParser(
        description="自动化添加新外设模块脚本",
        add_help=False, # 禁用默认帮助，手动添加中文帮助
        usage="python add_module.py [选项] 模块名称"
    )

    # 手动添加帮助选项
    parser.add_argument('-h', '--help', action='help', default=argparse.SUPPRESS,
                        help='显示帮助信息并退出')

    parser.add_argument("name", help="模块名称 (例如: pwm_led，不要包含空格)")
    parser.add_argument("--desc", help="模块功能描述 (默认: New Module Sample)", default="New Module Sample")
    parser.add_argument("--complex", action="store_true", help="生成复杂结构 (包含 bsp_src/bsp_include 目录)")
    
    args = parser.parse_args()

    module_name = args.name.lower()
    upper_name = module_name.upper()
    title_name = module_name.replace("_", " ").title()
    module_path = os.path.join(BASE_PATH, module_name)

    print(f"=== 开始创建模块: {module_name} ===")

    # 1. 检查目录是否存在
    if os.path.exists(module_path):
        print(f"[错误] 模块 '{module_name}' 目录已存在，请勿重复创建。")
        sys.exit(1)

    # 2. 创建目录结构
    ensure_dir(module_path)
    if args.complex:
        ensure_dir(os.path.join(module_path, "bsp_include"))
        ensure_dir(os.path.join(module_path, "bsp_src"))
        write_file(os.path.join(module_path, "bsp_include", f"bsp_{module_name}.h"), f"#ifndef BSP_{upper_name}_H\n#define BSP_{upper_name}_H\n\n#endif")
        write_file(os.path.join(module_path, "bsp_src", f"bsp_{module_name}.c"), f'#include "bsp_{module_name}.h"\n')

    # 3. 生成文件
    c_content = TEMPLATE_C.format(
        module_name=module_name,
        author=AUTHOR_NAME,
        date=datetime.now().strftime("%Y-%m-%d"),
        description=args.desc,
        license_year=LICENSE_YEAR
    )
    write_file(os.path.join(module_path, f"{module_name}_example.c"), c_content)

    if args.complex:
        cmake_content = TEMPLATE_CMAKE_COMPLEX.format(module_name=module_name)
    else:
        cmake_content = TEMPLATE_CMAKE_SIMPLE.format(module_name=module_name)
    write_file(os.path.join(module_path, "CMakeLists.txt"), cmake_content)

    kconfig_content = TEMPLATE_KCONFIG.format(
        upper_name=upper_name,
        title_name=title_name,
        module_name=module_name
    )
    write_file(os.path.join(module_path, "Kconfig"), kconfig_content)

    # 4. 注册到上级文件
    parent_cmake = os.path.join(BASE_PATH, "CMakeLists.txt")
    parent_kconfig = os.path.join(BASE_PATH, "Kconfig")

    if not os.path.exists(parent_cmake):
        print(f"[警告] 未找到父级 CMake 文件: {parent_cmake}，跳过自动注册。")
    else:
        cmake_injection = f'if(DEFINED CONFIG_SAMPLE_SUPPORT_{upper_name})\n    add_subdirectory_if_exist({module_name})\nendif()'
        with open(parent_cmake, "a", encoding="utf-8") as f:
            f.write("\n" + cmake_injection + "\n")
        print(f"[更新] 已添加到 {parent_cmake}")

    if not os.path.exists(parent_kconfig):
        print(f"[警告] 未找到父级 Kconfig 文件: {parent_kconfig}，跳过自动注册。")
    else:
        kconfig_select = f"    select SAMPLE_SUPPORT_{upper_name}"
        inject_to_file(parent_kconfig, "depends on ENABLE_PERIPHERAL_SAMPLE", kconfig_select, insert_after=True)

    kconfig_source = f'if SAMPLE_SUPPORT_{upper_name}\nmenu "{title_name} Sample Configuration"\n    osource "{BASE_PATH}/{module_name}/Kconfig"\nendmenu\nendif'
    kconfig_source = f'if SAMPLE_SUPPORT_{upper_name}\nmenu "{title_name} Sample Configuration"\n    osource "{BASE_PATH}/{module_name}/Kconfig"\nendmenu\nendif'
    
    # 直接追加到文件末尾，通常 Kconfig 允许这样做
        kconfig_source = f'if SAMPLE_SUPPORT_{upper_name}\nmenu "{title_name} Sample Configuration"\n    osource "{BASE_PATH}/{module_name}/Kconfig"\nendmenu\nendif'
    
    # 直接追加到文件末尾，通常 Kconfig 允许这样做
        with open(parent_kconfig, "a", encoding="utf-8") as f:
            f.write("\n" + kconfig_source + "\n")
        print(f"[更新] 已添加 osource 到 {parent_kconfig}")

    print("\n[成功] 模块创建完成！")
    print(f"请运行 'python build.py menuconfig' 并启用 SAMPLE_SUPPORT_{upper_name}")

if __name__ == "__main__":
    main()
# 通用 App CMakeLists

# 1. 扫描当前目录下(即扁平化后的目录)的所有 .c 文件
file(GLOB APP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

# 2. 添加头文件路径 (对于 robot_demo 这种有本地 .h 的情况很重要)
# 这样 #include "bsp_robot_control.h" 就能直接找到
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# 3. 加入到全局构建源文件列表
list(APPEND SOURCES ${APP_SRCS})

# 4. 导出变量给上层
set(SOURCES ${SOURCES} PARENT_SCOPE)
# 如果 SDK 需要显式导出 include 路径，取消下面注释
# set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES} PARENT_SCOPE)
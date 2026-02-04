# SLE Test App CMakeLists

# 1. 扫描当前目录下的所有 .c 文件
file(GLOB APP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

# 2. 添加头文件路径
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/sle")

# 3. 加入到全局构建源文件列表
list(APPEND SOURCES ${APP_SRCS})

# 4. 导出变量给上层
set(SOURCES ${SOURCES} PARENT_SCOPE)

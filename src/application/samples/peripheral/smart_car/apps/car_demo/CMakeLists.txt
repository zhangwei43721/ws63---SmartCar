# 通用 App CMakeLists

# 1. 扫描当前目录下(即扁平化后的目录)的所有 .c 文件
file(GLOB_RECURSE APP_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/services/*.c"
)

# Exclude legacy + deleted/relocated files
list(REMOVE_ITEM APP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bsp_car_control.c")
list(REMOVE_ITEM APP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/services/storage_service.c")
list(REMOVE_ITEM APP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/services/car_proto.c")

# 2. 添加头文件路径 (对于 car_demo 这种有本地 .h 的情况很重要)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")                          # car_common.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/core")                     # mode_*.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/services")                 # *_service.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../platform")           # storage_service.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/motor_control") # bsp_motor.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../..")                    # board_config.h

# 3. 加入到全局构建源文件列表
list(APPEND SOURCES ${APP_SRCS})

# 4. 导出变量给上层
set(SOURCES ${SOURCES} PARENT_SCOPE)
# 如果 SDK 需要显式导出 include 路径，取消下面注释
# set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES} PARENT_SCOPE)
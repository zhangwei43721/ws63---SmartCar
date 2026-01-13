# Drivers CMake - 自动根据配置加载源码

# 定义临时变量收集源文件
set(DRIVER_SRCS "")

# --- L9110S ---
if(CONFIG_SMART_CAR_DRIVER_L9110S)
    # 扫描 l9110s 文件夹下的所有 .c 文件
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/l9110s/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    # 添加头文件路径 (就是 l9110s 文件夹本身)
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/l9110s")
endif()

# --- HC-SR04 ---
if(CONFIG_SMART_CAR_DRIVER_HCSR04)
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/hcsr04/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/hcsr04")
endif()

# --- TCRT5000 ---
if(CONFIG_SMART_CAR_DRIVER_TCRT5000)
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/tcrt5000/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/tcrt5000")
endif()

# --- SG90 ---
if(CONFIG_SMART_CAR_DRIVER_SG90)
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/sg90/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/sg90")
endif()

# --- SSD1306 ---
if(CONFIG_SMART_CAR_DRIVER_SSD1306)
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ssd1306/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ssd1306")
endif()

# --- WiFi ---
if(CONFIG_SMART_CAR_DRIVER_WIFI)
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/wifi_client/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/wifi_client")
endif()

# --- Bluetooth SPP ---
if(CONFIG_SMART_CAR_DRIVER_BT_SPP)
    file(GLOB_RECURSE CURRENT_DRIVER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/bt_spp_server/*.c")
    list(APPEND DRIVER_SRCS ${CURRENT_DRIVER_SRCS})
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/bt_spp_server")
endif()

# --- 汇总导出 ---
# 将收集到的所有驱动源文件加入全局构建列表
list(APPEND SOURCES ${DRIVER_SRCS})
set(SOURCES ${SOURCES} PARENT_SCOPE)

# 对于 include_directories，CMake 会自动处理作用域，
# 但为了保险起见，某些 SDK 需要显式导出，如果编译找不到头文件，可以取消下一行的注释
# set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES} PARENT_SCOPE)
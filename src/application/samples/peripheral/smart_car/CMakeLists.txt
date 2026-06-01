# Smart Car Top Level CMake

# 全局 include：所有子目录都能直接用模块名 include
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")                          # board_config.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/platform")                  # storage_service.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/apps/car_demo")           # car_common.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/apps/car_demo/core")      # mode_*.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/apps/car_demo/services")  # *_service.h
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/drivers/motor_control")     # bsp_motor.h

# 1. 编译平台层（NV 存储等共享基础设施）
add_subdirectory(platform)

# 2. 编译驱动库 (drivers/CMakeLists.txt 会决定具体编译哪些)
add_subdirectory(drivers)

# 3. 根据 Kconfig 选择编译哪个应用
if(CONFIG_SMART_CAR_RUN_CAR_DEMO)
    add_subdirectory(apps/car_demo)
elseif(CONFIG_SMART_CAR_RUN_L9110S_TEST)
    add_subdirectory(apps/test_l9110s)
elseif(CONFIG_SMART_CAR_RUN_HCSR04_TEST)
    add_subdirectory(apps/test_hcsr04)
elseif(CONFIG_SMART_CAR_RUN_TCRT5000_TEST)
    add_subdirectory(apps/test_tcrt5000)
elseif(CONFIG_SMART_CAR_RUN_SSD1306_TEST)
    add_subdirectory(apps/test_ssd1306)
elseif(CONFIG_SMART_CAR_RUN_WIFI_TEST)
    add_subdirectory(apps/test_wifi)
elseif(CONFIG_SMART_CAR_RUN_BT_SPP_TEST)
    add_subdirectory(apps/test_bt_spp)
elseif(CONFIG_SMART_CAR_RUN_SLE_TEST)
    add_subdirectory(apps/sle_test)
endif()

# 3. 关键步骤：将下层收集上来的变量暴露给 SDK 顶层
# 注意：WS63 SDK 通常通过 SOURCES 变量收集源文件
set(SOURCES ${SOURCES} PARENT_SCOPE)
set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES} PARENT_SCOPE)
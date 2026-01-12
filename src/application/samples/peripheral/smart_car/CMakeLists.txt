#===============================================================================
# @brief    cmake file for Smart Car
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2025-2025. All rights reserved.
#===============================================================================

# 根据配置选择编译哪些子模块
if(DEFINED CONFIG_SMART_CAR_L9110S_ENABLE)
    add_subdirectory_if_exist(l9110s)
endif()

if(DEFINED CONFIG_SMART_CAR_HCSR04_ENABLE)
    add_subdirectory_if_exist(hcsr04)
endif()

if(DEFINED CONFIG_SMART_CAR_TCRT5000_ENABLE)
    add_subdirectory_if_exist(tcrt5000)
endif()

if(DEFINED CONFIG_SMART_CAR_ROBOT_DEMO_ENABLE)
    add_subdirectory_if_exist(robot_demo)
endif()

if(DEFINED CONFIG_SMART_CAR_SG90_ENABLE)
    add_subdirectory_if_exist(sg90)
endif()

if(DEFINED CONFIG_SMART_CAR_SSD1306_ENABLE)
    add_subdirectory_if_exist(ssd1306)
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)

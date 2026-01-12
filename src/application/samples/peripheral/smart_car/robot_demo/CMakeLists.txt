#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2025-2025. All rights reserved.
#===============================================================================
# 添加头文件路径 (包含本模块和依赖的其他模块)
set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../l9110s/bsp_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../hcsr04/bsp_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../tcrt5000/bsp_include
)

# 添加源文件
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_src/bsp_sg90.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_src/bsp_robot_control.c
    ${CMAKE_CURRENT_SOURCE_DIR}/robot_demo.c
)

# 设置到父作用域
set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)

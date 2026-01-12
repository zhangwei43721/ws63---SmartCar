#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2025-2025. All rights reserved.
#===============================================================================
# 添加头文件路径
set(HEADER_LIST ${CMAKE_CURRENT_SOURCE_DIR}/bsp_include)

# 添加源文件
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_src/bsp_tcrt5000.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tcrt5000_example.c
)

# 设置到父作用域
set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)

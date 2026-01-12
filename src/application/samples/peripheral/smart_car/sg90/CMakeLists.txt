#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2025-2025. All rights reserved.
#===============================================================================

set(HEADER_LIST ${CMAKE_CURRENT_SOURCE_DIR}/bsp_include)

# 仅在启用 sg90 示例时编译源文件
if(DEFINED CONFIG_SMART_CAR_SUPPORT_SG90)
    set(SOURCES_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/bsp_src/bsp_sg90.c
        ${CMAKE_CURRENT_SOURCE_DIR}/sg90_example.c
    )
else()
    # 只编译BSP层，供其他模块使用
    set(SOURCES_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/bsp_src/bsp_sg90.c
    )
endif()

set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)

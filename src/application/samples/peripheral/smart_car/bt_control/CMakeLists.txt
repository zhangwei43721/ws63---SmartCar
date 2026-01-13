#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2025-2025. All rights reserved.
#===============================================================================

set(HEADER_LIST ${CMAKE_CURRENT_SOURCE_DIR})

# 仅在启用 bt_control 示例时编译源文件
if(DEFINED CONFIG_SMART_CAR_SUPPORT_BT_CONTROL)
    set(SOURCES_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/bt_control_example.c
    )
endif()

set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)

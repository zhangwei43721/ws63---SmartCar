#===============================================================================
# @brief    cmake file
# Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
#===============================================================================
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_wifi/wifi_connect.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_hrSpo2/algorithm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_hrSpo2/i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_hrSpo2/max30102_i2c_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_hrSpo2/max30102.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_hrSpo2/soft_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_dht11/bsp_dht11.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp_dht11/bsp_dht11.c
    ${CMAKE_CURRENT_SOURCE_DIR}/PAJ7620/PAJ_I2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/PAJ7620/PAJ7620.c
    ${CMAKE_CURRENT_SOURCE_DIR}/template.c
)

set(HEADER_LIST ${CMAKE_CURRENT_SOURCE_DIR}/wifi)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
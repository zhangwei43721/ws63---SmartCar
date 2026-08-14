# Chassis UART Test App CMakeLists

file(GLOB APP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/chassis_uart")

list(APPEND SOURCES ${APP_SRCS})
set(SOURCES ${SOURCES} PARENT_SCOPE)

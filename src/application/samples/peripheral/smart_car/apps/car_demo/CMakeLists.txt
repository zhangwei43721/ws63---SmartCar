# 通用 App CMakeLists

# ==============================================================================
# Pure CMake HTML Embedding Rules (Zero scripts, natively updates portal_html.h/c)
# ==============================================================================
set(PORTAL_HTML_C "${CMAKE_CURRENT_SOURCE_DIR}/services/portal_html.c")
set(PORTAL_HTML_H "${CMAKE_CURRENT_SOURCE_DIR}/services/portal_html.h")

message(STATUS "SmartCar: Packing web assets to firmware...")

# Initialize headers
file(WRITE "${PORTAL_HTML_H}"
    "#ifndef PORTAL_HTML_H\n"
    "#define PORTAL_HTML_H\n\n"
    "extern const char HTTP_OK_HEADER[];\n"
)
file(WRITE "${PORTAL_HTML_C}"
    "#include \"portal_html.h\"\n\n"
    "const char HTTP_OK_HEADER[] =\n"
    "    \"HTTP/1.1 200 OK\\r\\n\"\n"
    "    \"Content-Type: text/html; charset=utf-8\\r\\n\"\n"
    "    \"Connection: close\\r\\n\\r\\n\";\n\n"
)

function(embed_html_file HTML_FILE_NAME VAR_NAME)
    set(FILE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/services/html/${HTML_FILE_NAME}")
    if(EXISTS "${FILE_PATH}")
        file(READ "${FILE_PATH}" CONTENT)
        
        # Escape for C string literal
        string(REPLACE "\\" "\\\\" CONTENT "${CONTENT}")
        string(REPLACE "\"" "\\\"" CONTENT "${CONTENT}")
        string(REPLACE "\r" "" CONTENT "${CONTENT}")
        string(REPLACE "\n" "\\n\"\n\"" CONTENT "${CONTENT}")
        
        file(APPEND "${PORTAL_HTML_H}" "extern const char ${VAR_NAME}[];\n")
        file(APPEND "${PORTAL_HTML_C}" "const char ${VAR_NAME}[] =\n\"${CONTENT}\";\n\n")
    else()
        message(FATAL_ERROR "SmartCar: Web asset not found at ${FILE_PATH}")
    endif()
endfunction()

# Embed html pages
embed_html_file("portal_page.html" "s_html_page")
embed_html_file("portal_fail.html" "s_html_fail_result")
embed_html_file("portal_busy.html" "s_html_busy")
embed_html_file("portal_submitted.html" "s_html_submitted")
embed_html_file("portal_control.html" "s_html_control")

file(APPEND "${PORTAL_HTML_H}" "\n#endif // PORTAL_HTML_H\n")

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
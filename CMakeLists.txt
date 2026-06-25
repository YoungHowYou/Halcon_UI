# ============================================================
# CMakeLists.txt for HALCON extension package 'Halcon_UI'
# 跨平台 WebSocket/HTTP 后端服务器，用于 Browser/Electron 前端通信
#
# 依赖管理: vcpkg (manifest 模式)
#   - libjpeg-turbo: JPEG 图像编解码（跨平台）
# ============================================================

cmake_minimum_required(VERSION 3.20)

# ==================== 项目声明 ====================
project(Halcon_UI
    VERSION 1.0.0
    DESCRIPTION "Halcon 扩展包 —— 内置 HTTP/WebSocket 服务器，跨平台图像推流"
    LANGUAGES C CXX
)

set(ProjectName Halcon_UI)

# ==================== 平台检测 ====================
if(WIN32)
    set(HALCON_UI_PLATFORM "windows")
elseif(APPLE)
    set(HALCON_UI_PLATFORM "macos")
else()
    set(HALCON_UI_PLATFORM "linux")
endif()
message(STATUS "Halcon_UI 目标平台: ${HALCON_UI_PLATFORM}")

# ==================== 输出目录 ====================
# Windows: bin/（HALCON 从 bin/ 加载 DLL）
# Linux  : lib/x64-linux/（HALCON 从 lib/<arch>/ 加载 .so）
if(WIN32)
    set(HALCON_UI_OUT_DIR "${CMAKE_SOURCE_DIR}/bin")
else()
    set(HALCON_UI_OUT_DIR "${CMAKE_SOURCE_DIR}/lib/x64-linux")
endif()

# 统一输出到目标目录（不生成 Debug/Release 子目录）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${HALCON_UI_OUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${HALCON_UI_OUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${HALCON_UI_OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${HALCON_UI_OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${HALCON_UI_OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${HALCON_UI_OUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${HALCON_UI_OUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${HALCON_UI_OUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${HALCON_UI_OUT_DIR}")

# ==================== C++ 标准 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==================== Halcon SDK ====================
# 优先从 HALCONEXAMPLES 获取 cmake 模块
# 也兼容 shell 里写成字面量未展开的情况（"$HALCONROOT/..."）
file(TO_CMAKE_PATH "$ENV{HALCONEXAMPLES}" HALCONEXAMPLES)
if(NOT HALCONEXAMPLES OR HALCONEXAMPLES MATCHES "\\$" OR NOT EXISTS "${HALCONEXAMPLES}/cmake/UseHALCON.cmake")
    file(TO_CMAKE_PATH "$ENV{HALCONROOT}" _HALCONROOT_NORM)
    if(_HALCONROOT_NORM AND EXISTS "${_HALCONROOT_NORM}/examples/cmake/UseHALCON.cmake")
        set(HALCONEXAMPLES "${_HALCONROOT_NORM}/examples")
        message(STATUS "HALCONEXAMPLES 环境变量不可用，回退到 ${HALCONEXAMPLES}")
    else()
        message(FATAL_ERROR
            "找不到 HALCON cmake 模块。请设置 HALCONROOT 指向 Halcon 安装目录 "
            "(当前 HALCONROOT='$ENV{HALCONROOT}', HALCONEXAMPLES='$ENV{HALCONEXAMPLES}')。")
    endif()
endif()
list(APPEND CMAKE_MODULE_PATH "${HALCONEXAMPLES}/cmake")
include(UseHALCON)

# Halcon 头文件 & 库目录
set(HALCON_ROOT "$ENV{HALCONROOT}")
set(HALCON_INCLUDE_DIR "${HALCON_ROOT}/include/halconcpp")
if(WIN32)
    set(HALCON_LIB_DIR "${HALCON_ROOT}/lib/x64-win64")
else()
    set(HALCON_LIB_DIR "${HALCON_ROOT}/lib/x64-linux")
endif()

# ==================== vcpkg 三方库 ====================
# libjpeg-turbo: 跨平台 JPEG 编解码（替代 Windows GDI+ / Linux bundled libjpeg）
# 使用 TurboJPEG C API（turbojpeg.h）
find_package(libjpeg-turbo CONFIG REQUIRED)
message(STATUS "libjpeg-turbo 已找到: ${libjpeg-turbo_DIR}")

# vcpkg 推荐写法：自动选择动态库或静态库 target
set(HALCON_UI_JPEG_TARGET
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)

# ==================== 系统库 ====================
# 跨平台线程库
find_package(Threads REQUIRED)

# ==================== 定义 HALCON 扩展包 ====================
HALCON_AddExtensionPackage(${ProjectName}
    DEF_FILES
        def/Halcon_UI.def
    SOURCES
        src/websocket.cpp
        src/Halcon_UI.cpp
        src/Halcon_UI.c
    CHAPTERS
        userextensions
    CLASSES
        WebUI
    OPERATORS
)

# ==================== 头文件 ====================
file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})

# Windows: .def 文件标记为仅头文件（不参与编译）
if(WIN32)
    file(GLOB DEF_FILES "${CMAKE_CURRENT_SOURCE_DIR}/def/*.def")
    target_sources(${ProjectName} PRIVATE ${DEF_FILES})
    set_source_files_properties(${DEF_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

# ==================== 头文件搜索路径 ====================
target_include_directories(${ProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ==================== 链接库 ====================
if(WIN32)
    target_link_libraries(${ProjectName}
        ${HALCON_LIB_DIR}/halconcpp.lib
        ${HALCON_UI_JPEG_TARGET}
        ws2_32          # Winsock2
    )
else()
    # Linux: 查找 halconcpp 库
    find_library(HALCON_LIB halconcpp PATHS ${HALCON_LIB_DIR} NO_DEFAULT_PATH)
    if(NOT HALCON_LIB)
        find_library(HALCON_LIB halconcpp)
    endif()
    if(NOT HALCON_LIB)
        message(FATAL_ERROR "找不到 halconcpp 库。请正确设置 HALCONROOT。")
    endif()

    target_link_libraries(${ProjectName}
        ${HALCON_LIB}
        ${HALCON_UI_JPEG_TARGET}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    # 隐藏默认符号可见性（仅 EXPORTS_API 标记的导出，与 Windows .def 行为对齐）
    set_target_properties(${ProjectName} PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    # RPATH: 运行时从 .so 所在目录查找依赖库
    target_link_options(${ProjectName} PRIVATE
        "LINKER:-rpath,\$ORIGIN"
        "LINKER:--disable-new-dtags"
    )
endif()

# ==================== 编译选项 ====================
if(MSVC)
    # Windows MSVC 编译选项
    target_compile_options(${ProjectName} PRIVATE
        /W3             # 警告级别 3
        /utf-8          # UTF-8 源文件编码
    )
    target_compile_definitions(${ProjectName} PRIVATE
        _CRT_SECURE_NO_WARNINGS   # 抑制 fopen/strcpy 等 CRT 安全警告
    )
else()
    # Linux / GCC / Clang 编译选项
    target_compile_options(${ProjectName} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

# ==================== 构建信息摘要 ====================
message(STATUS "")
message(STATUS "======== Halcon_UI 构建配置摘要 ========")
message(STATUS "  平台:          ${HALCON_UI_PLATFORM}")
message(STATUS "  构建类型:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  输出目录:      ${HALCON_UI_OUT_DIR}")
message(STATUS "  Halcon 根目录: ${HALCON_ROOT}")
message(STATUS "  JPEG 库:       libjpeg-turbo (vcpkg)")
message(STATUS "==========================================")
message(STATUS "")

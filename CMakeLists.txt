# CMakeLists.txt for HALCON extension package 'Halcon_UI'
# WebSocket 后端服务器，用于 Browser/Electron 前端通信

# Option for using HALCON XL.
option(HALCON_XL "Use HALCON XL" OFF)
# Option for enabling .NET support. .NET is Windows-only, default OFF elsewhere.
if(WIN32)
  option(HALCON_DOTNET "Build .NET support" ON)
else()
  option(HALCON_DOTNET "Build .NET support" OFF)
endif()
# If the HALCON_DOTNET option is selected, check if this version of CMake
# has the required CSharp support. Note that the Visual Studio 2010 or later
# generator has to be used.
if(HALCON_DOTNET)
  cmake_minimum_required(VERSION 4.1.1)
  set(_CSHARP CSharp)
else()
  cmake_minimum_required(VERSION 3.20)
endif()
# When using CMake 3.9 or newer, do not let RPATH affect the install_name on
# macOS.
if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)
endif()
# If policy CMP0091 is available, set it to NEW to enable use of the
# CMAKE_MSVC_RUNTIME_LIBRARY variable.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
# Start the project here


project(Halcon_UI C CXX)
set(ProjectName Halcon_UI)
set(CMAKE_BUILD_TYPE Debug)
# 禁止生成 Debug/Release 子目录，直接输出到 bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")

# Append HALCON's cmake directory to the module path.
# 优先从 HALCONEXAMPLES 取，但要兼容 shell 里写成字面量未展开（"$HALCONROOT/..."）
# 的情况——这种情况退化到 HALCONROOT/examples 自己拼。
file(TO_CMAKE_PATH "$ENV{HALCONEXAMPLES}" HALCONEXAMPLES)
if(NOT HALCONEXAMPLES OR HALCONEXAMPLES MATCHES "\\$" OR NOT EXISTS "${HALCONEXAMPLES}/cmake/UseHALCON.cmake")
  file(TO_CMAKE_PATH "$ENV{HALCONROOT}" _HALCONROOT_NORM)
  if(_HALCONROOT_NORM AND EXISTS "${_HALCONROOT_NORM}/examples/cmake/UseHALCON.cmake")
    set(HALCONEXAMPLES "${_HALCONROOT_NORM}/examples")
    message(STATUS "HALCONEXAMPLES not usable from environment, falling back to ${HALCONEXAMPLES}")
  else()
    message(FATAL_ERROR
      "Cannot locate HALCON cmake modules. Set HALCONROOT to your HALCON install "
      "(currently HALCONROOT='$ENV{HALCONROOT}', HALCONEXAMPLES='$ENV{HALCONEXAMPLES}').")
  endif()
endif()
list(APPEND CMAKE_MODULE_PATH "${HALCONEXAMPLES}/cmake")
# Include common settings for HALCON projects
include(UseHALCON)

# 跨平台线程库
find_package(Threads REQUIRED)
##这以上都能不能动


HALCON_AddExtensionPackage(Halcon_UI
  DEF_FILES
    def/Halcon_UI.def
  SOURCES
    source/websocket.cpp
    source/Halcon_UI.cpp
    source/Halcon_UI.c

  CHAPTERS
    userextensions
  CLASSES
    WebUI
  OPERATORS

)

##三方库包含
set(HALCON_ROOT $ENV{HALCONROOT})   # 转成 CMake 变量，后面用着方便
# Halcon 头文件目录
set(HALCON_INCLUDE_DIR ${HALCON_ROOT}/include/halconcpp)
# Halcon 的 lib 目录（按平台区分）
if(WIN32)
    set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-win64)
else()
    set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-linux)
endif()

file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})

# Windows 上的 .def 文件不参与编译，只用于导出
if(WIN32)
    file(GLOB DEF_FILES "${CMAKE_CURRENT_SOURCE_DIR}/def/*.def")
    target_sources(${ProjectName} PRIVATE ${DEF_FILES})
    set_source_files_properties(${DEF_FILES} PROPERTIES
        HEADER_FILE_ONLY TRUE
    )
endif()


target_include_directories(${ProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        #往后增加3方库的头文件目录

)

if(WIN32)
    target_link_libraries(${ProjectName}
        ${HALCON_LIB_DIR}/halconcpp.lib
        gdiplus.lib
    )
else()
    # Linux: 找 halconcpp + libjpeg
    find_library(HALCON_LIB halconcpp PATHS ${HALCON_LIB_DIR} NO_DEFAULT_PATH)
    if(NOT HALCON_LIB)
        find_library(HALCON_LIB halconcpp)
    endif()
    if(NOT HALCON_LIB)
        message(FATAL_ERROR "Could not find halconcpp library. Set HALCONROOT correctly.")
    endif()

    find_package(JPEG REQUIRED)

    target_link_libraries(${ProjectName}
        ${HALCON_LIB}
        ${JPEG_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    target_include_directories(${ProjectName} PRIVATE ${JPEG_INCLUDE_DIR})

    # 隐藏默认符号可见性，仅 EXPORTS_API 标记的导出（与 Windows .def 行为对齐）
    set_target_properties(${ProjectName} PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    # 把扩展包的标准产物目录烧进 RPATH，运行时不依赖 LD_LIBRARY_PATH
    target_link_options(${ProjectName} PRIVATE
        "LINKER:-rpath,\$ORIGIN"
        "LINKER:--disable-new-dtags"
    )
endif()

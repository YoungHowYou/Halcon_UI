# Install script for directory: /home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/help" TYPE FILE FILES
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/help/operators_en_US.idx"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/help/operators_en_US.key"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/help/operators_en_US.num"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/help/operators_en_US.ref"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/help/operators_en_US.sta"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/doc/html/reference" TYPE FILE FILES
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/doc/html/reference/index.html"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/doc/html/reference/index_by_name.html"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/doc/html/reference/index_classes.html"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/doc/html/reference/HWebUI.html"
    "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/doc/html/reference/toc_userextensions.html"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux" TYPE SHARED_LIBRARY FILES "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/bin/libHalcon_UI.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UI.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UI.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UI.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux" TYPE SHARED_LIBRARY FILES "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/bin/libHalcon_UIc.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UIc.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UIc.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UIc.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/include" TYPE FILE FILES "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/HCHalcon_UI.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux" TYPE SHARED_LIBRARY FILES "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/bin/libHalcon_UIcpp.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UIcpp.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UIcpp.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-linux/libHalcon_UIcpp.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/include" TYPE FILE FILES "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/HCPPHalcon_UI.h")
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/home/njsc/MVTec/HALCON-24.11-Progress-Steady/Halcon_Extension/Halcon_UI/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")

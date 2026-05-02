# Install script for directory: D:/Desktop/Source/Halcon_Extension/Halcon_UI

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/Halcon_UI")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
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

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/help" TYPE FILE FILES
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/help/operators_en_US.idx"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/help/operators_en_US.key"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/help/operators_en_US.num"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/help/operators_en_US.ref"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/help/operators_en_US.sta"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/doc/html/reference" TYPE FILE FILES
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/doc/html/reference/index.html"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/doc/html/reference/index_by_name.html"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/doc/html/reference/index_classes.html"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/doc/html/reference/HWebUI.html"
    "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/doc/html/reference/toc_userextensions.html"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UI.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UI.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/MinSizeRel/Halcon_UI.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/RelWithDebInfo/Halcon_UI.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UI.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UI.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/MinSizeRel/Halcon_UI.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/RelWithDebInfo/Halcon_UI.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIc.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIc.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/MinSizeRel/Halcon_UIc.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/RelWithDebInfo/Halcon_UIc.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIc.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIc.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/MinSizeRel/Halcon_UIc.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/RelWithDebInfo/Halcon_UIc.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/include" TYPE FILE FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/HCHalcon_UI.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIcpp.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIcpp.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/MinSizeRel/Halcon_UIcpp.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/lib/x64-win64" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/RelWithDebInfo/Halcon_UIcpp.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIcpp.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/Halcon_UIcpp.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/MinSizeRel/Halcon_UIcpp.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/bin/x64-win64" TYPE SHARED_LIBRARY FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/bin/RelWithDebInfo/Halcon_UIcpp.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/Halcon_UI/include" TYPE FILE FILES "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/HCPPHalcon_UI.h")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/Desktop/Source/Halcon_Extension/Halcon_UI/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

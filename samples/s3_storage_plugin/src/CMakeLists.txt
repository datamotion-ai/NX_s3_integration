## Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/

cmake_minimum_required(VERSION 3.13)

set(SERVICE_NAME s3)
set(SERVICE_COMPONENTS s3)

project(s3_storage_plugin)

# Build shared libraries by default.
set(BUILD_SHARED_LIBS ON)

# Set the C++ standard to use to build this target.
set(CMAKE_CXX_STANDARD 20)

# Use the MSVC variable to determine if this is a Windows build.
set(WINDOWS_BUILD ${MSVC})


if(WINDOWS_BUILD)
    string(REPLACE ";" "/aws-cpp-sdk-all;" SYSTEM_MODULE_PATH "${CMAKE_SYSTEM_PREFIX_PATH}/aws-cpp-sdk-all")
    list(APPEND CMAKE_PREFIX_PATH ${SYSTEM_MODULE_PATH})
endif()

find_package(AWSSDK REQUIRED COMPONENTS ${SERVICE_COMPONENTS})
find_package(CURL REQUIRED)
if(NOT WIN32)
   find_package(ZLIB REQUIRED)
endif()

Message(STATUS "All linked libs: ${AWSSDK_LINK_LIBRARIES} ")
Message(STATUS "All platform libs: ${AWSSDK_PLATFORM_DEPS} ")
Message(STATUS "current cmake directory ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE source_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
add_library(s3_storage_plugin SHARED ${source_files} )

target_include_directories(s3_storage_plugin PRIVATE
    ../../../src #< SDK headers.
    ${AWSSDK_INCLUDE_DIRS}
    ../../../lib/jsoncpp/include
    E:/vcpkg-master/installed/x64-windows-static/include)
    
if(WIN32)
    set(API_IMPORT_MACRO "__declspec(dllimport)")
    set(API_EXPORT_MACRO "__declspec(dllexport)")
else()
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(API_IMPORT_MACRO "")
    set(API_EXPORT_MACRO "__attribute__((visibility(\"default\")))")
endif()

target_compile_definitions(s3_storage_plugin
    PRIVATE NX_PLUGIN_API=${API_EXPORT_MACRO}
    INTERFACE NX_PLUGIN_API=${API_IMPORT_MACRO}
)

set(BUILD_SHARED_LIBS ON CACHE STRING "Link to shared libraries by default.")

if(WINDOWS_BUILD)
     # Copy relevant AWS SDK for C++ libraries into the current binary directory for running and debugging.

     set(BIN_SUB_DIR "/debug") # If you are building from the command line, you may need to uncomment this 
                                    # and set the proper subdirectory to the executables' location.

     AWSSDK_CPY_DYN_LIBS(SERVICE_COMPONENTS "" ${CMAKE_CURRENT_BINARY_DIR}${BIN_SUB_DIR})
     string(APPEND CMAKE_CXX_FLAGS " /MP")
     target_compile_options(s3_storage_plugin BEFORE PRIVATE /Z7)
     target_compile_definitions(s3_storage_plugin PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
     set_target_properties(s3_storage_plugin PROPERTIES
    			   CXX_STANDARD_REQUIRED YES
    			   CXX_EXTENSIONS NO
    			   LINK_FLAGS "-Wl,-z,defs")
endif()

if(WINDOWS_BUILD)
    list(FIND AWSSDK_CLIENT_LIBS "curl" CONTAINS_CURL)
    if (CONTAINS_CURL EQUAL 0)
        set(CURL_INCLUDE "E:/vcpkg-master/packages/curl_x64-windows-static/include") # Update this with correct curl install location.
        set(CURL_LIB "E:/vcpkg-master/packages/curl_x64-windows-static/lib") # Update this with correct  curl install location.
    endif()
endif()

if(WINDOWS_BUILD)
    target_link_libraries(s3_storage_plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/Release/jsoncpp_static.lib
        ${AWSSDK_LINK_LIBRARIES}
        CURL::libcurl)
else()
    target_link_libraries(s3_storage_plugin PRIVATE 
   	${AWSSDK_LINK_LIBRARIES} 
   	${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/libjsoncpp.a)
endif()
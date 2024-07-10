## Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/

cmake_minimum_required(VERSION 3.13)

#set(AWSSDK_DIR, "/home/user/vca/NX_s3_integration/lib/aws/")
#set(SERVICE_NAME s3)
#set(SERVICE_COMPONENTS s3)

#
# Conan
#

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

#find_package(AWSSDK REQUIRED COMPONENTS ${SERVICE_COMPONENTS})
#find_package(CURL REQUIRED)
#if(NOT WIN32)
#   find_package(ZLIB REQUIRED)
#endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(ARMV8_LIB_ROOT "../../../lib/")

Message(STATUS "All linked libs: ${AWSSDK_LINK_LIBRARIES} ")
Message(STATUS "All platform libs: ${AWSSDK_PLATFORM_DEPS} ")
Message(STATUS "current cmake directory ${CMAKE_CURRENT_SOURCE_DIR}")

#file(GLOB_RECURSE source_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
FILE(GLOB TARGET_H "${CMAKE_SOURCE_DIR}/*.h")
FILE(GLOB TARGET_CPP "${CMAKE_SOURCE_DIR}/*.cpp")
FILE(GLOB TARGET_HPP "${CMAKE_SOURCE_DIR}/*.hpp")

add_library(s3_storage_plugin SHARED ${TARGET_H} ${TARGET_CPP} ${TARGET_HPP} )

target_include_directories(s3_storage_plugin PRIVATE
    ../../../src #< SDK headers.
    ${ARMV8_LIB_ROOT}aws/include
    ${ARMV8_LIB_ROOT}armv8-a/curl-7.80.0/pkg/include
    ${ARMV8_LIB_ROOT}armv8-a/openssl/include
    ${ARMV8_LIB_ROOT}armv8-a/zlib/include
    ${ARMV8_LIB_ROOT}/jsoncpp/include)
    
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

target_link_directories(
  s3_storage_plugin
  PRIVATE
  ${ARMV8_LIB_ROOT}armv8-a/curl-7.80.0/usr/lib
  ${ARMV8_LIB_ROOT}armv8-a/libidn2/usr/lib
  ${ARMV8_LIB_ROOT}armv8-a/libnghttp2/usr/lib
  ${ARMV8_LIB_ROOT}armv8-a/libssh2/usr/lib
  ${ARMV8_LIB_ROOT}armv8-a/libunistring/usr/lib
  ${ARMV8_LIB_ROOT}armv8-a/openssl/usr/lib
  ${ARMV8_LIB_ROOT}armv8-a/zlib/usr/lib
  ${ARMV8_LIB_ROOT}aws/lib
)

if(WINDOWS_BUILD)
    target_link_libraries(s3_storage_plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/Release/jsoncpp_static.lib
        ${AWSSDK_LINK_LIBRARIES}
        #${AWSSDK_PLATFORM_DEPS}
        CURL::libcurl)
else()
    target_link_libraries(s3_storage_plugin PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/${ARMV8_LIB_ROOT}jsoncpp/lib/libjsoncpp.a 
        crypto
  	curl
  	ssl
  	z
	dl
  	Threads::Threads
	aws-cpp-sdk-s3
	aws-cpp-sdk-core
	aws-crt-cpp
	aws-c-s3
	aws-c-auth
	aws-c-http
	aws-c-compression
	aws-c-io
	aws-c-cal
	aws-c-sdkutils
	aws-checksums
	aws-c-event-stream
	aws-c-mqtt
	aws-c-common
	s2n)
endif()

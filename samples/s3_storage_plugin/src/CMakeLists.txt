## Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/

cmake_minimum_required(VERSION 3.13)

set(SERVICE_NAME s3)
set(SERVICE_COMPONENTS s3)

# Keep deps/CRT consistent with the installed AWS SDK (x64-windows-static => /MT).
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

# Prefer vcpkg static AWS SDK over any other AWSSDK on the machine (e.g. Anaconda).
set(AWSSDK_ROOT_DIR "C:/develop/vcpkg/installed/x64-windows-static" CACHE PATH "" FORCE)
list(PREPEND CMAKE_PREFIX_PATH
    "C:/develop/vcpkg/installed/x64-windows-static"
    "C:/develop/vcpkg/downloads/curl-mt-install")

project(s3_storage_plugin)

# Plugin is still a shared library; its third-party deps are linked statically (/MT).
set(BUILD_SHARED_LIBS ON)

set(CMAKE_CXX_STANDARD 17)

set(WINDOWS_BUILD ${MSVC})

find_package(AWSSDK REQUIRED COMPONENTS ${SERVICE_COMPONENTS})
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

set(JSONCPP_INCLUDE_DIR "C:/develop/vcpkg/buildtrees/jsoncpp/src/1.9.6-29ceffc35f.clean/include")
set(JSONCPP_LIBRARY "C:/develop/vcpkg/buildtrees/jsoncpp/x64-windows-static-rel/lib/jsoncpp.lib")

message(STATUS "All linked libs: ${AWSSDK_LINK_LIBRARIES} ")
message(STATUS "All platform libs: ${AWSSDK_PLATFORM_DEPS} ")
message(STATUS "current cmake directory ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE source_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
add_library(s3_storage_plugin SHARED ${source_files})

target_include_directories(s3_storage_plugin PRIVATE
    ../../../src #< SDK headers.
    ${AWSSDK_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS})

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

if(WINDOWS_BUILD)
    string(APPEND CMAKE_CXX_FLAGS " /MP")
    target_compile_options(s3_storage_plugin BEFORE PRIVATE /Z7 /MT$<$<CONFIG:Debug>:d>)
    target_compile_definitions(s3_storage_plugin PRIVATE
        _CRT_SECURE_NO_WARNINGS
        CURL_STATICLIB)
    # AWS SDK was built without USE_IMPORT_EXPORT (static libs).
    target_compile_definitions(s3_storage_plugin PRIVATE
        AWS_SDK_USE_IMPORT_EXPORT=0)
else()
    set_target_properties(s3_storage_plugin PROPERTIES
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        LINK_FLAGS "-Wl,-z,defs")
endif()

if(WINDOWS_BUILD)
    target_link_libraries(s3_storage_plugin PRIVATE
        ${JSONCPP_LIBRARY}
        ${AWSSDK_LINK_LIBRARIES}
        CURL::libcurl
        ZLIB::ZLIB
        ws2_32
        crypt32
        wldap32
        normaliz
        bcrypt
        winhttp
        wininet
        userenv
        version
        ncrypt
        secur32)
else()
    target_link_libraries(s3_storage_plugin PRIVATE
        ${AWSSDK_LINK_LIBRARIES}
        ${JSONCPP_LIBRARY}
        CURL::libcurl
        ZLIB::ZLIB)
endif()

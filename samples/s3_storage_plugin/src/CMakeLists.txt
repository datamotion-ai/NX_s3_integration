## Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/

cmake_minimum_required(VERSION 3.3.2)
project(s3_storage_plugin)

find_package(AWSSDK CONFIG COMPONENTS s3 REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

Message(STATUS "All linked libs: ${AWSSDK_LINK_LIBRARIES}")

file(GLOB_RECURSE source_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
add_library(s3_storage_plugin SHARED ${source_files} )

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

target_include_directories(s3_storage_plugin PRIVATE
    ../../../src #< SDK headers.
    ${AWSSDK_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS} #< curl headers.
)

if(WIN32)
    # Use all CPU cores by MSVC.
    string(APPEND CMAKE_CXX_FLAGS " /MP")
    # Do not create separate .pdb files for object files (workaround for mspdbsrv.exe bug, needed
    # for using "/MP" flag).
    target_compile_options(s3_storage_plugin BEFORE PRIVATE /Z7)
    target_compile_definitions(s3_storage_plugin PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(s3_storage_plugin PRIVATE ws2_32)
endif()

set_target_properties(s3_storage_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_link_libraries(s3_storage_plugin PRIVATE ${AWSSDK_LINK_LIBRARIES} /home/ubuntu/jsoncpp/build/lib/libjsoncpp.a)
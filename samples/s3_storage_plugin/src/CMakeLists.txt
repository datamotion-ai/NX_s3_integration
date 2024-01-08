## Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/

cmake_minimum_required(VERSION 3.3.2)
project(s3_storage_plugin)

find_package(AWSSDK CONFIG COMPONENTS s3 REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(CURL REQUIRED)

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
    /usr/include/jsoncpp #< Json headers.
    ${AWSSDK_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS} #< curl headers.
)

target_compile_definitions(s3_storage_plugin PRIVATE
    # Needed for ftplib.
    -DNOSSL
    -DNOLFS
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

target_link_libraries(s3_storage_plugin PRIVATE ${AWSSDK_LIBRARIES} jsoncpp ${CURL_LIBRARIES})

# # Install the library and headers
# install(TARGETS s3_storage_plugin
#     EXPORT s3_storage_pluginTargets
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib
#     RUNTIME DESTINATION bin
#     INCLUDES DESTINATION include
# )

# install(DIRECTORY ../../../src/
#     DESTINATION include
#     FILES_MATCHING PATTERN "*.h"
# )

# # Export target information to the package
# export(EXPORT s3_storage_pluginTargets
#     FILE ${CMAKE_CURRENT_BINARY_DIR}/s3_storage_pluginTargets.cmake
#     NAMESPACE s3_storage_plugin::
# )

# # Install exported target information
# install(EXPORT s3_storage_pluginTargets
#     FILE s3_storage_pluginTargets.cmake
#     NAMESPACE s3_storage_plugin::
#     DESTINATION lib/cmake
# )

# # Create a release package (tarball)
# set(CPACK_GENERATOR "TGZ")
# set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
# set(CPACK_PACKAGE_VERSION "1.0.0")  # Set your actual version
# set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
# include(CPack)


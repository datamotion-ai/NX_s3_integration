cmake_minimum_required(VERSION 3.12)

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

project(Storage_SDK_License_Config)

set(CMAKE_CXX_STANDARD 20)

set(WINDOWS_BUILD ${MSVC})

set(JSONCPP_INCLUDE_DIR "C:/develop/vcpkg/buildtrees/jsoncpp/src/1.9.6-29ceffc35f.clean/include")
set(JSONCPP_LIBRARY "C:/develop/vcpkg/buildtrees/jsoncpp/x64-windows-static-rel/lib/jsoncpp.lib")

add_executable(Storage_SDK_License_Config "Storage_SDK_License_Config.cpp")

target_include_directories(Storage_SDK_License_Config PRIVATE
    ${JSONCPP_INCLUDE_DIR})

if(WINDOWS_BUILD)
    target_compile_options(Storage_SDK_License_Config PRIVATE /MT$<$<CONFIG:Debug>:d>)
    target_link_libraries(Storage_SDK_License_Config PRIVATE
        ${JSONCPP_LIBRARY})
else()
    target_link_libraries(Storage_SDK_License_Config PRIVATE
        ${JSONCPP_LIBRARY})
endif()

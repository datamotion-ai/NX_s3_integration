cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)

# Use the MSVC variable to determine if this is a Windows build.
set(WINDOWS_BUILD ${MSVC})

project(Storage_SDK_License_Config)

# Add source to this project's executable.
add_executable(Storage_SDK_License_Config "Storage_SDK_License_Config.cpp")

SET(CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed")

target_include_directories(Storage_SDK_License_Config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/include)

if(WINDOWS_BUILD)
    target_link_libraries(Storage_SDK_License_Config PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/Release/jsoncpp_static.lib
        ssl 
        crypto
        z)
else()
    target_link_libraries(Storage_SDK_License_Config PRIVATE  
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/libjsoncpp.a
        ssl 
        crypto
        z)
endif()
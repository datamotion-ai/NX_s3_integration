cmake_minimum_required(VERSION 3.12)

project(Hex_Key_Generator)

set(CMAKE_CXX_STANDARD 20)

# Use the MSVC variable to determine if this is a Windows build.
set(WINDOWS_BUILD ${MSVC})

find_package(OpenSSL REQUIRED)

# Add source to this project's executable.
add_executable(Hex_Key_Generator "Hex_Key_Generator.cpp")

SET(CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed")

target_include_directories(Hex_Key_Generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/include
    E:/vcpkg-master/installed/x64-windows-static/include)

if(WINDOWS_BUILD)
    target_link_libraries(Hex_Key_Generator PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/Release/jsoncpp_static.lib
        OpenSSL::SSL
        OpenSSL::Crypto)
else()
    target_link_libraries(Hex_Key_Generator PRIVATE  
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/jsoncpp/build/lib/libjsoncpp.a
        ssl 
        crypto
        z)
endif()
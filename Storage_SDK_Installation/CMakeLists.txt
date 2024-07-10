# CMakeList.txt : CMake project for Storage_SDK_Installation, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)

# Use the MSVC variable to determine if this is a Windows build.
set(WINDOWS_BUILD ${MSVC})

set(ARMV8_LIB_ROOT "../lib/")

Message(STATUS "All linked libs: ${OpenSSL_LINK_LIBRARIES} ")

project ("Storage_SDK_Installation")

# Add source to this project's executable.
add_executable (Storage_SDK_Installation "Storage_SDK_Installation.cpp")

SET ( CMAKE_EXE_LINKER_FLAGS "-Wl,--as-needed" )

target_include_directories(Storage_SDK_Installation PRIVATE
    ${ARMV8_LIB_ROOT}armv8-a/openssl/include
    ${ARMV8_LIB_ROOT}armv8-a/zlib/include
    ${ARMV8_LIB_ROOT}jsoncpp/include)

target_link_directories(
  Storage_SDK_Installation
  PRIVATE
  ${ARMV8_LIB_ROOT}armv8-a/openssl/usr/lib
  ${ARMV8_LIB_ROOT}/armv8-a/zlib/usr/lib
)

target_link_libraries(Storage_SDK_Installation PRIVATE 
    ssl 
    crypto
    z
    ${CMAKE_CURRENT_SOURCE_DIR}/${ARMV8_LIB_ROOT}jsoncpp/lib/libjsoncpp.a)
# TODO: Add tests and install targets if needed.

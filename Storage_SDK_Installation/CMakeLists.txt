# CMakeList.txt : CMake project for Storage_SDK_Installation, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)

# Use the MSVC variable to determine if this is a Windows build.
set(WINDOWS_BUILD ${MSVC})

find_package(OpenSSL REQUIRED)

Message(STATUS "All linked libs: ${OpenSSL_LINK_LIBRARIES} ")

project ("Storage_SDK_Installation")

# Add source to this project's executable.
add_executable (Storage_SDK_Installation "Storage_SDK_Installation.cpp")

target_include_directories(Storage_SDK_Installation PRIVATE
    ${OpenSSL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/jsoncpp/include)

target_link_libraries(Storage_SDK_Installation PRIVATE 
    OpenSSL::SSL 
    OpenSSL::Crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/jsoncpp/build/lib/libjsoncpp.a)

# TODO: Add tests and install targets if needed.

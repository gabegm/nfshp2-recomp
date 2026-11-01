cmake_minimum_required(VERSION 3.18)
project(PlayGsSmoke LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH "${PLAY_SOURCE}/deps/Dependencies/cmake-modules")
include(Header)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This isolated backend test currently supports Linux only")
endif()
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)
add_subdirectory("${PLAY_SOURCE}/deps/Framework/build_cmake/Framework" Framework)
add_subdirectory("${PLAY_SOURCE}/Source/gs/GSH_Vulkan" gsh)
# Pinned zstd gzip wrapper needs STDC with current zlib headers.
target_compile_definitions(libzstd_zlibwrapper_static PRIVATE STDC=1)
add_library(gs_frontend STATIC
  ${PLAY_SOURCE}/Source/gs/GSHandler.cpp
  ${PLAY_SOURCE}/Source/gs/GsPixelFormats.cpp
  ${PLAY_SOURCE}/Source/gs/GsCachedArea.cpp
  ${PLAY_SOURCE}/Source/FrameDump.cpp
  ${PLAY_SOURCE}/Source/MailBox.cpp
  ${PLAY_SOURCE}/Source/ee/INTC.cpp
  ${PLAY_SOURCE}/Source/states/MemoryStateFile.cpp
  ${PLAY_SOURCE}/Source/states/RegisterState.cpp
  ${PLAY_SOURCE}/Source/states/RegisterStateFile.cpp)
target_include_directories(gs_frontend PUBLIC ${PLAY_SOURCE}/Source ${PLAY_SOURCE}/deps/CodeGen/include)
target_link_libraries(gs_frontend PUBLIC app_shared Threads::Threads)
add_executable(play-gs-smoke main.cpp)
target_include_directories(play-gs-smoke PRIVATE ${PLAY_SOURCE}/Source/gs/GSH_Vulkan)
target_link_libraries(play-gs-smoke PRIVATE gsh_vulkan gs_frontend Vulkan::Vulkan)

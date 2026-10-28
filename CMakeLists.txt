cmake_minimum_required(VERSION 3.24)
project(ZeClinic CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

# ZECLINIC_VENDOR_SDL=ON downloads SDL3 and links it statically, producing a
# self-contained executable (used for the Linux/Windows release builds).
option(ZECLINIC_VENDOR_SDL "Build SDL3 from source and link it statically" OFF)

if(ZECLINIC_VENDOR_SDL)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
else()
  find_package(SDL3 REQUIRED CONFIG)
endif()

add_executable(zeclinic WIN32
  src/main.cpp
  src/Game.cpp
  src/GameCombat.cpp
  src/GameFx.cpp
  src/GameNet.cpp
  src/GameRender.cpp
  src/Menu.cpp
  src/Autotest.cpp
  src/Art.cpp
  src/Audio.cpp
  src/Gfx.cpp
  src/Net.cpp
)
target_link_libraries(zeclinic PRIVATE SDL3::SDL3)
# Steam is loaded at runtime (SDL_LoadObject), so no Steamworks SDK is needed to build.
if(WIN32)
  target_link_libraries(zeclinic PRIVATE ws2_32)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(zeclinic PRIVATE -Wall -Wextra)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(zeclinic PRIVATE -s)
  endif()
  if(ZECLINIC_VENDOR_SDL)
    target_link_options(zeclinic PRIVATE -static-libstdc++ -static-libgcc)
  endif()
elseif(MSVC)
  target_compile_options(zeclinic PRIVATE /W3 /utf-8)
endif()

cmake_minimum_required(VERSION 3.21)
project(GraceValley VERSION 0.1.0 LANGUAGES CXX)

option(GRACE_BUILD_GAME "Build the SDL3 graphical game" ON)
option(GRACE_BUILD_TESTS "Build simulation tests" ON)
option(GRACE_FETCH_SDL "Download SDL3 if it is not installed" ON)

add_library(grace_core src/core/world.cpp)
target_include_directories(grace_core PUBLIC include)
target_compile_features(grace_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(grace_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(grace_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GRACE_BUILD_GAME)
  find_package(SDL3 3.4 QUIET CONFIG)
  if(NOT SDL3_FOUND AND GRACE_FETCH_SDL)
    include(FetchContent)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
      URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
  endif()
  if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3 3.4+ is required. Install SDL3 or enable GRACE_FETCH_SDL.")
  endif()

  add_executable(grace-valley src/game/main.cpp)
  target_link_libraries(grace-valley PRIVATE grace_core SDL3::SDL3)
  target_compile_features(grace-valley PRIVATE cxx_std_20)
  add_custom_command(TARGET grace-valley POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:grace-valley>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/assets/Grace Valley Sunrise Church Emblem.png"
      $<TARGET_FILE_DIR:grace-valley>/assets/
    VERBATIM)
  if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET grace-valley POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:grace-valley>
      VERBATIM)
  endif()
endif()

if(GRACE_BUILD_TESTS)
  enable_testing()
  add_executable(grace_core_tests tests/world_tests.cpp)
  target_link_libraries(grace_core_tests PRIVATE grace_core)
  add_test(NAME grace_core_tests COMMAND grace_core_tests)
  add_executable(grace_play_scenarios tests/play_scenarios.cpp)
  target_link_libraries(grace_play_scenarios PRIVATE grace_core)
  add_test(NAME grace_play_scenarios COMMAND grace_play_scenarios)
  if(GRACE_BUILD_GAME)
    add_test(NAME grace_setup_smoke COMMAND grace-valley --smoke)
    set_tests_properties(grace_setup_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_annual_ui_smoke COMMAND grace-valley --annual-smoke)
    set_tests_properties(grace_annual_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_drag_smoke COMMAND grace-valley --drag-smoke)
    set_tests_properties(grace_drag_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_bridge_smoke COMMAND grace-valley --bridge-smoke)
    set_tests_properties(grace_bridge_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_bankruptcy_ui_smoke COMMAND grace-valley --bankrupt-smoke)
    set_tests_properties(grace_bankruptcy_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_zoning_ui_smoke COMMAND grace-valley --zone-smoke)
    set_tests_properties(grace_zoning_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_toolbar_ui_smoke COMMAND grace-valley --toolbar-smoke)
    set_tests_properties(grace_toolbar_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_power_ui_smoke COMMAND grace-valley --power-smoke)
    set_tests_properties(grace_power_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_traffic_ui_smoke COMMAND grace-valley --traffic-smoke)
    set_tests_properties(grace_traffic_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_test(NAME grace_save_ui_smoke
      COMMAND grace-valley --save-smoke "${CMAKE_CURRENT_BINARY_DIR}/save-smoke.sav")
    set_tests_properties(grace_save_ui_smoke PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  endif()
endif()

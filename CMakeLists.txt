cmake_minimum_required(VERSION 3.24)

project(WornEnchantmentPBR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_SKIP_INSTALL_RULES ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.3
  GIT_SHALLOW    TRUE
  OVERRIDE_FIND_PACKAGE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(rapidcsv
  GIT_REPOSITORY https://github.com/d99kris/rapidcsv.git
  GIT_TAG        v8.99
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  does-not-exist)

FetchContent_Declare(CommonLibSSE
  GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
  GIT_TAG        b93280e832f263dbef44e44cbe2936622a02f91a
  SOURCE_SUBDIR  does-not-exist)

FetchContent_MakeAvailable(spdlog rapidcsv CommonLibSSE)

set(RAPIDCSV_INCLUDE_DIRS "${rapidcsv_SOURCE_DIR}/src" CACHE PATH "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
add_subdirectory("${commonlibsse_SOURCE_DIR}" "${commonlibsse_BINARY_DIR}" EXCLUDE_FROM_ALL)
include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

get_target_property(_clib_links CommonLibSSE INTERFACE_LINK_LIBRARIES)
set(_clib_links_fixed)
foreach(_lib IN LISTS _clib_links)
  if(_lib MATCHES "\\.lib$")
    string(TOLOWER "${_lib}" _lib)
  endif()
  list(APPEND _clib_links_fixed "${_lib}")
endforeach()
set_target_properties(CommonLibSSE PROPERTIES INTERFACE_LINK_LIBRARIES "${_clib_links_fixed}")

set(PLUGIN_SOURCES
  src/main.cpp
  src/Events.cpp
  src/Hooks.cpp
  src/Menu.cpp
  src/MenuState.cpp
  src/History.cpp
  src/EditCheck.cpp
  src/Paint.cpp
  src/Region.cpp
  src/MenuWidgets.cpp
  src/ComposePage.cpp
  src/RuntimeTextures.cpp
  src/Settings.cpp
  src/SettingsCore.cpp
  src/Timing.cpp
  src/Recipe.cpp
  src/RecipeJson.cpp
  src/Expression.cpp
  src/Signals.cpp
  src/Importer.cpp
  src/RecipeStore.cpp
  src/EngineForms.cpp
  src/Environment.cpp
  src/Compositor.cpp
  src/Mesh.cpp
  src/Analysis.cpp
  src/MeshReader.cpp
  src/Binding.cpp
  src/Manager.cpp
  src/Studio.cpp
  src/Edits.cpp
)

add_commonlibsse_plugin(${PROJECT_NAME}
  NAME    ${PROJECT_NAME}
  AUTHOR  "collin"
  VERSION ${PROJECT_VERSION}
  USE_ADDRESS_LIBRARY
  SOURCES ${PLUGIN_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src src/extern)
target_compile_definitions(${PROJECT_NAME} PRIVATE WEPBR_PLUGIN_NAME="${PROJECT_NAME}")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

set(CLANG_CL_FLAGS
  /permissive- /EHsc /W4 /utf-8
  -fdelayed-template-parsing -Wno-delayed-template-parsing-in-cxx20
  -Wno-overloaded-virtual
  -Wno-delete-non-abstract-non-virtual-dtor
  -Wno-inconsistent-missing-override
  -Wno-reinterpret-base-class)
target_compile_options(${PROJECT_NAME} PRIVATE ${CLANG_CL_FLAGS})
target_compile_options(CommonLibSSE PRIVATE ${CLANG_CL_FLAGS} -Wno-invalid-offsetof -Wno-nontrivial-memcall)

target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /PDBALTPATH:%_PDB%)

set(HOST_OBJECT_SOURCES
  src/Timing.cpp
  src/SettingsCore.cpp
  src/Expression.cpp
  src/Recipe.cpp
  src/RecipeJson.cpp
  src/Signals.cpp
  src/Importer.cpp
  src/Mesh.cpp
  src/Analysis.cpp
  src/Region.cpp
  src/Studio.cpp
  src/MenuState.cpp
  src/History.cpp
  src/Edits.cpp
  src/EditCheck.cpp
  src/Paint.cpp)
add_library(${PROJECT_NAME}HostObjects OBJECT ${HOST_OBJECT_SOURCES})
set_target_properties(${PROJECT_NAME}HostObjects PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_include_directories(${PROJECT_NAME}HostObjects PRIVATE src src/extern)
target_compile_options(${PROJECT_NAME}HostObjects PRIVATE /EHsc /W4 /utf-8)
target_compile_definitions(${PROJECT_NAME}HostObjects PRIVATE WEPBR_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

function(add_host_test NAME)
  add_executable(${NAME} ${ARGN})
  set_target_properties(${NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  target_include_directories(${NAME} PRIVATE src src/extern)
  target_compile_options(${NAME} PRIVATE /EHsc /W4 /utf-8)
  target_compile_definitions(${NAME} PRIVATE WEPBR_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
  target_link_libraries(${NAME} PRIVATE ${PROJECT_NAME}HostObjects)
endfunction()
add_host_test(${PROJECT_NAME}Tests tests/timing_tests.cpp)
add_host_test(${PROJECT_NAME}SettingsTests tests/settings_tests.cpp)
add_host_test(${PROJECT_NAME}ExpressionTests tests/expression_tests.cpp)
add_host_test(${PROJECT_NAME}RecipeTests tests/recipe_tests.cpp)
add_host_test(${PROJECT_NAME}SignalTests tests/signal_tests.cpp)
add_host_test(${PROJECT_NAME}ImporterTests tests/importer_tests.cpp)
add_host_test(${PROJECT_NAME}BakeTests tests/bake_tests.cpp)
add_host_test(${PROJECT_NAME}AnalysisTests tests/analysis_tests.cpp)
add_host_test(${PROJECT_NAME}RegionTests tests/region_tests.cpp)
add_host_test(${PROJECT_NAME}StudioTests tests/studio_tests.cpp)
add_host_test(${PROJECT_NAME}EditsTests tests/edits_tests.cpp)

add_custom_target(host-tests DEPENDS
  ${PROJECT_NAME}Tests
  ${PROJECT_NAME}SettingsTests
  ${PROJECT_NAME}ExpressionTests
  ${PROJECT_NAME}RecipeTests
  ${PROJECT_NAME}SignalTests
  ${PROJECT_NAME}ImporterTests
  ${PROJECT_NAME}BakeTests
  ${PROJECT_NAME}AnalysisTests
  ${PROJECT_NAME}RegionTests
  ${PROJECT_NAME}StudioTests
  ${PROJECT_NAME}EditsTests)

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${PROJECT_NAME}")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SKSE/Plugins"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:${PROJECT_NAME}>"
          "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
          "${DIST_DIR}/SKSE/Plugins/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.ini"
          "${DIST_DIR}/SKSE/Plugins/"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/presets/regions.json"
          "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/"
  VERBATIM)

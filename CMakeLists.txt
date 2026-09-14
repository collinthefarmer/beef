cmake_minimum_required(VERSION 3.24)

project(BetterEnchantmentEffects VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_SKIP_INSTALL_RULES ON)

# Cache third-party object files across clean builds and branch switches.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

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

set(CLANG_CL_FLAGS
  /permissive- /EHsc /W4 /utf-8
  -fdelayed-template-parsing -Wno-delayed-template-parsing-in-cxx20)
set(COMMONLIBSSE_SUPPRESSIONS
  -Wno-overloaded-virtual
  -Wno-delete-non-abstract-non-virtual-dtor
  -Wno-inconsistent-missing-override
  -Wno-reinterpret-base-class
  -Wno-invalid-offsetof
  -Wno-nontrivial-memcall)

# The engine-free host surface. Later waves drop their pure modules into
# src/recipe, src/mesh and src/studio; the globs pick them up with no edit here.
file(GLOB_RECURSE NATIVE_MODULE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/recipe/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/mesh/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/studio/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/planners/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/diagnostics/*.cpp")
set(NATIVE_SOURCES
  src/Settings.cpp
  ${NATIVE_MODULE_SOURCES})

add_library(${PROJECT_NAME}Native OBJECT ${NATIVE_SOURCES})
target_include_directories(${PROJECT_NAME}Native PRIVATE src)
target_include_directories(${PROJECT_NAME}Native SYSTEM PRIVATE src/extern)
target_compile_options(${PROJECT_NAME}Native PRIVATE ${CLANG_CL_FLAGS} -Werror=switch)

# The plugin DLL. Engine-facing sources: the wave-3 engine/, render/, and menu/
# adapters plus the entrypoint and settings I/O.
# The engine-free modules (recipe/mesh/studio/planners) and Settings.cpp come
# from the Native object library, linked below, so each is compiled once and
# shared with the native test host.
file(GLOB_RECURSE ENGINE_RENDER_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/engine/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/render/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/menu/*.cpp")
set(PLUGIN_SOURCES
  src/main.cpp
  src/SettingsFile.cpp
  ${ENGINE_RENDER_SOURCES})

add_commonlibsse_plugin(${PROJECT_NAME}
  NAME    ${PROJECT_NAME}
  AUTHOR  "collin"
  VERSION ${PROJECT_VERSION}
  USE_ADDRESS_LIBRARY
  SOURCES ${PLUGIN_SOURCES})

find_program(BEEF_PYTHON python3 REQUIRED)
add_custom_target(BuildIdentity
  COMMAND "${BEEF_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/build-identity.py"
          --root "${CMAKE_SOURCE_DIR}" --output "${CMAKE_BINARY_DIR}/generated"
          --config "${CMAKE_BUILD_TYPE}"
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/BuildIdentity.h"
             "${CMAKE_BINARY_DIR}/generated/build-identity.json"
  VERBATIM)
add_dependencies(${PROJECT_NAME} BuildIdentity)

target_include_directories(${PROJECT_NAME} PRIVATE src "${CMAKE_BINARY_DIR}/generated")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE src/extern)
target_compile_definitions(${PROJECT_NAME} PRIVATE BEEF_PLUGIN_NAME="${PROJECT_NAME}")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_sources(${PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:${PROJECT_NAME}Native>)

set_target_properties(${PROJECT_NAME} ${PROJECT_NAME}Native PROPERTIES
  CXX_COMPILER_LAUNCHER "")

target_compile_options(${PROJECT_NAME} PRIVATE ${CLANG_CL_FLAGS} -Werror=switch)
target_compile_options(CommonLibSSE PRIVATE ${CLANG_CL_FLAGS} ${COMMONLIBSSE_SUPPRESSIONS})

target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /PDBALTPATH:%_PDB%)

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${PROJECT_NAME}")
add_custom_target(PresenterTextures ALL
  COMMAND "${BEEF_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/presenter-textures.py"
          --output "${DIST_DIR}/textures/${PROJECT_NAME}/slots"
  VERBATIM)
add_dependencies(${PROJECT_NAME} PresenterTextures)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SKSE/Plugins"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:${PROJECT_NAME}>"
          "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
          "${DIST_DIR}/SKSE/Plugins/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/generated/build-identity.json"
          "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}-build.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.ini"
          "${DIST_DIR}/SKSE/Plugins/"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/presets/presets.json"
          "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/"
  VERBATIM)

cmake_minimum_required(VERSION 3.24)

project(WornEnchantmentPBR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Nothing is installed; this also keeps CommonLibSSE-NG from exporting a
# target set that would need spdlog to be exported too.
set(CMAKE_SKIP_INSTALL_RULES ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# CommonLibSSE-NG calls find_package(spdlog CONFIG REQUIRED); OVERRIDE_FIND_PACKAGE
# redirects that lookup to this FetchContent build.
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

# Pinned to the last commit on main as of 2026-09-03; the library has no
# release tag covering Skyrim 1.6.1170.
FetchContent_Declare(CommonLibSSE
  GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
  GIT_TAG        b93280e832f263dbef44e44cbe2936622a02f91a
  SOURCE_SUBDIR  does-not-exist)

FetchContent_MakeAvailable(spdlog rapidcsv CommonLibSSE)

# CommonLibSSE-NG uses find_path for rapidcsv; seeding the cache variable
# short-circuits the search.
set(RAPIDCSV_INCLUDE_DIRS "${rapidcsv_SOURCE_DIR}/src" CACHE PATH "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
add_subdirectory("${commonlibsse_SOURCE_DIR}" "${commonlibsse_BINARY_DIR}" EXCLUDE_FROM_ALL)
include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

# The xwin SDK layout is case-sensitive on Linux and ships lowercase import
# libraries; CommonLibSSE-NG links mixed-case names (Advapi32.lib, Dbghelp.lib).
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
  src/Regions.cpp
  src/MaskStack.cpp
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
# The only place the plugin name is spelled in the sources is src/Identity.h,
# which reads it from here.
target_compile_definitions(${PROJECT_NAME} PRIVATE WEPBR_PLUGIN_NAME="${PROJECT_NAME}")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# CommonLibSSE-NG relies on MSVC's late template parsing (dependent-base
# members used without this->), so clang has to defer parsing the same way.
set(CLANG_CL_FLAGS
  /permissive- /EHsc /W4 /utf-8
  -fdelayed-template-parsing -Wno-delayed-template-parsing-in-cxx20
  -Wno-overloaded-virtual
  -Wno-delete-non-abstract-non-virtual-dtor
  -Wno-inconsistent-missing-override
  -Wno-reinterpret-base-class)
target_compile_options(${PROJECT_NAME} PRIVATE ${CLANG_CL_FLAGS})
target_compile_options(CommonLibSSE PRIVATE ${CLANG_CL_FLAGS} -Wno-invalid-offsetof -Wno-nontrivial-memcall)

# SKSE needs a PDB next to the DLL for crash logs; emit one in every config.
target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /PDBALTPATH:%_PDB%)

# Host-independent checks; each produces a Windows console exe. The same
# sources compile natively through tests/run-native.sh.
set(RECIPE_MODEL_SOURCES src/Recipe.cpp src/RecipeJson.cpp src/Expression.cpp src/Signals.cpp src/Importer.cpp src/Timing.cpp)
function(add_host_test NAME)
  add_executable(${NAME} ${ARGN})
  target_include_directories(${NAME} PRIVATE src src/extern)
  target_compile_options(${NAME} PRIVATE /EHsc /W4 /utf-8)
  target_compile_definitions(${NAME} PRIVATE WEPBR_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
endfunction()
add_host_test(${PROJECT_NAME}Tests tests/timing_tests.cpp src/Timing.cpp)
add_host_test(${PROJECT_NAME}SettingsTests tests/settings_tests.cpp src/SettingsCore.cpp src/Timing.cpp)
add_host_test(${PROJECT_NAME}ExpressionTests tests/expression_tests.cpp src/Expression.cpp)
add_host_test(${PROJECT_NAME}RecipeTests tests/recipe_tests.cpp ${RECIPE_MODEL_SOURCES})
add_host_test(${PROJECT_NAME}SignalTests tests/signal_tests.cpp ${RECIPE_MODEL_SOURCES})
add_host_test(${PROJECT_NAME}ImporterTests tests/importer_tests.cpp ${RECIPE_MODEL_SOURCES})
add_host_test(${PROJECT_NAME}BakeTests tests/bake_tests.cpp src/Mesh.cpp ${RECIPE_MODEL_SOURCES})
add_host_test(${PROJECT_NAME}AnalysisTests tests/analysis_tests.cpp src/Analysis.cpp src/Mesh.cpp ${RECIPE_MODEL_SOURCES})
add_host_test(${PROJECT_NAME}MaskStackTests tests/maskstack_tests.cpp src/MaskStack.cpp src/Expression.cpp)
add_host_test(${PROJECT_NAME}StudioTests tests/studio_tests.cpp src/Studio.cpp src/MenuState.cpp src/History.cpp src/Edits.cpp src/EditCheck.cpp src/Regions.cpp src/MaskStack.cpp src/Analysis.cpp src/Mesh.cpp ${RECIPE_MODEL_SOURCES})
add_host_test(${PROJECT_NAME}EditsTests tests/edits_tests.cpp src/Edits.cpp ${RECIPE_MODEL_SOURCES})

# Stage a ready-to-install mod folder: dist/<mod>/SKSE/Plugins/{dll,pdb,ini}.
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
# Recipes are not staged: recipes/ holds examples the tests read, and a
# recipe the user wants in the game is written straight into the MO2 mod
# folder (Data/<mod>/<folder>/), never through the installer.

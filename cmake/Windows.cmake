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
  BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/spdlog-build"
  SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/spdlog-subbuild"
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.3
  GIT_SHALLOW    TRUE
  OVERRIDE_FIND_PACKAGE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(rapidcsv
  BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/rapidcsv-build"
  SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/rapidcsv-subbuild"
  GIT_REPOSITORY https://github.com/d99kris/rapidcsv.git
  GIT_TAG        v8.99
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  does-not-exist)

FetchContent_Declare(CommonLibSSE
  BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/commonlibsse-build"
  SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/commonlibsse-subbuild"
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

file(GLOB_RECURSE ENGINE_RENDER_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/engine/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/render/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/menu/*.cpp")
set(PLUGIN_SOURCES
  src/main.cpp
  src/SettingsFile.cpp
  ${ENGINE_RENDER_SOURCES})

configure_file("${CMAKE_CURRENT_LIST_DIR}/Plugin.cpp.in"
  "${CMAKE_BINARY_DIR}/generated/Plugin.cpp" @ONLY)
add_library(${PROJECT_NAME} SHARED ${PLUGIN_SOURCES}
  "${CMAKE_BINARY_DIR}/generated/Plugin.cpp")
target_compile_definitions(${PROJECT_NAME} PRIVATE __CMAKE_COMMONLIBSSE_PLUGIN=1)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

include("${CMAKE_CURRENT_LIST_DIR}/Generated.cmake")

target_include_directories(${PROJECT_NAME} PRIVATE src "${CMAKE_BINARY_DIR}/generated")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE src/extern)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  BEEF_PLUGIN_NAME="${PROJECT_NAME}" BEEF_PRESENTER_COUNT=${BEEF_PRESENTER_COUNT})
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}Native)
set_target_properties(${PROJECT_NAME} ${PROJECT_NAME}Native BeefValidate PROPERTIES
  CXX_COMPILER_LAUNCHER "")
target_compile_options(${PROJECT_NAME} PRIVATE ${CLANG_CL_FLAGS} -Werror=switch /Zi)
target_compile_options(${PROJECT_NAME}Native PRIVATE ${CLANG_CL_FLAGS} -Werror=switch)
target_compile_options(BeefValidate PRIVATE ${CLANG_CL_FLAGS} -Werror=switch)
target_compile_options(CommonLibSSE PRIVATE ${CLANG_CL_FLAGS} ${COMMONLIBSSE_SUPPRESSIONS})
target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /PDBALTPATH:%_PDB%)

include("${CMAKE_CURRENT_LIST_DIR}/Stage.cmake")

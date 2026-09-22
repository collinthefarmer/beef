find_package(Threads REQUIRED)
add_library(BeefNativeOptions INTERFACE)
target_compile_options(BeefNativeOptions INTERFACE -O1 -Wall -Wextra -Werror=switch)
target_link_libraries(BeefNativeOptions INTERFACE Threads::Threads)
if(BEEF_SANITIZE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The sanitizer configuration requires clang++")
  endif()
  target_compile_options(BeefNativeOptions INTERFACE
    -fsanitize=address,undefined -fno-sanitize=vptr
    -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
  target_link_options(BeefNativeOptions INTERFACE -fsanitize=address,undefined)
endif()
target_link_libraries(${PROJECT_NAME}Native PUBLIC BeefNativeOptions)
include(CTest)
if(NOT BUILD_TESTING)
  return()
endif()

add_library(BeefEngineServices STATIC
  src/engine/SessionQueue.cpp src/engine/ApplicationService.cpp
  src/engine/TextFile.cpp src/engine/PluginEvents.cpp)
target_link_libraries(BeefEngineServices PUBLIC ${PROJECT_NAME}Native)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS tests/*_tests.cpp)
foreach(source IN LISTS TEST_SOURCES)
  file(RELATIVE_PATH suite "${CMAKE_SOURCE_DIR}/tests" "${source}")
  string(REPLACE "/" "_" suite "${suite}")
  string(REGEX REPLACE "_tests\\.cpp$" "" suite "${suite}")
  add_executable(${suite} "${source}")
  target_link_libraries(${suite} PRIVATE BeefEngineServices)
  target_include_directories(${suite} PRIVATE tests)
  target_compile_definitions(${suite} PRIVATE
    BEEF_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    BEEF_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/templates"
    BEEF_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/${suite}-data")
  add_test(NAME ${suite} COMMAND ${suite})
  set_tests_properties(${suite} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
file(GLOB TOOL_TESTS CONFIGURE_DEPENDS tests/tools/*_tests.py)
foreach(source IN LISTS TOOL_TESTS)
  get_filename_component(suite "${source}" NAME_WE)
  add_test(NAME tools_${suite} COMMAND "${Python3_EXECUTABLE}" "${source}")
endforeach()
find_program(CHECK_JSONSCHEMA check-jsonschema REQUIRED)
add_test(NAME schema COMMAND "${CHECK_JSONSCHEMA}"
  --schemafile "${CMAKE_SOURCE_DIR}/schema/recipe.schema.json"
  "${CMAKE_SOURCE_DIR}/schema/example-magicka.json"
  "${CMAKE_SOURCE_DIR}/templates/fill.json"
  "${CMAKE_SOURCE_DIR}/templates/bare.json")

find_package(Threads REQUIRED)
add_library(BeefNativeOptions INTERFACE)
target_compile_options(BeefNativeOptions INTERFACE -O1 -Wall -Wextra -Wno-missing-field-initializers -Werror=switch)
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
  src/engine/TextFile.cpp src/engine/PluginEvents.cpp src/engine/RecipeFiles.cpp
  src/engine/InstanceTime.cpp src/engine/MenuDependency.cpp)
target_link_libraries(BeefEngineServices PUBLIC ${PROJECT_NAME}Native)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS tests/*_tests.cpp)
foreach(source IN LISTS TEST_SOURCES)
  file(RELATIVE_PATH suite "${CMAKE_SOURCE_DIR}/tests" "${source}")
  string(REPLACE "/" "_" suite "${suite}")
  string(REGEX REPLACE "_tests\\.cpp$" "" suite "${suite}")
  add_executable(${suite} "${source}")
  target_link_libraries(${suite} PRIVATE BeefEngineServices)
  if(suite STREQUAL "engine_wornkeys")
    target_sources(${suite} PRIVATE src/engine/WornKeys.cpp src/engine/EnchantmentEffects.cpp)
    target_include_directories(${suite} BEFORE PRIVATE tests/engine/wornkeys)
  endif()
  if(suite STREQUAL "engine_animationsubscriptions")
    target_sources(${suite} PRIVATE src/engine/AnimationSubscriptions.cpp src/engine/ManagerAnimation.cpp)
    target_include_directories(${suite} BEFORE PRIVATE tests/engine/animation)
    set(animation_suite ${suite})
  endif()
  if(suite STREQUAL "engine_editorintegration")
    target_sources(${suite} PRIVATE src/engine/RecipeEditor.cpp src/engine/RecipeStore.cpp)
    target_include_directories(${suite} BEFORE PRIVATE tests/engine/platform)
  endif()
  if(suite STREQUAL "engine_hooks")
    target_sources(${suite} PRIVATE src/engine/Hooks.cpp)
    target_include_directories(${suite} BEFORE PRIVATE tests/engine/hooks)
    add_test(NAME engine_hooks_success COMMAND ${suite} success)
  endif()
  if(suite STREQUAL "engine_liveretirement")
    target_sources(${suite} PRIVATE src/engine/LiveActor.cpp)
    target_include_directories(${suite} BEFORE PRIVATE tests/engine/lifetime)
  endif()
  target_include_directories(${suite} PRIVATE tests)
  target_compile_definitions(${suite} PRIVATE
    BEEF_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    BEEF_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/templates"
    BEEF_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/${suite}-data")
  add_test(NAME ${suite} COMMAND ${suite})
  set_tests_properties(${suite} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
set_tests_properties(${animation_suite} PROPERTIES TIMEOUT 30)
file(GLOB TOOL_TESTS CONFIGURE_DEPENDS tests/tools/*_tests.py)
foreach(source IN LISTS TOOL_TESTS)
  get_filename_component(suite "${source}" NAME_WE)
  if(suite STREQUAL "recipe_contract_tests")
    add_test(NAME tools_${suite} COMMAND "${Python3_EXECUTABLE}" "${source}"
      --validator "$<TARGET_FILE:BeefValidate>")
  else()
    add_test(NAME tools_${suite} COMMAND "${Python3_EXECUTABLE}" "${source}")
  endif()
endforeach()
find_program(CHECK_JSONSCHEMA check-jsonschema REQUIRED)
add_test(NAME example_arcane_circuit COMMAND "$<TARGET_FILE:BeefValidate>"
  "${CMAKE_SOURCE_DIR}/recipes/examples/arcane-circuit.json")
add_test(NAME example_resonant_ward COMMAND "$<TARGET_FILE:BeefValidate>"
  "${CMAKE_SOURCE_DIR}/recipes/examples/resonant-ward.json")
add_test(NAME example_winterglass COMMAND "$<TARGET_FILE:BeefValidate>"
  "${CMAKE_SOURCE_DIR}/recipes/examples/winterglass.json")
add_test(NAME schema COMMAND "${CHECK_JSONSCHEMA}"
  --schemafile "${CMAKE_SOURCE_DIR}/schema/recipe.schema.json"
  "${CMAKE_SOURCE_DIR}/schema/example-magicka.json"
  "${CMAKE_SOURCE_DIR}/recipes/examples/winterglass.json"
  "${CMAKE_SOURCE_DIR}/recipes/examples/resonant-ward.json"
  "${CMAKE_SOURCE_DIR}/recipes/examples/arcane-circuit.json"
  "${CMAKE_SOURCE_DIR}/templates/fill.json"
  "${CMAKE_SOURCE_DIR}/templates/bare.json")

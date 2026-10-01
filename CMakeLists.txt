cmake_minimum_required(VERSION 3.24)
project(BetterEnchantmentEffects VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(BEEF_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB_RECURSE NATIVE_MODULE_SOURCES CONFIGURE_DEPENDS
  src/recipe/*.cpp src/mesh/*.cpp src/studio/*.cpp
  src/planners/*.cpp src/diagnostics/*.cpp src/regression/*.cpp)
add_library(${PROJECT_NAME}Native STATIC src/Settings.cpp ${NATIVE_MODULE_SOURCES})
target_include_directories(${PROJECT_NAME}Native PUBLIC src)
target_include_directories(${PROJECT_NAME}Native SYSTEM PUBLIC src/extern)
set_target_properties(${PROJECT_NAME}Native PROPERTIES CXX_COMPILER_LAUNCHER "")

add_executable(BeefValidate src/validator/Main.cpp)
set_target_properties(BeefValidate PROPERTIES OUTPUT_NAME beef-validate)
target_link_libraries(BeefValidate PRIVATE ${PROJECT_NAME}Native)

if(WIN32)
  include(cmake/Windows.cmake)
else()
  include(cmake/Native.cmake)
endif()

set(BEEF_PRESENTER_COUNT 1024)
file(GLOB_RECURSE IDENTITY_INPUTS CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*" "${CMAKE_SOURCE_DIR}/cmake/*"
  "${CMAKE_SOURCE_DIR}/shaders/*")
list(FILTER IDENTITY_INPUTS EXCLUDE REGEX "/_old/")
list(APPEND IDENTITY_INPUTS
  "${CMAKE_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_SOURCE_DIR}/CMakePresets.json"
  "${CMAKE_SOURCE_DIR}/flake.nix"
  "${CMAKE_SOURCE_DIR}/flake.lock" "${CMAKE_SOURCE_DIR}/tools/build-identity.py"
  "${CMAKE_SOURCE_DIR}/tools/presenter-textures.py")
find_package(Git REQUIRED)
foreach(ref HEAD packed-refs)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${ref}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE ref_path
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  cmake_path(ABSOLUTE_PATH ref_path BASE_DIRECTORY "${CMAKE_SOURCE_DIR}")
  if(EXISTS "${ref_path}")
    list(APPEND IDENTITY_INPUTS "${ref_path}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ref_path}")
  endif()
endforeach()
execute_process(COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE branch
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(branch)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${branch}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE branch_path
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  cmake_path(ABSOLUTE_PATH branch_path BASE_DIRECTORY "${CMAKE_SOURCE_DIR}")
  if(EXISTS "${branch_path}")
    list(APPEND IDENTITY_INPUTS "${branch_path}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${branch_path}")
  endif()
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/identity-inputs.txt"
  CONTENT "${IDENTITY_INPUTS}\n")
list(APPEND IDENTITY_INPUTS "${CMAKE_BINARY_DIR}/generated/identity-inputs.txt")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/identity.stamp"
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/BuildIdentity.h"
    "${CMAKE_BINARY_DIR}/generated/build-identity.json"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build-identity.py"
    --root "${CMAKE_SOURCE_DIR}" --output "${CMAKE_BINARY_DIR}/generated"
    --config "${CMAKE_BUILD_TYPE}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/generated/identity.stamp"
  DEPENDS ${IDENTITY_INPUTS} VERBATIM)
add_custom_target(BuildIdentity DEPENDS "${CMAKE_BINARY_DIR}/generated/identity.stamp")
add_dependencies(${PROJECT_NAME} BuildIdentity)

set(PRESENTER_DIR "${CMAKE_BINARY_DIR}/presenters")
math(EXPR last_presenter "${BEEF_PRESENTER_COUNT} - 1")
set(PRESENTER_FILES)
foreach(index RANGE 0 ${last_presenter})
  if(index LESS 10)
    set(index "0${index}")
  endif()
  list(APPEND PRESENTER_FILES "${PRESENTER_DIR}/slot_${index}.dds")
endforeach()
add_custom_command(OUTPUT "${PRESENTER_DIR}/presenters.stamp"
  BYPRODUCTS ${PRESENTER_FILES}
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/presenter-textures.py"
    --output "${PRESENTER_DIR}" --count "${BEEF_PRESENTER_COUNT}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${PRESENTER_DIR}/presenters.stamp"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/presenter-textures.py"
  VERBATIM)
add_custom_target(PresenterTextures DEPENDS "${PRESENTER_DIR}/presenters.stamp")

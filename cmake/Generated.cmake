set(BEEF_PRESENTER_COUNT 1024)
add_custom_target(BuildIdentity
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DOUTPUT_DIR=${CMAKE_BINARY_DIR}/generated" "-DCONFIG=${CMAKE_BUILD_TYPE}"
    "-DPROFILE=${BEEF_COMPATIBILITY_PROFILE}" -P "${CMAKE_CURRENT_LIST_DIR}/BuildIdentity.cmake"
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/BuildIdentity.h"
    "${CMAKE_BINARY_DIR}/generated/build-identity.json"
  VERBATIM)
add_dependencies(${PROJECT_NAME} BuildIdentity)

set(PRESENTER_DIR "${CMAKE_BINARY_DIR}/presenters")
file(MAKE_DIRECTORY "${PRESENTER_DIR}")
set(PRESENTER_FILES)
math(EXPR last_presenter "${BEEF_PRESENTER_COUNT} - 1")
foreach(index RANGE ${last_presenter})
  if(index LESS 10)
    set(index "0${index}")
  endif()
  file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/presenter-slot.dds" "${PRESENTER_DIR}/slot_${index}.dds"
    ONLY_IF_DIFFERENT)
  list(APPEND PRESENTER_FILES "${PRESENTER_DIR}/slot_${index}.dds")
endforeach()

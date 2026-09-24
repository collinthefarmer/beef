set(BEEF_COMPATIBILITY_PROFILE "steam-1.6.1170" CACHE STRING "Reviewed target profile name")
if(NOT BEEF_COMPATIBILITY_PROFILE MATCHES "^[a-z0-9][a-z0-9.-]*$")
  message(FATAL_ERROR "Invalid compatibility profile name")
endif()
set(BEEF_PROFILE_SOURCE "${CMAKE_SOURCE_DIR}/cmake/compatibility/${BEEF_COMPATIBILITY_PROFILE}.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${BEEF_PROFILE_SOURCE}" "${CMAKE_SOURCE_DIR}/tools/compatibility.py"
  "${CMAKE_SOURCE_DIR}/src/cs/BSLightingShaderMaterialPBR.h"
  "${CMAKE_SOURCE_DIR}/src/extern/SKSEMenuFramework.h")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/compatibility.py"
  --profile "${BEEF_PROFILE_SOURCE}" --root "${CMAKE_SOURCE_DIR}"
  --output "${CMAKE_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_BINARY_DIR}/generated/compatibility.cmake")
set(BEEF_EFFECTIVE_PROFILE "${CMAKE_BINARY_DIR}/generated/compatibility.json")

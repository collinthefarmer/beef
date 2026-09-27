set(BEEF_COMPATIBILITY_PROFILE "steam-1.6.1170" CACHE STRING "Reviewed target profile name")
if(NOT BEEF_COMPATIBILITY_PROFILE MATCHES "^[a-z0-9][a-z0-9.-]*$")
  message(FATAL_ERROR "Invalid compatibility profile name")
endif()
set(BEEF_EFFECTIVE_PROFILE "${CMAKE_SOURCE_DIR}/cmake/compatibility/${BEEF_COMPATIBILITY_PROFILE}.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BEEF_EFFECTIVE_PROFILE}")
file(READ "${BEEF_EFFECTIVE_PROFILE}" profile_text)

function(profile_field variable)
  string(JSON value ERROR_VARIABLE error GET "${profile_text}" ${ARGN})
  if(error)
    message(FATAL_ERROR "${BEEF_EFFECTIVE_PROFILE}: ${error}")
  endif()
  set(${variable} "${value}" PARENT_SCOPE)
endfunction()

function(profile_version cpp packed)
  foreach(index 0 1 2 3)
    profile_field(part_${index} ${ARGN} ${index})
  endforeach()
  set(${cpp} "REL::Version{ ${part_0}, ${part_1}, ${part_2}, ${part_3} }" PARENT_SCOPE)
  math(EXPR value "(${part_0} << 24) | (${part_1} << 16) | (${part_2} << 4) | ${part_3}")
  set(${packed} "${value}u" PARENT_SCOPE)
endfunction()

profile_field(BEEF_COMMONLIB_REVISION dependencies commonlib)
profile_field(BEEF_SPDLOG_REVISION dependencies spdlog)
profile_field(BEEF_RAPIDCSV_REVISION dependencies rapidcsv)
foreach(family se ae vr)
  string(TOUPPER "${family}" upper)
  profile_field(BEEF_COMPILE_${upper} compiled ${family})
endforeach()
profile_field(BEEF_STRUCT_COMPATIBILITY struct_compatibility)
profile_version(BEEF_MINIMUM_SKSE BEEF_MINIMUM_SKSE_PACKED minimum_skse)

string(JSON runtime_count ERROR_VARIABLE error LENGTH "${profile_text}" runtimes)
if(error OR runtime_count EQUAL 0)
  message(FATAL_ERROR "${BEEF_EFFECTIVE_PROFILE}: no runtimes ${error}")
endif()
set(runtime_versions)
set(runtime_checks)
math(EXPR last_runtime "${runtime_count} - 1")
foreach(index RANGE ${last_runtime})
  profile_version(version packed runtimes ${index} version)
  list(APPEND runtime_versions "${version}")
  list(APPEND runtime_checks "a_runtime == ${packed}")
endforeach()
list(JOIN runtime_versions ", " runtime_versions)
set(BEEF_RUNTIME_DECLARATION "{ ${runtime_versions} }")
list(JOIN runtime_checks " || " BEEF_RUNTIME_CHECKS)

foreach(peer community_shaders menu_framework)
  profile_field(header peers ${peer} header)
  profile_field(expected peers ${peer} sha256)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${header}")
  file(SHA256 "${CMAKE_SOURCE_DIR}/${header}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${header} differs from the ${peer} baseline in ${BEEF_EFFECTIVE_PROFILE}")
  endif()
endforeach()

file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/BuildCompatibility.h" CONTENT
"#pragma once
#include <cstdint>
namespace BetterEnchantmentEffects::BuildCompatibility {
inline constexpr const char *profile = \"@BEEF_COMPATIBILITY_PROFILE@\";
inline constexpr std::uint32_t minimumSKSE = @BEEF_MINIMUM_SKSE_PACKED@;
inline constexpr bool SupportsRuntime(std::uint32_t a_runtime) noexcept {
  return @BEEF_RUNTIME_CHECKS@;
}
}
" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/Plugin.cpp.in" "${CMAKE_BINARY_DIR}/generated/Plugin.cpp" @ONLY)

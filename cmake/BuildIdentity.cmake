function(git output)
  execute_process(COMMAND git ${ARGN} WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE text OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  set(${output} "${text}" PARENT_SCOPE)
endfunction()

set(build_inputs src cmake CMakeLists.txt CMakePresets.json COPYING.md flake.nix flake.lock)

if(EXISTS "${SOURCE_DIR}/.git")
  git(revision rev-parse HEAD)
  git(changes diff HEAD --binary -- ${build_inputs})
  git(untracked ls-files --others --exclude-standard -- ${build_inputs})
  if(NOT changes STREQUAL "" OR NOT untracked STREQUAL "")
    string(SHA256 digest "${changes}\n${untracked}")
    string(SUBSTRING "${digest}" 0 12 digest)
    set(source "dirty-${digest}")
  else()
    set(source "clean")
  endif()
else()
  file(STRINGS "${SOURCE_DIR}/cmake/source-revision.txt" revision LIMIT_COUNT 1)
  if(NOT revision MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "No .git and cmake/source-revision.txt is not stamped by git archive")
  endif()
  set(source "archive")
endif()
string(SUBSTRING "${revision}" 0 12 short)
if(source STREQUAL "clean")
  set(build "${short}-${CONFIG}")
else()
  set(build "${short}-${source}-${CONFIG}")
endif()

file(CONFIGURE OUTPUT "${OUTPUT_DIR}/BuildIdentity.h" CONTENT
"#pragma once
namespace BetterEnchantmentEffects::BuildIdentity {
inline constexpr const char *build = \"@build@\";
inline constexpr const char *revision = \"@revision@\";
inline constexpr const char *source = \"@source@\";
}
" @ONLY)
file(CONFIGURE OUTPUT "${OUTPUT_DIR}/build-identity.json" CONTENT
"{
  \"build\": \"@build@\",
  \"revision\": \"@revision@\",
  \"source\": \"@source@\",
  \"configuration\": \"@CONFIG@\",
  \"compatibility_profile\": \"@PROFILE@\"
}
" @ONLY)

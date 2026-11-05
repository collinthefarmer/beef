set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${PROJECT_NAME}" CACHE PATH "Staged mod directory")
file(GLOB license_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/licenses/*")

set(notice_prefix "${PROJECT_NAME}-")
set(notice_dir "${CMAKE_BINARY_DIR}/generated/notices")
set(shipped_license "${notice_prefix}LICENSE")
set(shipped_copying "${notice_prefix}COPYING.md")
set(shipped_notices "${notice_prefix}THIRD_PARTY_NOTICES.md")
set(shipped_licenses "${notice_prefix}licenses")
function(write_shipped_notice source destination)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(READ "${source}" text)
  string(REPLACE "[LICENSE](LICENSE)" "[${shipped_license}](${shipped_license})" text "${text}")
  string(REPLACE "COPYING.md" "${shipped_copying}" text "${text}")
  string(REPLACE "THIRD_PARTY_NOTICES.md" "${shipped_notices}" text "${text}")
  string(REPLACE "licenses/" "${shipped_licenses}/" text "${text}")
  set(existing "")
  if(EXISTS "${notice_dir}/${destination}")
    file(READ "${notice_dir}/${destination}" existing)
  endif()
  if(NOT existing STREQUAL text)
    file(WRITE "${notice_dir}/${destination}" "${text}")
  endif()
endfunction()
write_shipped_notice("${CMAKE_SOURCE_DIR}/COPYING.md" "${shipped_copying}")
write_shipped_notice("${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${shipped_notices}")
add_custom_target(stage
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/templates"
    "${DIST_DIR}/textures/${PROJECT_NAME}/slots"
    "${DIST_DIR}/${shipped_licenses}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_DIR}/LICENSE" "${DIST_DIR}/COPYING.md"
    "${DIST_DIR}/THIRD_PARTY_NOTICES.md" "${DIST_DIR}/licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${BEEF_EFFECTIVE_PROFILE}" "${DIST_DIR}/COMPATIBILITY.json"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/LICENSE" "${DIST_DIR}/${shipped_license}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${notice_dir}/${shipped_copying}" "${notice_dir}/${shipped_notices}" "${DIST_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${license_files} "${DIST_DIR}/${shipped_licenses}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/docs/bug-report.md" "${DIST_DIR}/BUG_REPORT.md"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:${PROJECT_NAME}>" "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
    "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.ini" "${DIST_DIR}/SKSE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_BINARY_DIR}/generated/build-identity.json"
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}-build.json"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/presets/presets.json" "$<TARGET_FILE:BeefValidate>"
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/templates/fill.json" "${CMAKE_SOURCE_DIR}/templates/bare.json"
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/templates/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${PRESENTER_FILES} "${DIST_DIR}/textures/${PROJECT_NAME}/slots/"
  DEPENDS ${PROJECT_NAME} BeefValidate VERBATIM)

set(package_files)
macro(package_file source destination)
  list(APPEND package_files "{\"source\":\"${source}\",\"destination\":\"${destination}\"}")
endmacro()
package_file("$<TARGET_FILE:${PROJECT_NAME}>" "SKSE/Plugins/${PROJECT_NAME}.dll")
package_file("${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.ini" "SKSE/Plugins/${PROJECT_NAME}.ini")
package_file("${CMAKE_BINARY_DIR}/generated/build-identity.json" "SKSE/Plugins/${PROJECT_NAME}-build.json")
package_file("$<TARGET_FILE:BeefValidate>" "SKSE/Plugins/${PROJECT_NAME}/beef-validate.exe")
package_file("${CMAKE_SOURCE_DIR}/presets/presets.json" "SKSE/Plugins/${PROJECT_NAME}/presets.json")
foreach(template fill bare)
  package_file("${CMAKE_SOURCE_DIR}/templates/${template}.json" "SKSE/Plugins/${PROJECT_NAME}/templates/${template}.json")
endforeach()
foreach(presenter IN LISTS PRESENTER_FILES)
  get_filename_component(filename "${presenter}" NAME)
  package_file("${presenter}" "textures/${PROJECT_NAME}/slots/${filename}")
endforeach()
package_file("${BEEF_EFFECTIVE_PROFILE}" "COMPATIBILITY.json")
package_file("${CMAKE_SOURCE_DIR}/docs/alpha-package.md" "README.md")
package_file("${CMAKE_SOURCE_DIR}/docs/bug-report.md" "BUG_REPORT.md")
list(JOIN package_files ",\n" package_entries)
set(package_files)
package_file("${CMAKE_SOURCE_DIR}/LICENSE" "${shipped_license}")
package_file("${notice_dir}/${shipped_copying}" "${shipped_copying}")
package_file("${notice_dir}/${shipped_notices}" "${shipped_notices}")
foreach(license IN LISTS license_files)
  get_filename_component(filename "${license}" NAME)
  package_file("${license}" "${shipped_licenses}/${filename}")
endforeach()
list(JOIN package_files ",\n" notice_entries)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/package.json" CONTENT
"{
  \"name\": \"${PROJECT_NAME}-${PROJECT_VERSION}-${BEEF_COMPATIBILITY_PROFILE}\",
  \"identity\": \"${CMAKE_BINARY_DIR}/generated/build-identity.json\",
  \"files\": [${package_entries}],
  \"notices\": [${notice_entries}],
  \"symbols\": [{\"source\":\"$<TARGET_PDB_FILE:${PROJECT_NAME}>\",\"destination\":\"${PROJECT_NAME}.pdb\"}]
}")
add_custom_target(package-candidate
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/package.py"
    --spec "${CMAKE_BINARY_DIR}/generated/package.json"
    --work "${CMAKE_BINARY_DIR}/package" --output "${CMAKE_SOURCE_DIR}/dist/archives"
  DEPENDS ${PROJECT_NAME} BeefValidate VERBATIM)

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${PROJECT_NAME}" CACHE PATH "Staged mod directory")
file(GLOB license_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/licenses/*")
add_custom_target(stage
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/templates"
    "${DIST_DIR}/textures/${PROJECT_NAME}/slots"
    "${DIST_DIR}/licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${BEEF_EFFECTIVE_PROFILE}" "${DIST_DIR}/COMPATIBILITY.json"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/COPYING.md"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${DIST_DIR}/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${license_files} "${DIST_DIR}/licenses/"
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
package_file("${CMAKE_SOURCE_DIR}/LICENSE" "LICENSE")
package_file("${CMAKE_SOURCE_DIR}/COPYING.md" "COPYING.md")
package_file("${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "THIRD_PARTY_NOTICES.md")
foreach(license IN LISTS license_files)
  get_filename_component(filename "${license}" NAME)
  package_file("${license}" "licenses/${filename}")
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
  COMMAND "${CMAKE_COMMAND}" "-DSPEC=${CMAKE_BINARY_DIR}/generated/package.json"
    "-DWORK=${CMAKE_BINARY_DIR}/package" "-DOUTPUT=${CMAKE_SOURCE_DIR}/dist/archives"
    -P "${CMAKE_CURRENT_LIST_DIR}/Package.cmake"
  DEPENDS ${PROJECT_NAME} BeefValidate VERBATIM)

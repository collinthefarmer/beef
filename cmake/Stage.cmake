set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${PROJECT_NAME}" CACHE PATH "Staged mod directory")
set(license_inventory "${CMAKE_SOURCE_DIR}/licenses/inventory.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${license_inventory}")
file(READ "${license_inventory}" license_json)
string(JSON license_count LENGTH "${license_json}" files)
math(EXPR license_last "${license_count} - 1")
set(license_files "${license_inventory}")
foreach(index RANGE ${license_last})
  string(JSON filename GET "${license_json}" files ${index} path)
  list(APPEND license_files "${CMAKE_SOURCE_DIR}/licenses/${filename}")
endforeach()
add_custom_target(stage
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/templates"
    "${DIST_DIR}/textures/${PROJECT_NAME}/slots"
    "${DIST_DIR}/licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${BEEF_EFFECTIVE_PROFILE}" "${DIST_DIR}/COMPATIBILITY.json"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${DIST_DIR}/"
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
  DEPENDS ${PROJECT_NAME} BeefValidate PresenterTextures VERBATIM)

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
package_file("${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "THIRD_PARTY_NOTICES.md")
foreach(license IN LISTS license_files)
  get_filename_component(filename "${license}" NAME)
  package_file("${license}" "licenses/${filename}")
endforeach()
list(JOIN package_files ",\n" notice_entries)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/package.json" CONTENT
"{
  \"name\": \"${PROJECT_NAME}\",
  \"version\": \"${PROJECT_VERSION}\",
  \"identity\": \"${CMAKE_BINARY_DIR}/generated/build-identity.json\",
  \"declaration\": \"${CMAKE_BINARY_DIR}/generated/Plugin.cpp\",
  \"compatibility\": \"${BEEF_EFFECTIVE_PROFILE}\",
  \"files\": [${package_entries}],
  \"notices\": [${notice_entries}],
  \"symbols\": [{\"source\":\"$<TARGET_PDB_FILE:${PROJECT_NAME}>\",\"destination\":\"${PROJECT_NAME}.pdb\"}]
}")
add_custom_target(package-candidate
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/package.py" create
    "${CMAKE_BINARY_DIR}/generated/package.json" --output "${CMAKE_SOURCE_DIR}/dist/archives"
  DEPENDS ${PROJECT_NAME} BeefValidate PresenterTextures VERBATIM)

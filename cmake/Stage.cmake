set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${PROJECT_NAME}" CACHE PATH "Staged mod directory")
add_custom_target(stage
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${DIST_DIR}/SKSE/Plugins/${PROJECT_NAME}/templates"
    "${DIST_DIR}/textures/${PROJECT_NAME}/slots"
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

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(DEFINED ENV{XWIN_DIR})
  set(XWIN_DIR "$ENV{XWIN_DIR}")
endif()
if(NOT EXISTS "${XWIN_DIR}/crt/include" OR NOT EXISTS "${XWIN_DIR}/sdk/include")
  message(FATAL_ERROR "XWIN_DIR='${XWIN_DIR}' does not hold the Windows CRT and SDK. Inside nix develop, "
    "run NIXPKGS_ALLOW_UNFREE=1 nix build --impure .#windows-sdk -o build/windows-sdk; "
    "it downloads Microsoft's CRT and SDK and accepts Microsoft's license for them.")
endif()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(_xwin_target "--target=x86_64-pc-windows-msvc")
set(_xwin_cflags "${_xwin_target} /vctoolsdir ${XWIN_DIR}/crt /winsdkdir ${XWIN_DIR}/sdk")
set(CMAKE_C_FLAGS_INIT "${_xwin_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_cflags}")
set(CMAKE_RC_FLAGS_INIT "-I${XWIN_DIR}/sdk/include/um -I${XWIN_DIR}/sdk/include/shared")

set(_xwin_ldflags "/LIBPATH:${XWIN_DIR}/crt/lib/x86_64 /LIBPATH:${XWIN_DIR}/sdk/lib/um/x86_64 /LIBPATH:${XWIN_DIR}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_ldflags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_ldflags}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

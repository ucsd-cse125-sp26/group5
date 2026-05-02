set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Jolt's CMake adds -mavx2 as a PUBLIC compile option, which propagates to
# every target linking Jolt. MinGW-w64 GCC then emits vmovdqa %ymm, mem on
# stack-allocated locals — but the Windows x64 ABI only guarantees 16-byte
# stack alignment, so the 32-byte-aligned move faults at the first call.
# MSVC handles AVX-256 stack realignment correctly; MinGW does not.
# Forcing these off here keeps Jolt at SSE 4.2 for the cross-build.
set(USE_AVX OFF CACHE BOOL "Disabled on MinGW: AVX-256 vmovdqa faults on 16-byte-aligned ABI stack" FORCE)
set(USE_AVX2 OFF CACHE BOOL "Disabled on MinGW: AVX-256 vmovdqa faults on 16-byte-aligned ABI stack" FORCE)
set(USE_AVX512 OFF CACHE BOOL "Disabled on MinGW: AVX-256 vmovdqa faults on 16-byte-aligned ABI stack" FORCE)
# -mfma and -mf16c both imply -mavx, so GCC may still emit 256-bit YMM ops.
set(USE_F16C OFF CACHE BOOL "Disabled on MinGW: implies -mavx which re-enables 256-bit ymm emission" FORCE)
set(USE_FMADD OFF CACHE BOOL "Disabled on MinGW: implies -mavx which re-enables 256-bit ymm emission" FORCE)

if(DEFINED ENV{MINGW_PTHREAD_STATIC_LIB_DIR})
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -L$ENV{MINGW_PTHREAD_STATIC_LIB_DIR}")
endif()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

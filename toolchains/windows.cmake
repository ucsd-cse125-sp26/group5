set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# MinGW + Jolt's PUBLIC -mavx2 emits vmovdqa %ymm on 32-byte-aligned locals,
# but Windows x64 ABI only guarantees 16-byte stack alignment so it faults.
# Pin Jolt to SSE 4.2 for the cross-build.
set(USE_AVX OFF CACHE BOOL "MinGW: AVX-256 vs 16-byte ABI stack" FORCE)
set(USE_AVX2 OFF CACHE BOOL "MinGW: AVX-256 vs 16-byte ABI stack" FORCE)
set(USE_AVX512 OFF CACHE BOOL "MinGW: AVX-256 vs 16-byte ABI stack" FORCE)
# -mfma / -mf16c imply -mavx, which re-enables YMM emission.
set(USE_F16C OFF CACHE BOOL "MinGW: implies -mavx" FORCE)
set(USE_FMADD OFF CACHE BOOL "MinGW: implies -mavx" FORCE)

if(DEFINED ENV{MINGW_PTHREAD_STATIC_LIB_DIR})
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -L$ENV{MINGW_PTHREAD_STATIC_LIB_DIR}")
endif()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

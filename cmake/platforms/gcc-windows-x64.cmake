# Native UCRT64 GCC toolchain for 64-bit Windows.
message(STATUS "Using native MSYS2 UCRT64 GCC toolchain.")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(DEFINED ENV{MSYS2_UCRT64})
    file(TO_CMAKE_PATH "$ENV{MSYS2_UCRT64}" ANO_MSYS2_UCRT64_BIN)
else()
    set(ANO_MSYS2_UCRT64_BIN "C:/msys64/ucrt64/bin")
endif()

set(CMAKE_C_COMPILER "${ANO_MSYS2_UCRT64_BIN}/gcc.exe")
set(CMAKE_CXX_COMPILER "${ANO_MSYS2_UCRT64_BIN}/g++.exe")

# Optimization and debug information belong to CMAKE_BUILD_TYPE.
set(CMAKE_C_FLAGS_INIT "-m64 -march=x86-64")
set(CMAKE_CXX_FLAGS_INIT "-m64 -march=x86-64")

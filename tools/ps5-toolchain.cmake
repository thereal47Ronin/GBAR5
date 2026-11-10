# PSXS5 - CMake toolchain for libraries built for the PS5 (tools/build-netfs.sh):
# the repository's clang wrapper (tooling/prospero-clang18), static libraries only.
# SPDX-License-Identifier: GPL-3.0-or-later

set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
get_filename_component(PSXS5_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CMAKE_C_COMPILER "${PSXS5_ROOT}/tooling/prospero-clang18")
set(CMAKE_C_FLAGS_INIT "-O2 -fPIC -ffunction-sections -fdata-sections -march=znver2")
# the checks compile, they never link or run a program for the console
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH "$ENV{PS5_PAYLOAD_SDK}/target")

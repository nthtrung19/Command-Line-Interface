cmake_minimum_required(VERSION 3.16)

project(my_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Get the cli library -----------------------------------------------
# Default: fetch from GitHub, pinned to a commit already verified to build
# and run correctly for this project (see project notes: the latest tagged
# release, v2.1.0, is missing Cli::WrongCommandHandler and must not be used).
#
# Override CLI_LIBRARY_SOURCE_DIR to point at a local checkout instead
# (e.g. for offline builds), without editing this file:
#   cmake -S . -B build -DCLI_LIBRARY_SOURCE_DIR=/path/to/local/cli
set(CLI_LIBRARY_SOURCE_DIR "" CACHE PATH
    "Path to a local cli library checkout. Leave empty to fetch from GitHub.")

include(FetchContent)

if(CLI_LIBRARY_SOURCE_DIR)
    if(NOT EXISTS "${CLI_LIBRARY_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "CLI library not found at '${CLI_LIBRARY_SOURCE_DIR}'. "
            "Check CLI_LIBRARY_SOURCE_DIR, or clear it to fetch from GitHub instead.")
    endif()
    FetchContent_Declare(cli SOURCE_DIR "${CLI_LIBRARY_SOURCE_DIR}")
else()
    FetchContent_Declare(
        cli
        GIT_REPOSITORY https://github.com/daniele77/cli.git
        # Pinned commit, verified to build and provide Cli::WrongCommandHandler.
        # Do not switch to tag v2.1.0 (2023-06-29): it lacks that API.
        GIT_TAG 769c5fa060b94b39e43e1d6554775af7e73928cc)
endif()

FetchContent_MakeAvailable(cli)

# --- Executable ----------------------------------------------------------
add_executable(my_cli src/main.cpp)
target_link_libraries(my_cli PRIVATE cli::cli)

# NOTE: ZeroMQ (cppzmq/libzmq) will be added here once the Ares adapter
# is implemented. Not needed yet for the current console-only skeleton.
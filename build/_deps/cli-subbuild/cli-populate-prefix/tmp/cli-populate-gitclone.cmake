
if(NOT "/home/thanhtrung/CLI/my_cli/build/_deps/cli-subbuild/cli-populate-prefix/src/cli-populate-stamp/cli-populate-gitinfo.txt" IS_NEWER_THAN "/home/thanhtrung/CLI/my_cli/build/_deps/cli-subbuild/cli-populate-prefix/src/cli-populate-stamp/cli-populate-gitclone-lastrun.txt")
  message(STATUS "Avoiding repeated git clone, stamp file is up to date: '/home/thanhtrung/CLI/my_cli/build/_deps/cli-subbuild/cli-populate-prefix/src/cli-populate-stamp/cli-populate-gitclone-lastrun.txt'")
  return()
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf "/home/thanhtrung/CLI/my_cli/build/_deps/cli-src"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to remove directory: '/home/thanhtrung/CLI/my_cli/build/_deps/cli-src'")
endif()

# try the clone 3 times in case there is an odd git clone issue
set(error_code 1)
set(number_of_tries 0)
while(error_code AND number_of_tries LESS 3)
  execute_process(
    COMMAND "/usr/bin/git"  clone --no-checkout --config "advice.detachedHead=false" "https://github.com/daniele77/cli.git" "cli-src"
    WORKING_DIRECTORY "/home/thanhtrung/CLI/my_cli/build/_deps"
    RESULT_VARIABLE error_code
    )
  math(EXPR number_of_tries "${number_of_tries} + 1")
endwhile()
if(number_of_tries GREATER 1)
  message(STATUS "Had to git clone more than once:
          ${number_of_tries} times.")
endif()
if(error_code)
  message(FATAL_ERROR "Failed to clone repository: 'https://github.com/daniele77/cli.git'")
endif()

execute_process(
  COMMAND "/usr/bin/git"  checkout 769c5fa060b94b39e43e1d6554775af7e73928cc --
  WORKING_DIRECTORY "/home/thanhtrung/CLI/my_cli/build/_deps/cli-src"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to checkout tag: '769c5fa060b94b39e43e1d6554775af7e73928cc'")
endif()

set(init_submodules TRUE)
if(init_submodules)
  execute_process(
    COMMAND "/usr/bin/git"  submodule update --recursive --init 
    WORKING_DIRECTORY "/home/thanhtrung/CLI/my_cli/build/_deps/cli-src"
    RESULT_VARIABLE error_code
    )
endif()
if(error_code)
  message(FATAL_ERROR "Failed to update submodules in: '/home/thanhtrung/CLI/my_cli/build/_deps/cli-src'")
endif()

# Complete success, update the script-last-run stamp file:
#
execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy
    "/home/thanhtrung/CLI/my_cli/build/_deps/cli-subbuild/cli-populate-prefix/src/cli-populate-stamp/cli-populate-gitinfo.txt"
    "/home/thanhtrung/CLI/my_cli/build/_deps/cli-subbuild/cli-populate-prefix/src/cli-populate-stamp/cli-populate-gitclone-lastrun.txt"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to copy script-last-run stamp file: '/home/thanhtrung/CLI/my_cli/build/_deps/cli-subbuild/cli-populate-prefix/src/cli-populate-stamp/cli-populate-gitclone-lastrun.txt'")
endif()


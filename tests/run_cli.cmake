# Runs poker_cli once and checks both its exit code and its output, which plain add_test() can't do
# together (a PASS_REGULAR_EXPRESSION makes CTest ignore the exit code). Used by poker_cli_test() in
# CMakeLists.txt:
#
#   cmake -DCOMMAND=<program and arguments, separated by |> -DEXIT_CODE=<n> -DPATTERN=<regex>
#         -P run_cli.cmake
#
# The command is joined with | rather than CMake's list separator ;, which add_test() would split.
# PATTERN is a CMake regular expression matched against stdout and stderr together.

string(REPLACE "|" ";" command "${COMMAND}")
execute_process(
  COMMAND ${command}
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output
)

if(NOT exit_code STREQUAL EXIT_CODE)
  message(FATAL_ERROR
    "poker_cli exited with ${exit_code}, expected ${EXIT_CODE}. Output:\n${output}")
endif()
if(NOT output MATCHES "${PATTERN}")
  message(FATAL_ERROR "poker_cli's output doesn't match \"${PATTERN}\". Output:\n${output}")
endif()

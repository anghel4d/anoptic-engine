if(NOT DEFINED CXX OR NOT DEFINED SOURCE OR NOT DEFINED INCLUDE_DIR
        OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR
            "compile-failure test requires CXX, SOURCE, INCLUDE_DIR, and EXPECTED")
endif()

execute_process(
    COMMAND "${CXX}" -std=gnu++26 -freflection -fno-exceptions -fno-rtti
            -nostdlib++ -I "${INCLUDE_DIR}" -x c++ -fsyntax-only "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)

if(result EQUAL 0)
    message(FATAL_ERROR "invalid declaration compiled successfully: ${SOURCE}")
endif()

string(FIND "${output}" "${EXPECTED}" expected_position)
if(expected_position EQUAL -1)
    message(FATAL_ERROR
            "declaration failed for the wrong reason: ${SOURCE}\n${output}")
endif()

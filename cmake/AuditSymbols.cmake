# Reject C++ runtime dependencies in a linked engine binary.

if(NOT DEFINED ANO_BINARY OR NOT EXISTS "${ANO_BINARY}")
    message(FATAL_ERROR "ANO_BINARY must name an existing linked binary")
endif()

if(NOT DEFINED ANO_NM OR ANO_NM STREQUAL "" OR NOT EXISTS "${ANO_NM}")
    find_program(ANO_NM NAMES llvm-nm nm REQUIRED)
endif()

execute_process(
    COMMAND "${ANO_NM}" -u -C "${ANO_BINARY}"
    RESULT_VARIABLE ANO_NM_RESULT
    OUTPUT_VARIABLE ANO_UNDEFINED
    ERROR_VARIABLE ANO_NM_ERROR)
if(NOT ANO_NM_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Unable to inspect ${ANO_BINARY} with ${ANO_NM}: ${ANO_NM_ERROR}")
endif()

set(ANO_FORBIDDEN_SYMBOLS
    "std::.*basic_string<"
    "std::.*vector<"
    "std::.*locale"
    "std::.*ios_base"
    "std::.*bad_function_call"
    "std::.*bad_variant_access"
    "std::.*move_only_function"
    "std::.*copyable_function"
    "std::.*__format"
    "__cxa_throw"
    "_Unwind_")

foreach(ANO_PATTERN IN LISTS ANO_FORBIDDEN_SYMBOLS)
    if(ANO_UNDEFINED MATCHES "${ANO_PATTERN}")
        message(FATAL_ERROR
            "C++ runtime policy violation '${ANO_PATTERN}' in ${ANO_BINARY}")
    endif()
endforeach()

if(DEFINED ANO_READELF AND NOT ANO_READELF STREQUAL ""
        AND EXISTS "${ANO_READELF}")
    execute_process(
        COMMAND "${ANO_READELF}" -d "${ANO_BINARY}"
        RESULT_VARIABLE ANO_READELF_RESULT
        OUTPUT_VARIABLE ANO_DYNAMIC_SECTION
        ERROR_QUIET)
    if(ANO_READELF_RESULT EQUAL 0
            AND ANO_DYNAMIC_SECTION MATCHES
                "lib(std)?c\\+\\+|libstdc\\+\\+|libsupc\\+\\+")
        message(FATAL_ERROR "C++ runtime dependency in ${ANO_BINARY}")
    endif()
endif()

message(STATUS "C++ runtime symbol audit passed: ${ANO_BINARY}")

# Enforces the C++ standard-library header whitelist stored in .clang-tidy.

if(NOT DEFINED ANO_SOURCE_DIR)
    message(FATAL_ERROR "ANO_SOURCE_DIR is required")
endif()

if(NOT DEFINED ANO_POLICY_FILE)
    set(ANO_POLICY_FILE "${ANO_SOURCE_DIR}/.clang-tidy")
endif()
if(NOT EXISTS "${ANO_POLICY_FILE}")
    message(FATAL_ERROR "C++ stdlib policy file not found: ${ANO_POLICY_FILE}")
endif()

file(STRINGS "${ANO_POLICY_FILE}" ANO_POLICY_LINES)
set(ANO_READING_WHITELIST OFF)
set(ANO_ALLOWED_STD_HEADERS "")
foreach(ANO_LINE IN LISTS ANO_POLICY_LINES)
    if(ANO_LINE MATCHES "ANO_STDLIB_WHITELIST_BEGIN")
        set(ANO_READING_WHITELIST ON)
        continue()
    endif()
    if(ANO_LINE MATCHES "ANO_STDLIB_WHITELIST_END")
        set(ANO_READING_WHITELIST OFF)
        break()
    endif()
    if(NOT ANO_READING_WHITELIST OR ANO_LINE MATCHES "^[ \t]*value:")
        continue()
    endif()

    string(REGEX REPLACE "[ \t]*#.*$" "" ANO_LINE "${ANO_LINE}")
    string(REPLACE "," ";" ANO_TOKENS "${ANO_LINE}")
    foreach(ANO_TOKEN IN LISTS ANO_TOKENS)
        string(STRIP "${ANO_TOKEN}" ANO_TOKEN)
        if(NOT ANO_TOKEN STREQUAL "" AND NOT ANO_TOKEN MATCHES "^-"
                AND NOT ANO_TOKEN STREQUAL "*.h")
            list(APPEND ANO_ALLOWED_STD_HEADERS "${ANO_TOKEN}")
        endif()
    endforeach()
endforeach()

list(REMOVE_DUPLICATES ANO_ALLOWED_STD_HEADERS)
list(LENGTH ANO_ALLOWED_STD_HEADERS ANO_ALLOWED_HEADER_COUNT)
if(ANO_ALLOWED_HEADER_COUNT EQUAL 0)
    message(FATAL_ERROR "No C++ stdlib headers found in ${ANO_POLICY_FILE}")
endif()
list(FIND ANO_ALLOWED_STD_HEADERS vector ANO_VECTOR_ALLOWED)
if(NOT ANO_VECTOR_ALLOWED EQUAL -1)
    message(FATAL_ERROR "The denied header <vector> entered the whitelist")
endif()

if(NOT DEFINED ANO_POLICY_ROOTS)
    set(ANO_POLICY_ROOTS "${ANO_SOURCE_DIR}/include;${ANO_SOURCE_DIR}/src")
endif()

set(ANO_POLICY_FILES "")
foreach(ANO_ROOT IN LISTS ANO_POLICY_ROOTS)
    if(IS_DIRECTORY "${ANO_ROOT}")
        file(GLOB_RECURSE ANO_ROOT_FILES LIST_DIRECTORIES FALSE
            "${ANO_ROOT}/*.c"
            "${ANO_ROOT}/*.cpp"
            "${ANO_ROOT}/*.h")
        list(APPEND ANO_POLICY_FILES ${ANO_ROOT_FILES})
    elseif(EXISTS "${ANO_ROOT}")
        list(APPEND ANO_POLICY_FILES "${ANO_ROOT}")
    else()
        message(FATAL_ERROR "Policy scan root not found: ${ANO_ROOT}")
    endif()
endforeach()
list(REMOVE_DUPLICATES ANO_POLICY_FILES)

set(ANO_VIOLATIONS "")
set(ANO_ALLOWED_INCLUDE_MACROS
    FT_ERRORS_H
    FT_FREETYPE_H
    FT_MODULE_H
    FT_OUTLINE_H
    FT_SYSTEM_H
    FT_TRUETYPE_TABLES_H
    FT_TRUETYPE_TAGS_H)
foreach(ANO_FILE IN LISTS ANO_POLICY_FILES)
    file(STRINGS "${ANO_FILE}" ANO_INCLUDE_LINES
        REGEX "^[ \t]*#[ \t]*include")
    foreach(ANO_INCLUDE_LINE IN LISTS ANO_INCLUDE_LINES)
        if(NOT ANO_INCLUDE_LINE MATCHES
                "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]")
            if(ANO_INCLUDE_LINE MATCHES
                    "^[ \t]*#[ \t]*include[ \t]+([A-Z][A-Z0-9_]*)[ \t]*$")
                list(FIND ANO_ALLOWED_INCLUDE_MACROS "${CMAKE_MATCH_1}"
                    ANO_INCLUDE_MACRO_INDEX)
                if(NOT ANO_INCLUDE_MACRO_INDEX EQUAL -1)
                    continue()
                endif()
            endif()
            list(APPEND ANO_VIOLATIONS
                "${ANO_FILE}: computed or malformed include: ${ANO_INCLUDE_LINE}")
            continue()
        endif()

        set(ANO_HEADER "${CMAKE_MATCH_1}")
        set(ANO_IS_CPP_STDLIB OFF)
        if(ANO_HEADER MATCHES "^[A-Za-z0-9_]+$")
            set(ANO_IS_CPP_STDLIB ON)
        elseif(ANO_HEADER MATCHES
                "^(bits|debug|experimental|ext|parallel|tr1|backward|__)/")
            list(APPEND ANO_VIOLATIONS
                "${ANO_FILE}: private or experimental C++ header <${ANO_HEADER}> is forbidden")
            continue()
        endif()

        if(ANO_IS_CPP_STDLIB)
            list(FIND ANO_ALLOWED_STD_HEADERS "${ANO_HEADER}" ANO_HEADER_INDEX)
            if(ANO_HEADER_INDEX EQUAL -1)
                list(APPEND ANO_VIOLATIONS
                    "${ANO_FILE}: C++ stdlib header <${ANO_HEADER}> is not whitelisted")
            endif()
        endif()
    endforeach()
endforeach()

if(ANO_VIOLATIONS)
    list(JOIN ANO_VIOLATIONS "\n  " ANO_VIOLATION_TEXT)
    message(FATAL_ERROR
        "C++ stdlib policy violations:\n  ${ANO_VIOLATION_TEXT}")
endif()

list(LENGTH ANO_POLICY_FILES ANO_POLICY_FILE_COUNT)
message(STATUS
    "C++ stdlib policy: ${ANO_POLICY_FILE_COUNT} files; ${ANO_ALLOWED_HEADER_COUNT} whitelisted headers")

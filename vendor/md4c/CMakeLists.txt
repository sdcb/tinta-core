cmake_minimum_required(VERSION 3.16)

project(md4c VERSION 0.5.2 LANGUAGES C)
set(PROJECT_HOMEPAGE_URL "https://github.com/mity/md4c")

include(GNUInstallDirs)
include(CTest)

option(MD4C_USE_UTF16 "Build md4c and md4c-html with UTF-16 MD_CHAR" OFF)

if(MD4C_USE_UTF16)
    add_compile_definitions(MD4C_USE_UTF16)
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_executable(md4c-parser-test test/parser.c)
    target_link_libraries(md4c-parser-test PRIVATE md4c)
    set_target_properties(md4c-parser-test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src")
    add_test(NAME md4c-parser-test COMMAND md4c-parser-test)

    add_executable(md4c-smoke test/smoke.c)
    target_link_libraries(md4c-smoke PRIVATE md4c-html)
    set_target_properties(md4c-smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src")
    add_test(NAME md4c-smoke COMMAND md4c-smoke)
endif()

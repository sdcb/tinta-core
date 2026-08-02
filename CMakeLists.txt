cmake_minimum_required(VERSION 3.20)
project(TintaCore VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(TINTA_DEVELOPER_BUILD OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(TINTA_DEVELOPER_BUILD ON)
endif()

option(TINTA_BUILD_EXAMPLES "Build Tinta Core examples" ${TINTA_DEVELOPER_BUILD})
option(BUILD_TESTING "Build Tinta Core tests" ${TINTA_DEVELOPER_BUILD})
option(TINTA_BUILD_FUZZERS "Build parser fuzz targets" OFF)
option(TINTA_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
option(TINTA_ENABLE_UIA "Enable the UI Automation provider" ON)
option(TINTA_ENABLE_MERMAID "Enable Mermaid parsing and rendering" ON)
option(TINTA_ENABLE_SYNTAX "Enable code-block syntax highlighting" ON)
option(TINTA_ENABLE_REMOTE_IMAGES "Enable remote image downloads" ON)
option(TINTA_ENABLE_LOCAL_IMAGES "Enable local image decoding" ON)

set(TINTA_CORE_SOURCES
    vendor/md4c/src/md4c.c
    src/app.c
    src/control.c
    src/core.c
    src/document.c
    src/markdown.c
    src/renderer.c
    src/stream_async.c
    src/themes.c
    src/win32_utils.c)

if(TINTA_ENABLE_MERMAID)
    list(APPEND TINTA_CORE_SOURCES src/mermaid.c)
endif()
if(TINTA_ENABLE_REMOTE_IMAGES)
    list(APPEND TINTA_CORE_SOURCES src/image_cache.c)
endif()
if(TINTA_ENABLE_SYNTAX)
    list(APPEND TINTA_CORE_SOURCES src/syntax.c)
endif()
if(TINTA_ENABLE_UIA)
    list(APPEND TINTA_CORE_SOURCES
        src/uia_provider.c src/uia_semantics.c src/uia_text.c)
else()
    list(APPEND TINTA_CORE_SOURCES src/uia_stub.c)
endif()

add_library(tinta_core ${TINTA_CORE_SOURCES})
add_library(Tinta::Core ALIAS tinta_core)
target_include_directories(tinta_core BEFORE PRIVATE src/internal vendor/md4c/src)
target_include_directories(tinta_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(tinta_core PRIVATE
    d2d1 dwrite advapi32 shell32 shlwapi)
if(TINTA_ENABLE_REMOTE_IMAGES OR TINTA_ENABLE_LOCAL_IMAGES)
    target_link_libraries(tinta_core PRIVATE windowscodecs ole32)
endif()
if(TINTA_ENABLE_REMOTE_IMAGES)
    target_link_libraries(tinta_core PRIVATE urlmon)
endif()
if(TINTA_ENABLE_UIA)
    target_link_libraries(tinta_core PRIVATE oleaut32 uiautomationcore)
endif()
target_compile_definitions(tinta_core PRIVATE UNICODE _UNICODE TINTA_CORE_BUILD
    _CRT_SECURE_NO_WARNINGS
    TINTA_ENABLE_UIA=$<BOOL:${TINTA_ENABLE_UIA}>
    TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>
    TINTA_ENABLE_SYNTAX=$<BOOL:${TINTA_ENABLE_SYNTAX}>
    TINTA_ENABLE_REMOTE_IMAGES=$<BOOL:${TINTA_ENABLE_REMOTE_IMAGES}>
    TINTA_ENABLE_LOCAL_IMAGES=$<BOOL:${TINTA_ENABLE_LOCAL_IMAGES}>)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(tinta_core PUBLIC TINTA_CORE_SHARED)
endif()
if(MSVC)
    target_compile_options(tinta_core PRIVATE /utf-8 /W4 /WX)
    set_source_files_properties(vendor/md4c/src/md4c.c PROPERTIES
        COMPILE_OPTIONS /W0)
endif()
set_target_properties(tinta_core PROPERTIES
    OUTPUT_NAME tinta_core
    EXPORT_NAME Core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

if(TINTA_BUILD_EXAMPLES)
    add_executable(tinta_minimal WIN32 examples/minimal.c)
    target_link_libraries(tinta_minimal PRIVATE Tinta::Core)
    target_compile_definitions(tinta_minimal PRIVATE UNICODE _UNICODE)

    add_executable(tinta_demo WIN32 examples/demo.c)
    target_link_libraries(tinta_demo PRIVATE Tinta::Core comdlg32)
    target_compile_definitions(tinta_demo PRIVATE UNICODE _UNICODE)

    add_executable(tinta_chat_demo WIN32 examples/chat_demo.c)
    target_link_libraries(tinta_chat_demo PRIVATE Tinta::Core comctl32)
    target_compile_definitions(tinta_chat_demo PRIVATE UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(tinta_chat_demo PRIVATE /utf-8 /W4 /WX)
    endif()
endif()

if(BUILD_TESTING OR TINTA_BUILD_FUZZERS OR TINTA_BUILD_BENCHMARKS)
    if(BUILD_TESTING)
        enable_language(CXX)
        set(CMAKE_CXX_STANDARD 17)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        enable_testing()
    endif()
    set(TINTA_TEST_SUPPORT_SOURCES
        vendor/md4c/src/md4c.c src/core.c src/document.c src/markdown.c)
    if(TINTA_ENABLE_MERMAID)
        list(APPEND TINTA_TEST_SUPPORT_SOURCES src/mermaid.c)
    endif()
    if(TINTA_ENABLE_SYNTAX)
        list(APPEND TINTA_TEST_SUPPORT_SOURCES src/syntax.c)
    endif()
    add_library(tinta_test_support STATIC ${TINTA_TEST_SUPPORT_SOURCES})
    target_include_directories(tinta_test_support PRIVATE
        src/internal vendor/md4c/src)
    target_compile_definitions(tinta_test_support PRIVATE
        _CRT_SECURE_NO_WARNINGS
        TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>
        TINTA_ENABLE_SYNTAX=$<BOOL:${TINTA_ENABLE_SYNTAX}>)
    if(MSVC)
        target_compile_options(tinta_test_support PRIVATE /utf-8 /W4)
        set_source_files_properties(vendor/md4c/src/md4c.c PROPERTIES
            COMPILE_OPTIONS /W0)
    endif()

    if(BUILD_TESTING)
        add_executable(core_tests tests/core_tests.cpp)
        target_include_directories(core_tests PRIVATE src/internal)
        target_link_libraries(core_tests PRIVATE tinta_test_support)
        add_test(NAME core_containers COMMAND core_tests)

        if(TINTA_ENABLE_MERMAID)
            add_executable(mermaid_tests tests/mermaid_tests.cpp)
            target_include_directories(mermaid_tests PRIVATE src/internal)
            target_link_libraries(mermaid_tests PRIVATE tinta_test_support)
            add_test(NAME mermaid_parser COMMAND mermaid_tests)
        endif()

        add_executable(document_tests tests/document_tests.cpp)
        target_include_directories(document_tests PRIVATE src/internal)
        target_link_libraries(document_tests PRIVATE tinta_test_support)
        target_compile_definitions(document_tests PRIVATE
            TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>)
        add_test(NAME document_types COMMAND document_tests)

        if(TINTA_ENABLE_SYNTAX)
            add_executable(syntax_tests tests/syntax_tests.cpp)
            target_include_directories(syntax_tests PRIVATE src/internal)
            target_link_libraries(syntax_tests PRIVATE tinta_test_support)
            add_test(NAME syntax_highlighting COMMAND syntax_tests)
        endif()

        add_executable(control_tests tests/control_tests.cpp)
        target_link_libraries(control_tests PRIVATE Tinta::Core)
        if(TINTA_ENABLE_UIA)
            target_link_libraries(control_tests PRIVATE
                ole32 oleaut32 uiautomationcore)
        endif()
        target_compile_definitions(control_tests PRIVATE UNICODE _UNICODE
            TINTA_ENABLE_UIA=$<BOOL:${TINTA_ENABLE_UIA}>
            TINTA_ENABLE_REMOTE_IMAGES=$<BOOL:${TINTA_ENABLE_REMOTE_IMAGES}>
            TINTA_ENABLE_LOCAL_IMAGES=$<BOOL:${TINTA_ENABLE_LOCAL_IMAGES}>)
        add_test(NAME markdown_control COMMAND control_tests)
        if(TINTA_ENABLE_UIA)
            add_executable(uia_tests tests/uia_tests.cpp)
            target_link_libraries(uia_tests PRIVATE Tinta::Core
                ole32 oleaut32 uiautomationcore)
            target_compile_definitions(uia_tests PRIVATE UNICODE _UNICODE)
            add_test(NAME uia_provider COMMAND uia_tests)
        endif()
    endif()
endif()

if(TINTA_BUILD_FUZZERS)
    add_executable(tinta_markdown_fuzzer tests/markdown_fuzzer.c)
    target_include_directories(tinta_markdown_fuzzer PRIVATE
        src/internal vendor/md4c/src)
    target_link_libraries(tinta_markdown_fuzzer PRIVATE tinta_test_support)
endif()

if(TINTA_BUILD_BENCHMARKS)
    add_executable(tinta_benchmark tests/benchmark.c)
    target_include_directories(tinta_benchmark PRIVATE src/internal)
    target_link_libraries(tinta_benchmark PRIVATE tinta_test_support)
endif()

install(TARGETS tinta_core EXPORT TintaCoreTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/tinta_core.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md CHANGELOG.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/TintaCore)
install(EXPORT TintaCoreTargets NAMESPACE Tinta::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TintaCore)

configure_package_config_file(cmake/TintaCoreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TintaCoreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TintaCore)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TintaCoreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TintaCoreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TintaCoreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TintaCore)

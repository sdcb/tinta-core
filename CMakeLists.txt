cmake_minimum_required(VERSION 3.20)
project(TintaCore VERSION 1.3.0 LANGUAGES C)

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
option(TINTA_BUILD_LARGE_TESTS "Build 100 MiB editor stress tests" OFF)
option(TINTA_ENABLE_UIA "Enable the UI Automation provider" ON)
option(TINTA_ENABLE_MERMAID "Enable Mermaid parsing and rendering" ON)
option(TINTA_ENABLE_SYNTAX "Enable code-block syntax highlighting" ON)
option(TINTA_ENABLE_REMOTE_IMAGES "Enable remote image downloads" ON)
option(TINTA_ENABLE_LOCAL_IMAGES "Enable local image decoding" ON)
option(TINTA_ENABLE_SVG "Enable Direct2D SVG rendering" ON)
option(TINTA_ENABLE_MATH "Enable native TeX math layout and rendering" ON)

set(TINTA_CORE_SOURCES
    vendor/md4c/src/md4c.c
    vendor/md4c/src/entity.c
    src/app.c
    src/control.c
    src/control_api.c
    src/control_document.c
    src/control_input.c
    src/core.c
    src/document.c
    src/editor.c
    src/editor_document.c
    src/markdown.c
    src/renderer.c
    src/stream_async.c
    src/themes.c
    src/win32_utils.c)

if(TINTA_ENABLE_MATH)
    list(APPEND TINTA_CORE_SOURCES
        src/math.c src/math_layout.c src/math_opentype.c)
endif()

if(TINTA_ENABLE_MERMAID)
    list(APPEND TINTA_CORE_SOURCES src/mermaid.c)
endif()
if(TINTA_ENABLE_REMOTE_IMAGES)
    list(APPEND TINTA_CORE_SOURCES src/image_cache.c)
endif()
if(TINTA_ENABLE_SYNTAX)
    list(APPEND TINTA_CORE_SOURCES src/syntax.c)
endif()
if(TINTA_ENABLE_SVG)
    list(APPEND TINTA_CORE_SOURCES src/svg.c)
endif()
if(TINTA_ENABLE_UIA)
    list(APPEND TINTA_CORE_SOURCES
        src/editor_uia.c src/uia_provider.c src/uia_semantics.c src/uia_text.c)
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
    d2d1 dwrite imm32 advapi32 shell32 shlwapi)
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
    TINTA_ENABLE_LOCAL_IMAGES=$<BOOL:${TINTA_ENABLE_LOCAL_IMAGES}>
    TINTA_ENABLE_SVG=$<BOOL:${TINTA_ENABLE_SVG}>
    TINTA_ENABLE_MATH=$<BOOL:${TINTA_ENABLE_MATH}>)
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
    target_link_libraries(tinta_demo PRIVATE Tinta::Core comctl32 comdlg32)
    target_compile_definitions(tinta_demo PRIVATE UNICODE _UNICODE)

    add_executable(tinta_chat_demo WIN32 examples/chat_demo.c)
    target_link_libraries(tinta_chat_demo PRIVATE Tinta::Core comctl32)
    target_compile_definitions(tinta_chat_demo PRIVATE UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(tinta_chat_demo PRIVATE /utf-8 /W4 /WX)
    endif()

    add_executable(tinta_editor_demo WIN32 examples/editor.c)
    target_link_libraries(tinta_editor_demo PRIVATE Tinta::Core)
    target_compile_definitions(tinta_editor_demo PRIVATE UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(tinta_editor_demo PRIVATE /utf-8 /W4 /WX)
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
        vendor/md4c/src/md4c.c vendor/md4c/src/entity.c
        vendor/md4c/src/md4c-test.c
        src/core.c src/document.c src/editor_document.c src/markdown.c)
    if(TINTA_ENABLE_MATH)
        list(APPEND TINTA_TEST_SUPPORT_SOURCES
            src/math.c src/math_layout.c src/math_opentype.c)
    endif()
    if(TINTA_ENABLE_MERMAID)
        list(APPEND TINTA_TEST_SUPPORT_SOURCES src/mermaid.c)
    endif()
    if(TINTA_ENABLE_SYNTAX)
        list(APPEND TINTA_TEST_SUPPORT_SOURCES src/syntax.c)
    endif()
    if(TINTA_ENABLE_SVG)
        list(APPEND TINTA_TEST_SUPPORT_SOURCES src/svg.c)
    endif()
    add_library(tinta_test_support STATIC ${TINTA_TEST_SUPPORT_SOURCES})
    target_include_directories(tinta_test_support PRIVATE
        src/internal vendor/md4c/src)
    target_compile_definitions(tinta_test_support PRIVATE
        _CRT_SECURE_NO_WARNINGS MD4C_TEST_ALLOCATOR
        TINTA_EDITOR_TEST_ALLOCATOR
        TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>
        TINTA_ENABLE_SYNTAX=$<BOOL:${TINTA_ENABLE_SYNTAX}>
        TINTA_ENABLE_SVG=$<BOOL:${TINTA_ENABLE_SVG}>
        TINTA_ENABLE_MATH=$<BOOL:${TINTA_ENABLE_MATH}>)
    if(MSVC)
        target_compile_options(tinta_test_support PRIVATE /utf-8 /W4)
        set_source_files_properties(vendor/md4c/src/md4c.c PROPERTIES
            COMPILE_OPTIONS /W0)
    endif()

    if(BUILD_TESTING)
        set(TINTA_LAYOUT_TEST_LIBRARY Tinta::Core)
        if(BUILD_SHARED_LIBS)
            # layout_tests exercises internal renderer/app entry points which
            # are intentionally not exported by the public DLL. Shared builds
            # therefore use a static copy with the same feature set.
            add_library(tinta_layout_test_support STATIC ${TINTA_CORE_SOURCES})
            target_include_directories(tinta_layout_test_support BEFORE PRIVATE
                src/internal vendor/md4c/src include)
            target_compile_definitions(tinta_layout_test_support PRIVATE
                UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
                TINTA_ENABLE_UIA=$<BOOL:${TINTA_ENABLE_UIA}>
                TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>
                TINTA_ENABLE_SYNTAX=$<BOOL:${TINTA_ENABLE_SYNTAX}>
                TINTA_ENABLE_REMOTE_IMAGES=$<BOOL:${TINTA_ENABLE_REMOTE_IMAGES}>
                TINTA_ENABLE_LOCAL_IMAGES=$<BOOL:${TINTA_ENABLE_LOCAL_IMAGES}>
                TINTA_ENABLE_SVG=$<BOOL:${TINTA_ENABLE_SVG}>
                TINTA_ENABLE_MATH=$<BOOL:${TINTA_ENABLE_MATH}>)
            target_link_libraries(tinta_layout_test_support PRIVATE
                d2d1 dwrite imm32 advapi32 shell32 shlwapi)
            if(TINTA_ENABLE_REMOTE_IMAGES OR TINTA_ENABLE_LOCAL_IMAGES)
                target_link_libraries(tinta_layout_test_support PRIVATE
                    windowscodecs ole32)
            endif()
            if(TINTA_ENABLE_REMOTE_IMAGES)
                target_link_libraries(tinta_layout_test_support PRIVATE urlmon)
            endif()
            if(TINTA_ENABLE_UIA)
                target_link_libraries(tinta_layout_test_support PRIVATE
                    oleaut32 uiautomationcore)
            endif()
            if(MSVC)
                target_compile_options(tinta_layout_test_support PRIVATE
                    /utf-8 /W4 /WX)
            endif()
            set(TINTA_LAYOUT_TEST_LIBRARY tinta_layout_test_support)
        endif()

        set(TINTA_UNIT_TEST_SOURCES
            tests/unit_tests.cpp
            tests/test_harness.cpp
            tests/core_tests.cpp
            tests/document_tests.cpp
            tests/editor_document_tests.cpp
            tests/md4c_tests.cpp)
        if(TINTA_ENABLE_MATH)
            list(APPEND TINTA_UNIT_TEST_SOURCES tests/math_tests.cpp)
        endif()
        if(TINTA_ENABLE_SVG)
            list(APPEND TINTA_UNIT_TEST_SOURCES tests/svg_tests.cpp)
        endif()
        if(TINTA_ENABLE_MERMAID)
            list(APPEND TINTA_UNIT_TEST_SOURCES tests/mermaid_tests.cpp)
        endif()
        if(TINTA_ENABLE_SYNTAX)
            list(APPEND TINTA_UNIT_TEST_SOURCES tests/syntax_tests.cpp)
        endif()
        add_executable(tinta_unit_tests ${TINTA_UNIT_TEST_SOURCES})
        target_include_directories(tinta_unit_tests PRIVATE
            src/internal vendor/md4c/src)
        target_link_libraries(tinta_unit_tests PRIVATE tinta_test_support)
        target_compile_definitions(tinta_unit_tests PRIVATE
            TINTA_EDITOR_TEST_ALLOCATOR
            TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>
            TINTA_ENABLE_SYNTAX=$<BOOL:${TINTA_ENABLE_SYNTAX}>
            TINTA_ENABLE_SVG=$<BOOL:${TINTA_ENABLE_SVG}>
            TINTA_ENABLE_MATH=$<BOOL:${TINTA_ENABLE_MATH}>)
        add_test(NAME unit COMMAND tinta_unit_tests)
        set_tests_properties(unit PROPERTIES LABELS unit)

        if(TINTA_BUILD_LARGE_TESTS)
            add_executable(editor_large_tests tests/editor_large_tests.cpp)
            target_include_directories(editor_large_tests PRIVATE src/internal)
            target_link_libraries(editor_large_tests PRIVATE tinta_test_support)
            target_compile_definitions(editor_large_tests PRIVATE
                TINTA_EDITOR_TEST_ALLOCATOR)
            add_test(NAME editor_large COMMAND editor_large_tests)
            set_tests_properties(editor_large PROPERTIES LABELS "editor;large")

            add_executable(editor_large_control_tests
                tests/editor_large_control_tests.cpp)
            target_link_libraries(editor_large_control_tests PRIVATE Tinta::Core)
            target_compile_definitions(editor_large_control_tests PRIVATE
                UNICODE _UNICODE)
            add_test(NAME editor_large_control
                COMMAND editor_large_control_tests)
            set_tests_properties(editor_large_control PROPERTIES
                LABELS "editor;large")
        endif()

        add_executable(layout_tests tests/layout_tests.cpp)
        target_include_directories(layout_tests PRIVATE src/internal)
        target_link_libraries(layout_tests PRIVATE ${TINTA_LAYOUT_TEST_LIBRARY})
        target_compile_definitions(layout_tests PRIVATE
            TINTA_ENABLE_MERMAID=$<BOOL:${TINTA_ENABLE_MERMAID}>
            TINTA_ENABLE_SVG=$<BOOL:${TINTA_ENABLE_SVG}>
            TINTA_ENABLE_LOCAL_IMAGES=$<BOOL:${TINTA_ENABLE_LOCAL_IMAGES}>
            TINTA_ENABLE_MATH=$<BOOL:${TINTA_ENABLE_MATH}>)
        add_test(NAME renderer_layout COMMAND layout_tests)
        set_tests_properties(renderer_layout PROPERTIES LABELS layout)

        add_executable(control_tests tests/control_tests.cpp)
        target_link_libraries(control_tests PRIVATE Tinta::Core)
        if(TINTA_ENABLE_UIA)
            target_link_libraries(control_tests PRIVATE
                ole32 oleaut32 uiautomationcore)
        endif()
        target_compile_definitions(control_tests PRIVATE UNICODE _UNICODE
            TINTA_ENABLE_UIA=$<BOOL:${TINTA_ENABLE_UIA}>
            TINTA_ENABLE_REMOTE_IMAGES=$<BOOL:${TINTA_ENABLE_REMOTE_IMAGES}>
            TINTA_ENABLE_LOCAL_IMAGES=$<BOOL:${TINTA_ENABLE_LOCAL_IMAGES}>
            TINTA_ENABLE_SVG=$<BOOL:${TINTA_ENABLE_SVG}>
            TINTA_ENABLE_MATH=$<BOOL:${TINTA_ENABLE_MATH}>)
        add_test(NAME markdown_control COMMAND control_tests)
        set_tests_properties(markdown_control PROPERTIES LABELS control)

        add_executable(editor_control_tests tests/editor_control_tests.cpp)
        target_link_libraries(editor_control_tests PRIVATE Tinta::Core)
        target_compile_definitions(editor_control_tests PRIVATE
            UNICODE _UNICODE)
        add_test(NAME editor_control COMMAND editor_control_tests)
        set_tests_properties(editor_control PROPERTIES LABELS editor)
        if(TINTA_ENABLE_UIA)
            add_executable(uia_tests tests/uia_tests.cpp)
            target_link_libraries(uia_tests PRIVATE Tinta::Core
                ole32 oleaut32 uiautomationcore)
            target_compile_definitions(uia_tests PRIVATE UNICODE _UNICODE)
            add_test(NAME uia_provider COMMAND uia_tests)
            set_tests_properties(uia_provider PROPERTIES LABELS uia)

            add_executable(editor_uia_tests tests/editor_uia_tests.cpp)
            target_link_libraries(editor_uia_tests PRIVATE Tinta::Core
                ole32 oleaut32 uiautomationcore)
            target_compile_definitions(editor_uia_tests PRIVATE
                UNICODE _UNICODE)
            add_test(NAME editor_uia_provider COMMAND editor_uia_tests)
            set_tests_properties(editor_uia_provider PROPERTIES
                LABELS "editor;uia")
        endif()
    endif()
endif()

if(TINTA_BUILD_FUZZERS)
    add_executable(tinta_markdown_fuzzer fuzz/markdown_fuzzer.c)
    target_include_directories(tinta_markdown_fuzzer PRIVATE
        src/internal vendor/md4c/src)
    target_link_libraries(tinta_markdown_fuzzer PRIVATE tinta_test_support)

    if(TINTA_ENABLE_MATH)
        add_executable(tinta_math_fuzzer fuzz/math_fuzzer.c)
        target_include_directories(tinta_math_fuzzer PRIVATE src/internal)
        target_link_libraries(tinta_math_fuzzer PRIVATE tinta_test_support)
    endif()
endif()

if(TINTA_BUILD_BENCHMARKS)
    add_executable(tinta_benchmark benchmarks/benchmark.c)
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

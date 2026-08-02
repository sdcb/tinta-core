cmake_minimum_required(VERSION 3.20)
project(TintaCore VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TINTA_BUILD_EXAMPLES "Build Tinta Core examples" ON)

add_library(tinta_core
    vendor/md4c/src/md4c.c
    src/app.c
    src/control.c
    src/core.c
    src/document.c
    src/markdown.c
    src/mermaid.c
    src/renderer.c
    src/syntax.c
    src/themes.c
    src/uia_provider.c
    src/win32_utils.c)
add_library(Tinta::Core ALIAS tinta_core)
target_include_directories(tinta_core BEFORE PRIVATE src/internal vendor/md4c/src)
target_include_directories(tinta_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(tinta_core PRIVATE d2d1 dwrite windowscodecs
    advapi32 ole32 oleaut32 shell32 urlmon shlwapi uiautomationcore)
target_compile_definitions(tinta_core PRIVATE UNICODE _UNICODE TINTA_CORE_BUILD
    _CRT_SECURE_NO_WARNINGS)
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

if(BUILD_TESTING)
    add_library(tinta_test_support STATIC
        vendor/md4c/src/md4c.c
        src/core.c src/document.c src/markdown.c src/mermaid.c src/syntax.c)
    target_include_directories(tinta_test_support PRIVATE
        src/internal vendor/md4c/src)
    target_compile_definitions(tinta_test_support PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(tinta_test_support PRIVATE /utf-8 /W4)
        set_source_files_properties(vendor/md4c/src/md4c.c PROPERTIES
            COMPILE_OPTIONS /W0)
    endif()

    add_executable(core_tests tests/core_tests.cpp)
    target_include_directories(core_tests PRIVATE src/internal)
    target_link_libraries(core_tests PRIVATE tinta_test_support)
    add_test(NAME core_containers COMMAND core_tests)

    add_executable(mermaid_tests tests/mermaid_tests.cpp)
    target_include_directories(mermaid_tests PRIVATE src/internal)
    target_link_libraries(mermaid_tests PRIVATE tinta_test_support)
    add_test(NAME mermaid_parser COMMAND mermaid_tests)

    add_executable(document_tests tests/document_tests.cpp)
    target_include_directories(document_tests PRIVATE src/internal)
    target_link_libraries(document_tests PRIVATE tinta_test_support)
    add_test(NAME document_types COMMAND document_tests)

    add_executable(syntax_tests tests/syntax_tests.cpp)
    target_include_directories(syntax_tests PRIVATE src/internal)
    target_link_libraries(syntax_tests PRIVATE tinta_test_support)
    add_test(NAME syntax_highlighting COMMAND syntax_tests)

    add_executable(control_tests tests/control_tests.cpp)
    target_link_libraries(control_tests PRIVATE Tinta::Core ole32 oleaut32 uiautomationcore)
    target_compile_definitions(control_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME markdown_control COMMAND control_tests)
endif()

install(TARGETS tinta_core EXPORT TintaCoreTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/tinta_core.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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

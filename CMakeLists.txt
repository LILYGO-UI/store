cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
find_package(LilyGoUI CONFIG REQUIRED)
find_package(Threads REQUIRED)

# The CM0 sysroot ships libpng and its runtime zlib dependency, but not the
# zlib development package required by CMake's FindPNG module.
find_path(STORE_PNG_INCLUDE_DIR NAMES png.h PATH_SUFFIXES libpng16)
find_library(STORE_PNG_LIBRARY NAMES png16 png)
if(NOT STORE_PNG_INCLUDE_DIR OR NOT STORE_PNG_LIBRARY)
    message(FATAL_ERROR "libpng headers and library are required")
endif()
add_library(StorePNG::StorePNG UNKNOWN IMPORTED)
set_target_properties(StorePNG::StorePNG PROPERTIES
    IMPORTED_LOCATION "${STORE_PNG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${STORE_PNG_INCLUDE_DIR}")
mark_as_advanced(STORE_PNG_INCLUDE_DIR STORE_PNG_LIBRARY)

set(LILYGO_UI_STORE_PACKAGE_INSTALL_HELPER
    "${CMAKE_INSTALL_FULL_LIBEXECDIR}/${LILYGO_UI_PACKAGE_NAME}/${LILYGO_UI_PACKAGE_NAME}-package-install")
_lilygo_ui_escape_c(LILYGO_UI_STORE_PACKAGE_INSTALL_HELPER_C
    "${LILYGO_UI_STORE_PACKAGE_INSTALL_HELPER}")

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

add_library(lilygo_ui_store_core STATIC
    src/app_router.cpp
    src/domain/debian_version.cpp
    src/domain/store_model.cpp
    src/registry/http_client.cpp
    src/registry/json.cpp
    src/registry/registry_client.cpp
    src/registry/root_cache.cpp
    src/registry/sha256.cpp
    src/service/store_service.cpp
    src/system/apt_installer.cpp
    src/system/package_manager.cpp
    src/system/process.cpp)
target_include_directories(lilygo_ui_store_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_store_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_store_core PUBLIC cxx_std_17)
target_link_libraries(lilygo_ui_store_core PUBLIC Threads::Threads PRIVATE StorePNG::StorePNG)

add_library(lilygo_ui_store_ui STATIC
    src/app.cpp
    src/components/components.cpp
    src/store_view_model.cpp
    src/pages/catalog/catalog_view.cpp
    src/pages/catalog/catalog_view_model.cpp
    src/pages/detail/detail_view.cpp
    src/pages/detail/detail_view_model.cpp
    src/pages/installed/installed_view.cpp
    src/pages/installed/installed_view_model.cpp)
target_include_directories(lilygo_ui_store_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_store_ui PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_store_ui PUBLIC
    lilygo_ui_store_core
    LilyGoUI::Runtime)

add_executable(lilygo_ui_store src/main.cpp)
target_compile_options(lilygo_ui_store PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_store PRIVATE lilygo_ui_store_ui)
set_target_properties(lilygo_ui_store PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

# Keep the privileged installer independent of image and UI libraries.
add_executable(store-package-install
    src/system/package_install_main.cpp
    src/system/apt_installer.cpp
    src/system/process.cpp
    src/registry/sha256.cpp)
target_compile_options(store-package-install PRIVATE -Wall -Wextra -Wpedantic)
target_compile_features(store-package-install PRIVATE cxx_std_17)
target_include_directories(store-package-install PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(store-package-install PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_PACKAGE_NAME}-package-install")

configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)

install(TARGETS lilygo_ui_store
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT app)
install(TARGETS store-package-install
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/${LILYGO_UI_PACKAGE_NAME}
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps
    COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(store-core-tests tests/test_core.cpp)
    target_compile_options(store-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(store-core-tests PRIVATE
        lilygo_ui_store_core)
    add_test(NAME store-core COMMAND store-core-tests)

    add_executable(store-apt-tests tests/test_apt_installer.cpp)
    target_compile_options(store-apt-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(store-apt-tests PRIVATE lilygo_ui_store_core)
    add_test(NAME store-apt COMMAND store-apt-tests)

    add_executable(store-apt-integration-tests EXCLUDE_FROM_ALL
        tests/test_apt_integration.cpp)
    target_compile_options(store-apt-integration-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(store-apt-integration-tests PRIVATE lilygo_ui_store_core)

    add_executable(store-view-model-tests tests/test_view_model.cpp)
    target_compile_options(store-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(store-view-model-tests PRIVATE
        lilygo_ui_store_ui)
    add_test(NAME store-view-model COMMAND store-view-model-tests)

    add_executable(store-ui-contract-tests tests/test_ui_contract.cpp)
    target_compile_options(store-ui-contract-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(store-ui-contract-tests PRIVATE
        lilygo_ui_store_ui)
    add_test(NAME store-ui-contract COMMAND store-ui-contract-tests)

    add_executable(store-render-ui tests/render_ui.cpp)
    target_compile_options(store-render-ui PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(store-render-ui PRIVATE
        lilygo_ui_store_ui)
    foreach(viewport IN ITEMS compact-portrait portrait landscape large-landscape)
        if(viewport STREQUAL "compact-portrait")
            set(_width 320)
            set(_height 568)
        elseif(viewport STREQUAL "portrait")
            set(_width 568)
            set(_height 1232)
        elseif(viewport STREQUAL "landscape")
            set(_width 1232)
            set(_height 568)
        else()
            set(_width 1024)
            set(_height 768)
        endif()
        add_test(NAME store-render-${viewport} COMMAND store-render-ui
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/store-${viewport}.ppm)
    endforeach()
endif()

# New applications have no legacy CM0 package to replace. Add Debian migration
# fields only when this project supersedes an actually published package.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
    "libc6, libstdc++6, libdrm2, libfreetype6, libpng16-16, curl, pkexec, apt (>= 2.2), lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)

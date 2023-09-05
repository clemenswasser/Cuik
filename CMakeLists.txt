cmake_minimum_required(VERSION 3.26)

project(Cuik LANGUAGES C)

set(CUIK_ENABLE_PROJECTS "cuik;tildebackend" CACHE STRING "")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(cuik_install_component package_name)
    install(TARGETS ${ARGN}
        EXPORT "${package_name}Targets"
        COMPONENT "${package_name}"
        FILE_SET HEADERS)
    install(EXPORT "${package_name}Targets"
        FILE "${package_name}Targets.cmake"
        NAMESPACE "${package_name}::"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package_name}"
        COMPONENT "${package_name}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Config.cmake.in"
"@PACKAGE_INIT@
include(\"\${CMAKE_CURRENT_LIST_DIR}/${package_name}Targets.cmake\")
check_required_components(TildeBackend)")
    configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package_name}")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}ConfigVersion.cmake"
        VERSION [[0.0.0]] # TODO(cwasser): Add correct version
        COMPATIBILITY AnyNewerVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}ConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package_name}"
        COMPONENT "${package_name}")
    export(EXPORT "${package_name}Targets"
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/${package_name}Targets.cmake"
        NAMESPACE "${package_name}::")
endfunction()

add_subdirectory(common)
add_subdirectory(c11threads)

if(tildebackend IN_LIST CUIK_ENABLE_PROJECTS)
    add_subdirectory(tb)
endif()

if(cuik IN_LIST CUIK_ENABLE_PROJECTS)
    add_subdirectory(libCuik)
    add_subdirectory(main)
endif()

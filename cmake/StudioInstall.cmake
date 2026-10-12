#####################################################################
# Installation
#
# `cmake --install <build> --prefix <dir>` lays out a self-contained copy of
# the application in <dir>: everything a machine without MSYS2, vcpkg or HDF5
# needs to run it, and nothing else. The MSIX package (packaging/msix/) is that
# directory plus a manifest, so whatever is missing here is missing there.
#
#   bin/        bayests-studio.exe, the bayests command line, every DLL both
#               load, and Qt's plugins (windeployqt)
#   licenses/   one folder per component, as Help > Third-party licences says
#
# The bayests executable is installed from here rather than by turning on
# BAYESTS_INSTALL, which would also install BayesTS's own documents and CPack
# configuration into the studio's tree.

include(GNUInstallDirs)

get_filename_component(_studio_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

install(TARGETS bayests-studio bayests
    RUNTIME_DEPENDENCY_SET studio_runtime_deps
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION .
)

if(WIN32)
    # file(GET_RUNTIME_DEPENDENCIES) does not search PATH on Windows, only the
    # directories it is given: the compiler's bin/ (the GCC runtime, and in an
    # MSYS2 toolchain Qt and Qwt too) and the bin/ of every prefix a
    # dependency was found in (OpenBLAS, HDF5). STUDIO_RUNTIME_DEP_DIRS adds
    # any others.
    set(STUDIO_RUNTIME_DEP_DIRS "" CACHE STRING
        "Extra directories to search for the DLLs the installed executables load")

    set(_studio_dep_dirs "${_studio_compiler_dir}")
    if(TARGET Qt6::Core)
        get_target_property(_studio_qtcore Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(NOT _studio_qtcore)
            get_target_property(_studio_qtcore Qt6::Core IMPORTED_LOCATION)
        endif()
        if(_studio_qtcore)
            get_filename_component(_studio_qt_bin "${_studio_qtcore}" DIRECTORY)
            list(APPEND _studio_dep_dirs "${_studio_qt_bin}")
        endif()
    endif()
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND _studio_dep_dirs "${_prefix}/bin")
    endforeach()
    list(APPEND _studio_dep_dirs ${STUDIO_RUNTIME_DEP_DIRS})
    list(REMOVE_DUPLICATES _studio_dep_dirs)

    install(RUNTIME_DEPENDENCY_SET studio_runtime_deps
        DIRECTORIES ${_studio_dep_dirs}
        # Windows API sets are virtual: they resolve to nothing on disk.
        PRE_EXCLUDE_REGEXES
            [[api-ms-win-.*]]
            [[ext-ms-.*]]
        # Whatever resolved into the Windows directory belongs to the OS.
        POST_EXCLUDE_REGEXES
            [[.*[/\\][Ss]ystem32[/\\].*\.dll]]
            [[.*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*\.dll]]
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

# Qt's plugins (the Windows platform plugin, styles, image formats, the SVG
# icon engine) are loaded at run time, so no dependency scan finds them;
# windeployqt does. The DLLs it would also copy are already in the set above.
#
# Left to itself windeployqt also ships Qt Network with its TLS and
# network-information plugins, which nothing here loads. The application
# makes no network connection, and a security review reads the package's file
# list, so they are kept out. The D3D and DXC compilers are part of Windows
# 10 and later and need not travel with it either.
qt_generate_deploy_app_script(
    TARGET bayests-studio
    OUTPUT_SCRIPT _studio_deploy_script
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_TOOL_OPTIONS
        --no-network
        --skip-plugin-types tls,networkinformation,generic
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        --no-opengl-sw
)
install(SCRIPT "${_studio_deploy_script}")

#####################################################################
# Licence texts
#
# What LICENSING.md says each component obliges us to ship. They are looked
# up where this machine's dependencies came from; a text that cannot be found
# is a warning at configure time rather than a silently incomplete package.
# STUDIO_LICENSE_<NAME> overrides any of them.

function(studio_install_licence name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "NAMES;HINTS")
    find_file(STUDIO_LICENSE_${name}
        NAMES ${arg_NAMES}
        HINTS ${arg_HINTS}
        NO_DEFAULT_PATH
        DOC "Licence text installed as licenses/${name}/"
    )
    if(STUDIO_LICENSE_${name})
        install(FILES "${STUDIO_LICENSE_${name}}"
            DESTINATION licenses/${name})
    else()
        message(WARNING "No licence text found for ${name}; set STUDIO_LICENSE_${name}. "
                        "A package built from this tree is missing it.")
    endif()
endfunction()

get_filename_component(_studio_msys_prefix "${_studio_compiler_dir}" DIRECTORY)
set(_studio_lic "${_studio_msys_prefix}/share/licenses")

# The studio's own: the end-user licence agreement the setup asks the user
# to accept, and Help > Licence Agreement shows.
install(FILES "${PROJECT_SOURCE_DIR}/EULA.txt" DESTINATION "licenses/BayesTS Studio")

# The fonts in the resources, SIL OFL 1.1, each with its own copyright notice.
install(FILES "${PROJECT_SOURCE_DIR}/src/app/fonts/Inter-LICENSE.txt"
        DESTINATION "licenses/Inter" RENAME "LICENSE.txt")
install(FILES "${PROJECT_SOURCE_DIR}/src/app/fonts/SourceSerif4-LICENSE.md"
        DESTINATION "licenses/Source Serif 4" RENAME "LICENSE.md")

studio_install_licence(BayesTS   NAMES LICENSE         HINTS "${bayests_SOURCE_DIR}")
studio_install_licence(Qt        NAMES LGPL-3.0-only.txt HINTS "${_studio_lic}/qt6-base")
studio_install_licence(Qwt       NAMES COPYING         HINTS "${_studio_lic}/qwt-qt6")
studio_install_licence(GCC       NAMES COPYING.RUNTIME HINTS "${_studio_lic}/gcc-libs")
studio_install_licence(HDF5      NAMES LICENSE COPYING HINTS ${CMAKE_PREFIX_PATH})
studio_install_licence(HighFive  NAMES LICENSE         HINTS ${HIGHFIVE_DIR} ${CMAKE_PREFIX_PATH})

# vcpkg names every package's licence `copyright`, so these two need their own
# directory as a hint and cannot share the search above.
foreach(_pkg IN ITEMS openblas armadillo)
    set(_hints "")
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND _hints "${_prefix}/share/${_pkg}")
    endforeach()
    list(APPEND _hints "${_studio_lic}/${_pkg}")
    if(_pkg STREQUAL "openblas")
        studio_install_licence(OpenBLAS  NAMES copyright LICENSE HINTS ${_hints})
    else()
        studio_install_licence(Armadillo NAMES copyright LICENSE.txt HINTS ${_hints})
    endif()
endforeach()

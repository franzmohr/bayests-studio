#####################################################################
# MSIX package (Windows)
#
# An MSIX is registered per user, needs no administrator rights, and installs
# into C:\Program Files\WindowsApps, which users cannot write to -- so it is
# the per-user installation an IT department that refuses per-machine
# installers can still allowlist with one AppLocker packaged-app rule on the
# publisher. README.md in this directory has the details and the commands.
#
# Configuring writes the manifest (and, given an update location, the
# .appinstaller file) into <build>/msix/; build-msix.ps1 installs the
# application next to it, packs the layout with makeappx and signs it.

if(NOT WIN32)
    return()
endif()

set(STUDIO_MSIX_PUBLISHER "CN=Franz X. Mohr" CACHE STRING
    "MSIX publisher: must equal the Subject of the signing certificate exactly")
set(STUDIO_MSIX_PUBLISHER_DISPLAY_NAME "Franz X. Mohr" CACHE STRING
    "Publisher name shown to the user")
set(STUDIO_MSIX_IDENTITY_NAME "FranzXMohr.BayesTSStudio" CACHE STRING
    "MSIX package identity; never change it once a package has shipped, or updates become a second app")
set(STUDIO_MSIX_REVISION "0" CACHE STRING
    "Fourth part of the package version, for rebuilding the same release")
set(STUDIO_MSIX_UPDATE_URI "" CACHE STRING
    "Folder (https:// or file://server/share/...) the .msix and .appinstaller are published to; empty for none")

# MSIX versions have four parts, and an update must raise them.
set(STUDIO_MSIX_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${STUDIO_MSIX_REVISION}")
set(STUDIO_MSIX_PACKAGE_FILE "BayesTSStudio_${STUDIO_MSIX_VERSION}_x64.msix")

set(_msix_out "${CMAKE_BINARY_DIR}/msix")
configure_file(AppxManifest.xml.in "${_msix_out}/AppxManifest.xml" @ONLY)

# The values the packaging script needs, so it never has to parse the cache.
file(WRITE "${_msix_out}/package.env"
    "PACKAGE_FILE=${STUDIO_MSIX_PACKAGE_FILE}\n"
    "VERSION=${STUDIO_MSIX_VERSION}\n"
    "PUBLISHER=${STUDIO_MSIX_PUBLISHER}\n"
    "ASSETS=${CMAKE_CURRENT_SOURCE_DIR}/Assets\n"
)

if(STUDIO_MSIX_UPDATE_URI)
    string(REGEX REPLACE "/+$" "" STUDIO_MSIX_UPDATE_URI_TRIMMED "${STUDIO_MSIX_UPDATE_URI}")
    configure_file(BayesTSStudio.appinstaller.in "${_msix_out}/BayesTSStudio.appinstaller" @ONLY)
else()
    file(REMOVE "${_msix_out}/BayesTSStudio.appinstaller")
endif()

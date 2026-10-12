#####################################################################
# Setup program (Windows, NSIS)
#
# A setup.exe for students and anyone installing on a machine of their own:
# per user, no administrator rights, into %LOCALAPPDATA%\Programs. The MSIX
# (packaging/msix/) remains the package for machines an IT department manages.
# NSIS is under the zlib licence, free for commercial use; the WiX Toolset an
# .msi would need asks a fee of commercial users from version 6 on.
#
# Configuring writes the script into <build>/nsis/; build-setup.ps1 installs
# the application next to it, runs makensis and optionally signs the result.

if(NOT WIN32)
    return()
endif()

set(STUDIO_SETUP_PUBLISHER "Franz X. Mohr" CACHE STRING
    "Publisher shown in the setup and in Settings > Apps")

set(_setup_out "${CMAKE_BINARY_DIR}/nsis")
set(STUDIO_ICON_FILE "${PROJECT_SOURCE_DIR}/src/app/icons/bayests-studio.ico")
set(STUDIO_EULA_FILE "${PROJECT_SOURCE_DIR}/EULA.txt")
configure_file(BayesTSStudio.nsi.in "${_setup_out}/BayesTSStudio.nsi" @ONLY)
file(WRITE "${_setup_out}/package.env"
    "SETUP_FILE=BayesTSStudio-${PROJECT_VERSION}-setup.exe\n"
    "VERSION=${PROJECT_VERSION}\n"
    "EULA=${STUDIO_EULA_FILE}\n"
)

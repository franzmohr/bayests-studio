# The setup program (Windows)

`BayesTSStudio-<version>-setup.exe` is the installer for students and anyone
else installing on a machine of their own. By default it installs just for
the current user, needing no administrator rights, into
`%LOCALAPPDATA%\Programs\BayesTS Studio`; a page after the licence offers to
install for all users into Program Files instead, and asks for administrator
rights only then. It adds a Start menu entry, if ticked the bayests command
line to the PATH and a desktop shortcut, and registers itself under
Settings › Apps, from where it uninstalls. The MSIX
(`packaging/msix/README.md`) remains the package for machines an IT
department manages.

## Why NSIS and not an .msi

An .msi needs the WiX Toolset, which from version 6 asks commercial users
(above USD 10,000 of annual revenue) for an Open Source Maintenance Fee. NSIS
is under the zlib licence: free for any use, commercial included, with
nothing to ship or acknowledge in the setup it builds. Inno Setup would do
too, but asks commercial users to buy a licence.

## Building it

From a configured and built tree (a Release build for anything shipped):

    packaging\nsis\build-setup.ps1 -BuildDir build\my-windows-release

It installs the tree with `cmake --install` into `<build>\nsis\layout` -- the
same layout the MSIX is packed from, so the two ship the same files -- and runs
`makensis` (on PATH, in `D:\NSIS`, in Program Files, or `-Makensis`). The
setup lands in `<build>\nsis\out`.

Sign it for anything handed out: an unsigned setup draws a SmartScreen
warning a student has to click through ("More info › Run anyway").

    packaging\nsis\build-setup.ps1 -BuildDir build\my-windows-release -CertificateThumbprint <thumbprint>
    packaging\nsis\build-setup.ps1 -BuildDir build\my-windows-release -PfxFile cert.pfx

## The licence

The studio is free and open source under the BSD 3-Clause License. The setup
shows `LICENSE` (the repository root) for information before anything is
installed, with Next rather than I Agree; the same text is installed into
`licenses\BayesTS Studio\` and opens from Help > Licence.

## Behaviour

- **Scope.** *Just for me* (the default for a first installation) writes
  under `HKCU` and the user's profile; *For all users* under `HKLM`, Program
  Files and the common Start menu. Choosing the latter without administrator
  rights starts the setup again through UAC (`runas`), which carries on from
  the folder page; the finish page then starts the studio through Explorer, so
  that it does not run elevated. An update keeps the scope of the installation
  it replaces.
- **PATH** (ticked by default) gets one folder, `<install>\cmd`, holding only
  `bayests.cmd`, which starts `bin\bayests.exe` and passes its exit code on.
  `bin\` itself is not put on PATH: its Qt and HDF5 DLLs would be found by any
  other program looking for a DLL of that name. Just for me edits the user
  PATH, for all users the system PATH; every other entry is kept as it was,
  unexpanded, and a PATH over 8000 characters is left alone (the log says so).
  Terminals opened afterwards see it.
- **A previous installation is uninstalled first**, by running its own
  uninstaller silently: the current user's wherever its folder is, and the
  all-users one when installing for all users. One for all users stays when
  installing just for the current user, which needs no administrator rights
  to remove it; the users page says so. Then every folder the setup writes
  (`bin\`, `licenses\`, `share\`, as the layout has them -- `build-setup.ps1`
  lists them from the layout) is replaced whole, so no file an older version
  shipped lingers.
- **Silent install:** `setup.exe /S`, with `/ALLUSERS` or `/CURRENTUSER` for
  the scope (default as above), `/NOPATH` to leave PATH alone, and
  `/D=<folder>` (last on the command line) for another folder -- for a lab
  image. `/S /ALLUSERS` must itself run elevated and ends with exit code 740
  (`ERROR_ELEVATION_REQUIRED`) otherwise.
- **Uninstall** removes the application, the shortcuts, the PATH entry and the
  Apps entry. One for all users asks for administrator rights. It leaves the
  folder if anything else is in it, and never touches projects, model files
  or settings.

## Still to do

- A code-signing certificate, so that the setup draws no SmartScreen
  warning; `build-setup.ps1` signs with it once there is one.


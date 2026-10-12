# The setup program (Windows)

`BayesTSStudio-<version>-setup.exe` is the installer for students and anyone
else installing on a machine of their own. It needs no administrator rights:
it installs per user into `%LOCALAPPDATA%\Programs\BayesTS Studio`, adds a
Start menu entry (and, if ticked, a desktop shortcut), and registers itself
under Settings › Apps, from where it uninstalls. The MSIX
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

## Behaviour

- **Silent install:** `setup.exe /S`, and `/D=<folder>` (last on the command
  line) for another folder -- for a lab image.
- **Updates:** running a newer setup over an installation replaces every
  folder the setup writes (`bin\`, `licenses\`, `share\`, as the layout has
  them -- `build-setup.ps1` lists them from the layout), so no file an older
  version shipped lingers.
- **Uninstall** removes the application, the shortcuts and the Apps entry. It
  leaves the folder if anything else is in it, and never touches projects,
  model files or settings.

## Still to do

- An application icon: the setup and the shortcuts show the defaults until
  there is one.
- A licence agreement page, once the studio has its end-user licence.

# The Windows package (MSIX)

BayesTS Studio ships on Windows as an MSIX package, because it is the one
format that satisfies both what an IT department with strict rules asks for
and what it refuses:

- **Per user, no administrator rights.** Every MSIX is registered for the user
  who installs it. There is no per-machine installer to approve.
- **Not in user-writable space.** The files go under
  `C:\Program Files\WindowsApps`, which users cannot write to. A per-user MSI
  installs into `%LOCALAPPDATA%`, which is exactly what AppLocker and WDAC
  block by default.
- **One allowlist rule.** An AppLocker *packaged app* rule on the publisher
  and the package name (`FranzXMohr.BayesTSStudio`) covers this and every
  later version.
- **Signed, or it does not install**, and it uninstalls without leaving
  anything behind.
- **Nothing phones home.** Updates come from wherever IT publishes them
  (see "Updates" below); the application contacts nothing.

The package holds `bayests-studio.exe` and the `bayests` command line, which
an app execution alias puts on the user's PATH as `bayests`. It is a
full-trust desktop application: it reads and writes files as the user,
including on mapped drives and UNC paths.

## Building a package

1. **Build a release tree** with the usual preset:

   ```bash
   cmake --preset my-windows-release
   cmake --build build/my-windows-release
   ```

   Configuring writes `build/<preset>/msix/AppxManifest.xml` from
   `AppxManifest.xml.in`. Check the warnings: a licence text that could not
   be found is reported there, and the package would be missing it.

2. **Have `makeappx` and `signtool`.** They come with the Windows SDK, or
   without installing it with the NuGet package
   `Microsoft.Windows.SDK.BuildTools` (unpack it to
   `%USERPROFILE%\.nuget\packages\microsoft.windows.sdk.buildtools\<version>\`,
   or pass its `bin\<sdk-version>\x64` directory as `-SdkBinDir`).

3. **Pack and sign:**

   ```powershell
   packaging\msix\build-msix.ps1 -BuildDir build\my-windows-release -CertificateThumbprint <thumbprint>
   ```

   The package lands in `build\<preset>\msix\out\`. `-PfxFile`, Azure Trusted
   Signing (`-TrustedSigningDlib`, `-TrustedSigningMetadata`) and `-NoSign`
   are the alternatives to a thumbprint; `-SignContents` also signs every
   `.exe` and `.dll` inside, for organisations whose rules check those files
   individually.

The layout the script packs is exactly what `cmake --install` produces
(`cmake/StudioInstall.cmake`): both executables, every DLL they load, Qt's
plugins, and `licenses/`. Running `bin\bayests-studio.exe` from an install
prefix with nothing else on `PATH` is the quickest test that it is complete.

## The certificate

The manifest's `Publisher` must equal the signing certificate's subject
exactly; it is the cache variable `STUDIO_MSIX_PUBLISHER` (default
`CN=Franz X. Mohr`). For a real release that is the subject of an
organisation-validated code-signing certificate, or of an Azure Trusted
Signing profile -- whichever is chosen, set the variable to match it.

**Never change `STUDIO_MSIX_IDENTITY_NAME` or the publisher after a package
has shipped.** Windows identifies the app by both, and a package with either
changed is a second application rather than an update.

For testing on your own machine, `new-test-certificate.ps1` creates a
self-signed certificate with the right subject. Trusting it takes
administrator rights once (the script prints the command); a package signed
with it installs nowhere else.

## Versions

The package version is the project version plus a fourth part,
`STUDIO_MSIX_REVISION` (default 0), for rebuilding the same release. Windows
installs a package over an older one only if the version is higher.

## Updates

Set `STUDIO_MSIX_UPDATE_URI` to the folder the packages are published to, a
web address or a share (`file://server/share/BayesTSStudio`), and configuring
also writes `BayesTSStudio.appinstaller`. Publish it together with each
`.msix`; Windows then checks that folder once a day and offers the newer
version. Without it, IT distributes each version itself (Intune, or
`Add-AppxPackage`).

## What IT needs to know

- **Sideloading** -- installing packages from outside the Store -- is on by
  default since Windows 10 2004. Some organisations switch it off by policy.
- **Installation routes:** double-clicking the `.msix` (App Installer),
  `Add-AppxPackage <file>` in PowerShell, or per-user deployment through
  Intune. Microsoft switched off the `ms-appinstaller:` web link by default
  in 2023; the routes above are not affected.
- **AppLocker:** a packaged app rule with publisher `CN=...` (as above),
  package name `FranzXMohr.BayesTSStudio`, any version.
- **What the application writes:** its settings (the registry, under the
  package's own virtualised hive) and the model files the user saves. Nothing
  into its install folder; MSIX would refuse it.

## Assets

`Assets/*.png` are the application's icon at the sizes the manifest names
(50, 44 and 150 pixels square), drawn with every other size of it by
`tools/make_icon.py`; change the icon there, not here.

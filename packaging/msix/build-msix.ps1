<#
.SYNOPSIS
    Packs a configured and built BayesTS Studio tree into a signed MSIX.

.DESCRIPTION
    Installs the build into <BuildDir>\msix\layout with `cmake --install`,
    adds the manifest CMake configured and the logos, packs the layout with
    makeappx and signs the package with signtool. The result, and the
    .appinstaller file if STUDIO_MSIX_UPDATE_URI was set, land in -OutDir.

    makeappx and signtool come from the Windows SDK, or from the NuGet
    package Microsoft.Windows.SDK.BuildTools; pass -SdkBinDir to name the
    directory holding them if neither is found.

    Signing, one of:
      -CertificateThumbprint  a certificate in Cert:\CurrentUser\My (also what
                              new-test-certificate.ps1 creates)
      -PfxFile                a .pfx; its password in $env:STUDIO_PFX_PASSWORD
      -TrustedSigningDlib and -TrustedSigningMetadata
                              Azure Trusted Signing
      -NoSign                 leave it unsigned (it will not install)

.EXAMPLE
    packaging\msix\build-msix.ps1 -BuildDir build\my-windows-release -CertificateThumbprint 0123...
#>
[CmdletBinding(DefaultParameterSetName = 'Thumbprint')]
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [string] $OutDir,
    [string] $SdkBinDir,

    [Parameter(ParameterSetName = 'Thumbprint', Mandatory)] [string] $CertificateThumbprint,
    [Parameter(ParameterSetName = 'Pfx', Mandatory)] [string] $PfxFile,
    [Parameter(ParameterSetName = 'TrustedSigning', Mandatory)] [string] $TrustedSigningDlib,
    [Parameter(ParameterSetName = 'TrustedSigning', Mandatory)] [string] $TrustedSigningMetadata,
    [Parameter(ParameterSetName = 'NoSign', Mandatory)] [switch] $NoSign,

    # Also sign every .exe and .dll inside the package, for organisations
    # whose WDAC or AppLocker DLL rules check the files' own signatures. The
    # package signature alone is what packaged-app rules look at.
    [switch] $SignContents,
    [string] $TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$BuildDir = (Resolve-Path $BuildDir).Path
$msixDir = Join-Path $BuildDir 'msix'
$envFile = Join-Path $msixDir 'package.env'
if (-not (Test-Path $envFile)) {
    throw "$envFile not found: configure $BuildDir with this version of the studio first."
}
$pkg = @{}
foreach ($line in Get-Content $envFile) {
    if ($line -match '^([A-Z_]+)=(.*)$') { $pkg[$Matches[1]] = $Matches[2] }
}
if (-not $OutDir) { $OutDir = Join-Path $msixDir 'out' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

function Find-SdkTool([string] $name) {
    $candidates = @()
    if ($SdkBinDir) { $candidates += Join-Path $SdkBinDir $name }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $candidates += Get-ChildItem $kits -Directory |
            Where-Object { $_.Name -match '^\d+\.' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName "x64\$name" }
    }
    $nuget = Join-Path $env:USERPROFILE '.nuget\packages\microsoft.windows.sdk.buildtools'
    if (Test-Path $nuget) {
        $candidates += Get-ChildItem $nuget -Recurse -Filter $name |
            Where-Object { $_.DirectoryName -match '\\x64$' } |
            Sort-Object FullName -Descending |
            ForEach-Object FullName
    }
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    throw "$name not found. Install the Windows SDK, or the NuGet package Microsoft.Windows.SDK.BuildTools, or pass -SdkBinDir."
}

$makeappx = Find-SdkTool 'makeappx.exe'
$signtool = if ($NoSign) { $null } else { Find-SdkTool 'signtool.exe' }

function Invoke-Native([string] $exe, [string[]] $arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path -Leaf $exe) failed with exit code $LASTEXITCODE" }
}

function Get-SignArguments {
    $common = @('sign', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256')
    switch ($PSCmdlet.ParameterSetName) {
        'Thumbprint' { return $common + @('/sha1', $CertificateThumbprint, '/s', 'My') }
        'Pfx' {
            $a = $common + @('/f', (Resolve-Path $PfxFile).Path)
            if ($env:STUDIO_PFX_PASSWORD) { $a += @('/p', $env:STUDIO_PFX_PASSWORD) }
            return $a
        }
        'TrustedSigning' {
            return $common + @('/dlib', (Resolve-Path $TrustedSigningDlib).Path,
                               '/dmdf', (Resolve-Path $TrustedSigningMetadata).Path)
        }
    }
}

# 1. The layout: exactly what `cmake --install` produces, plus the manifest.
$layout = Join-Path $msixDir 'layout'
if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
Write-Host "Installing $BuildDir into $layout"
Invoke-Native 'cmake' @('--install', $BuildDir, '--prefix', $layout)

Copy-Item (Join-Path $msixDir 'AppxManifest.xml') $layout
Copy-Item -Recurse $pkg['ASSETS'] (Join-Path $layout 'Assets')
Get-ChildItem (Join-Path $layout 'Assets') -Exclude '*.png' | Remove-Item -Force

foreach ($exe in 'bin\bayests-studio.exe', 'bin\bayests.exe') {
    if (-not (Test-Path (Join-Path $layout $exe))) {
        throw "$exe is missing from the layout; the install step is incomplete."
    }
}
# Where windeployqt puts the plugins follows Qt's deployment settings (bin\qt.conf
# names the folder); without the platform plugin the application does not start.
if (-not (Get-ChildItem $layout -Recurse -Filter 'qwindows.dll' | Select-Object -First 1)) {
    throw "Qt's platform plugin qwindows.dll is missing from the layout."
}

# 2. Optionally sign the files inside before they are sealed into the package.
if ($SignContents -and $signtool) {
    $files = Get-ChildItem $layout -Recurse -Include '*.exe', '*.dll' | ForEach-Object FullName
    Write-Host "Signing $($files.Count) executables and libraries"
    # signtool takes many files at once; batch to stay under the command-line limit.
    for ($i = 0; $i -lt $files.Count; $i += 50) {
        $batch = $files[$i..([Math]::Min($i + 49, $files.Count - 1))]
        Invoke-Native $signtool ((Get-SignArguments) + $batch)
    }
}

# 3. Pack and sign the package.
$package = Join-Path $OutDir $pkg['PACKAGE_FILE']
Write-Host "Packing $package"
Invoke-Native $makeappx @('pack', '/d', $layout, '/p', $package, '/o')

if ($signtool) {
    Write-Host "Signing $package as $($pkg['PUBLISHER'])"
    Invoke-Native $signtool ((Get-SignArguments) + @($package))
} else {
    Write-Warning "$package is unsigned and will not install until it is signed."
}

$appinstaller = Join-Path $msixDir 'BayesTSStudio.appinstaller'
if (Test-Path $appinstaller) {
    Copy-Item $appinstaller $OutDir
    Write-Host "Publish both files in $OutDir to the update location together."
}
Write-Host "Done: $package"

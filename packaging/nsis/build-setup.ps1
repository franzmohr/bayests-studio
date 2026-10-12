<#
.SYNOPSIS
    Builds BayesTS Studio's setup.exe from a configured and built tree.

.DESCRIPTION
    Installs the build into <BuildDir>\nsis\layout with `cmake --install` -- the
    same layout the MSIX is packed from -- runs makensis on the script CMake
    configured, and optionally signs the result. The setup lands in -OutDir.

    makensis is looked for on PATH, in D:\NSIS and in Program Files; pass
    -Makensis to name it.

    Signing is optional but worth it: an unsigned setup draws a SmartScreen
    warning a student has to click through. One of
      -CertificateThumbprint  a certificate in Cert:\CurrentUser\My
      -PfxFile                a .pfx; its password in $env:STUDIO_PFX_PASSWORD
    and signtool from the Windows SDK or the NuGet package
    Microsoft.Windows.SDK.BuildTools (-SdkBinDir to name its directory).

.EXAMPLE
    packaging\nsis\build-setup.ps1 -BuildDir build\my-windows-release
#>
[CmdletBinding(DefaultParameterSetName = 'NoSign')]
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [string] $OutDir,
    [string] $Makensis,
    [string] $SdkBinDir,
    [Parameter(ParameterSetName = 'Thumbprint', Mandatory)] [string] $CertificateThumbprint,
    [Parameter(ParameterSetName = 'Pfx', Mandatory)] [string] $PfxFile,
    [string] $TimestampUrl = 'http://timestamp.digicert.com',
    # Build although EULA.txt is still the draft, for a setup that is tested
    # and not handed out.
    [switch] $DraftLicence
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$BuildDir = (Resolve-Path $BuildDir).Path
$setupDir = Join-Path $BuildDir 'nsis'
$envFile = Join-Path $setupDir 'package.env'
if (-not (Test-Path $envFile)) {
    throw "$envFile not found: configure $BuildDir with this version of the studio first."
}
$pkg = @{}
foreach ($line in Get-Content $envFile) {
    if ($line -match '^([A-Z_]+)=(.*)$') { $pkg[$Matches[1]] = $Matches[2] }
}
if (-not $OutDir) { $OutDir = Join-Path $setupDir 'out' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

function Invoke-Native([string] $exe, [string[]] $arguments) {
    # The exit code is the verdict: windeployqt and makensis write warnings
    # to stderr, which 'Stop' would otherwise treat as failures.
    $ErrorActionPreference = 'Continue'
    & $exe @arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path -Leaf $exe) failed with exit code $LASTEXITCODE" }
}

if (-not $Makensis) {
    $found = Get-Command makensis.exe -ErrorAction SilentlyContinue
    $candidates = @()
    if ($found) { $candidates += $found.Source }
    $candidates += 'D:\NSIS\makensis.exe', (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
                   (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
    $Makensis = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $Makensis) { throw 'makensis not found: install NSIS (https://nsis.sourceforge.io) or pass -Makensis.' }
}

# 0. The licence agreement the setup shows must be the final one: no
# [PLACEHOLDER] left to fill in -- matched by its opening, as one may run
# over a line break -- and no draft notice.
$eula = $pkg['EULA']
if (-not (Test-Path $eula)) { throw "The licence agreement $eula is missing." }
$open = @(Select-String -Path $eula -Pattern '\[[A-Z]{2,}|^DRAFT' -CaseSensitive)
if ($open.Count -gt 0) {
    $where = ($open | Select-Object -First 5 | ForEach-Object { "  line $($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
    if ($DraftLicence) {
        Write-Warning "EULA.txt is still a draft ($($open.Count) open item(s)); this setup must not be handed out."
    } else {
        throw "EULA.txt is still a draft -- complete it, or pass -DraftLicence for a test build:`n$where"
    }
}

# 1. The layout: exactly what `cmake --install` produces.
$layout = Join-Path $setupDir 'layout'
if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
Write-Host "Installing $BuildDir into $layout"
Invoke-Native 'cmake' @('--install', $BuildDir, '--prefix', $layout)
foreach ($exe in 'bin\bayests-studio.exe', 'bin\bayests.exe') {
    if (-not (Test-Path (Join-Path $layout $exe))) {
        throw "$exe is missing from the layout; the install step is incomplete."
    }
}
if (-not (Get-ChildItem $layout -Recurse -Filter 'qwindows.dll' | Select-Object -First 1)) {
    throw "Qt's platform plugin qwindows.dll is missing from the layout."
}

# 2. The setup. An update and the uninstaller remove what the layout puts in
# the install folder, folder by folder as the layout has them -- written here
# rather than in the script, so a folder the layout gains is never left behind.
$dirs = @(Get-ChildItem $layout -Directory | ForEach-Object Name)
if ($dirs -notcontains 'bin') { throw "The layout has no bin folder." }
$include = Join-Path $setupDir 'layout-dirs.nsh'
$lines = @('!macro RemoveLayout') + ($dirs | ForEach-Object { '  RMDir /r "$INSTDIR\' + $_ + '"' }) + @('!macroend')
Set-Content -Path $include -Value $lines -Encoding ASCII
$setup = Join-Path $OutDir $pkg['SETUP_FILE']
Write-Host "Building $setup (layout folders: $($dirs -join ', '))"
Invoke-Native $Makensis @('/V2', "/DLAYOUT=$layout", "/DLAYOUT_DIRS=$include", "/DOUTFILE=$setup",
                          (Join-Path $setupDir 'BayesTSStudio.nsi'))

# 3. Optionally, its signature.
if ($PSCmdlet.ParameterSetName -ne 'NoSign') {
    $signtool = $null
    $candidates = @()
    if ($SdkBinDir) { $candidates += Join-Path $SdkBinDir 'signtool.exe' }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $candidates += Get-ChildItem $kits -Directory | Where-Object { $_.Name -match '^\d+\.' } |
            Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' }
    }
    $nuget = Join-Path $env:USERPROFILE '.nuget\packages\microsoft.windows.sdk.buildtools'
    if (Test-Path $nuget) {
        $candidates += Get-ChildItem $nuget -Recurse -Filter 'signtool.exe' |
            Where-Object { $_.DirectoryName -match '\\x64$' } | Sort-Object FullName -Descending | ForEach-Object FullName
    }
    $signtool = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $signtool) { throw 'signtool.exe not found: install the Windows SDK or pass -SdkBinDir.' }
    $sign = @('sign', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256')
    if ($PSCmdlet.ParameterSetName -eq 'Thumbprint') {
        $sign += @('/sha1', $CertificateThumbprint, '/s', 'My')
    } else {
        $sign += @('/f', (Resolve-Path $PfxFile).Path)
        if ($env:STUDIO_PFX_PASSWORD) { $sign += @('/p', $env:STUDIO_PFX_PASSWORD) }
    }
    Write-Host "Signing $setup"
    Invoke-Native $signtool ($sign + @($setup))
} else {
    Write-Warning "$setup is unsigned: Windows SmartScreen will warn whoever runs it."
}
Write-Host "Done: $setup"

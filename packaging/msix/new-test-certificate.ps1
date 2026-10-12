# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

<#
.SYNOPSIS
    Creates a self-signed code-signing certificate for test packages.

.DESCRIPTION
    For trying the MSIX on your own machine only: a package signed with it
    installs only where this certificate is trusted. Its subject must equal
    the manifest's Publisher (STUDIO_MSIX_PUBLISHER), which is the default
    here. The certificate goes into Cert:\CurrentUser\My, and its public
    half is exported to -CerFile.

    To trust it, import the .cer into "Trusted People" of the local machine,
    which needs administrator rights once:

        Import-Certificate -FilePath <CerFile> -CertStoreLocation Cert:\LocalMachine\TrustedPeople

    Packages for anyone else are signed with a real certificate instead.
#>
param(
    [string] $Subject = 'CN=Franz X. Mohr',
    [string] $CerFile = (Join-Path $PWD 'BayesTSStudio-test.cer')
)
$ErrorActionPreference = 'Stop'

$cert = New-SelfSignedCertificate `
    -Type Custom `
    -Subject $Subject `
    -FriendlyName 'BayesTS Studio test packages' `
    -KeyUsage DigitalSignature `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -NotAfter (Get-Date).AddYears(2) `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')

Export-Certificate -Cert $cert -FilePath $CerFile | Out-Null
Write-Host "Thumbprint: $($cert.Thumbprint)"
Write-Host "Public certificate: $CerFile"

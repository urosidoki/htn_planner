param([string]$Version)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Variants.ps1"
if (-not $Version) { $Version = (Get-Content "$repo/VERSION" -Raw).Trim() }
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio MSBuild was not found' }
Push-Location $repo
try {
    & "$repo/ThirdParty/premake/bin/premake5.exe" --sdk vs2022
    if ($LASTEXITCODE) { throw 'Premake failed' }
    foreach ($variant in $HTNVariants) {
        & $msbuild "$repo/HTNSDK.sln" /m:3 /t:Build "/p:Configuration=$($variant.id)" /p:Platform=x64 /v:minimal /nologo
        if ($LASTEXITCODE) { throw "SDK build failed: $($variant.id)" }
    }
    & "$PSScriptRoot/PackageSDK.ps1" -Version $Version
    $archive = "$repo/dist/HTNSDK-$Version-windows-x86_64.zip"
    $validation = Join-Path ([IO.Path]::GetTempPath()) ('htn-sdk-extracted-' + [guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $archive -DestinationPath $validation
    & "$validation/HTNSDK-$Version-windows-x86_64/ValidatePackage.ps1"
} finally { Pop-Location }

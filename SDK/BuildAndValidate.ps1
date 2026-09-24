param([string]$Version)
$ErrorActionPreference = 'Stop'
function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

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
    & "$PSScriptRoot/ValidateSourceBoundary.ps1"
    & "$PSScriptRoot/AuditRuntimeBridge.ps1"
    foreach ($variant in $HTNVariants) {
        & $msbuild "$repo/HTNSDK.sln" /m:3 /t:Rebuild "/p:Configuration=$($variant.id)" /p:Platform=x64 /v:minimal /nologo
        if ($LASTEXITCODE) { throw "SDK build failed: $($variant.id)" }
    }
    $artifacts = @(Get-ChildItem "$repo/bin/sdk" -Recurse -File | Where-Object {
        $_.Extension -in @('.lib', '.dll', '.exe', '.pdb')
    } | ForEach-Object {
        @{path=$_.FullName.Substring($repo.Length + 1).Replace('\','/'); sha256=(Get-Sha256 $_.FullName)}
    })
    @{version=$Version; build_id=[guid]::NewGuid().ToString('N'); rebuilt=$true; artifacts=$artifacts} |
        ConvertTo-Json -Depth 5 | Set-Content "$repo/build/sdk/build-receipt.json" -Encoding UTF8
    & "$PSScriptRoot/PackageSDK.ps1" -Version $Version
    $archive = "$repo/dist/HTNSDK-$Version-windows-x86_64.zip"
    $validation = Join-Path ([IO.Path]::GetTempPath()) ('htn-sdk-extracted-' + [guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $archive -DestinationPath $validation
    & "$validation/HTNSDK-$Version-windows-x86_64/ValidatePackage.ps1"
} finally { Pop-Location }

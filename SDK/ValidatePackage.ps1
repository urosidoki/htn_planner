param([string]$BuildRoot)
$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('htn-sdk-validation-' + [guid]::NewGuid().ToString('N')) }
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
if (Test-Path -LiteralPath $BuildRoot) { throw 'Validation requires a new build directory; refusing cached builds' }
$manifest = Get-Content "$PSScriptRoot/manifest.json" -Raw | ConvertFrom-Json
if (!$manifest.build_id -or !$manifest.sdk_version -or $manifest.variants.Count -ne 8) {
    throw 'Incomplete release manifest'
}
$provenance = Get-Content "$PSScriptRoot/build-provenance.json" -Raw | ConvertFrom-Json
if ($provenance.build_id -ne $manifest.build_id -or $provenance.version -ne $manifest.sdk_version -or !$provenance.rebuilt) {
    throw 'Package/build provenance mismatch'
}
function Verify-BuildArtifact([string]$Source, [string]$Packaged) {
    $entry = @($provenance.artifacts | Where-Object { $_.path -eq $Source })
    if ($entry.Count -ne 1 -or (Get-Sha256 "$PSScriptRoot/$Packaged") -ne $entry[0].sha256) {
        throw "Packaged binary does not belong to recorded build: $Packaged"
    }
}
Verify-BuildArtifact 'bin/sdk/StaticReleasePlain-windows-x86_64/HTNTranslator/HTNTranslator.exe' 'bin/windows-x86_64/tools/HTNTranslator.exe'
$checked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in Get-Content "$PSScriptRoot/CHECKSUMS.sha256") {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid checksum entry' }
    $expected = $Matches[1]; $relative = $Matches[2]
    $path = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $relative))
    if (!$path.StartsWith([IO.Path]::GetFullPath($PSScriptRoot) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or !$checked.Add($path)) {
        throw "Invalid or duplicate checksum path: $relative"
    }
    if ((Get-Sha256 $path) -ne $expected) { throw "Checksum mismatch: $relative" }
}
foreach ($file in Get-ChildItem $PSScriptRoot -Recurse -File) {
    if ($file.FullName -ne "$PSScriptRoot\CHECKSUMS.sha256" -and !$checked.Contains($file.FullName)) {
        throw "Unverified package file: $($file.FullName)"
    }
}
foreach ($variant in $manifest.variants) {
    $id = $variant.id
    Verify-BuildArtifact "bin/sdk/$id-windows-x86_64/HTNRuntimeBridge/HTNRuntimeBridge.dll" "bin/windows-x86_64/$id/HTNRuntimeBridge.dll"
    Verify-BuildArtifact "bin/sdk/$id-windows-x86_64/HTNRuntimeBridge/HTNRuntimeBridge.lib" "lib/windows-x86_64/$id/HTNRuntimeBridge.lib"
    $build = Join-Path $BuildRoot $id
    & cmake -S "$PSScriptRoot/examples" -B $build -G 'Visual Studio 17 2022' -A x64 -T v143 "-DHTN_DIR=$PSScriptRoot/cmake" '-DCMAKE_CONFIGURATION_TYPES=Validation' "-DHTN_VARIANT_VALIDATION=$id"
    if ($LASTEXITCODE) { throw "Configure failed: $id" }
    & cmake --build $build --config Validation --parallel 3
    if ($LASTEXITCODE) { throw "Build failed: $id" }
    & ctest --test-dir $build -C Validation --output-on-failure
    if ($LASTEXITCODE) { throw "Consumer validation failed: $id" }
}
$negativeBuild = Join-Path $BuildRoot 'negative'
& cmake -S "$PSScriptRoot/examples" -B $negativeBuild -G 'Visual Studio 17 2022' -A x64 -T v143 "-DHTN_DIR=$PSScriptRoot/cmake" '-DCMAKE_CONFIGURATION_TYPES=Validation' '-DHTN_VARIANT_VALIDATION=StaticDebugPlain' '-DHTN_VALIDATE_NEGATIVE_CASES=ON'
if ($LASTEXITCODE) { throw 'Negative-test configuration failed' }
$negativeLog = Join-Path $BuildRoot 'runtime-mismatch.log'
& cmake --build $negativeBuild --config Validation --target RuntimeMismatch *> $negativeLog
if ($LASTEXITCODE -eq 0 -or -not (Select-String -LiteralPath $negativeLog -Pattern 'LNK2038.*RuntimeLibrary' -Quiet)) { throw "Runtime mismatch was not rejected as expected: $negativeLog" }
$unknownLog = Join-Path $BuildRoot 'unknown-variant.log'
$ErrorActionPreference = 'Continue' # Windows PowerShell represents native stderr as error records.
& cmake -S "$PSScriptRoot/examples" -B $negativeBuild '-DHTN_VARIANT_VALIDATION=UnknownVariant' *> $unknownLog
$unknownExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($unknownExitCode -eq 0 -or -not (Select-String -LiteralPath $unknownLog -Pattern 'Unknown HTN variant' -Quiet)) { throw 'Unknown variant was not rejected' }
Write-Host "PASS: all eight SDK variants, 24 consumer executions and 8 object/export checks. Build outputs: $BuildRoot"
Write-Host 'PASS: incompatible CRT and unknown variant rejected.'

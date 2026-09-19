param([string]$BuildRoot)
$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('htn-sdk-validation-' + [guid]::NewGuid().ToString('N')) }
$manifest = Get-Content "$PSScriptRoot/manifest.json" -Raw | ConvertFrom-Json
foreach ($line in Get-Content "$PSScriptRoot/CHECKSUMS.sha256") {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid checksum entry' }
    $expected = $Matches[1]; $relative = $Matches[2]
    if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $relative) -Algorithm SHA256).Hash -ne $expected) { throw "Checksum mismatch: $relative" }
}
foreach ($variant in $manifest.variants) {
    $id = $variant.id
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
Write-Host "PASS: all eight SDK variants, 24 consumer executions. Build outputs: $BuildRoot"
Write-Host 'PASS: incompatible CRT and unknown variant rejected.'

param([string]$Version, [switch]$Force)
$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Variants.ps1"
if (-not $Version) { $Version = (Get-Content "$RepositoryRoot/VERSION" -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') { throw 'Invalid SDK version' }
$Platform = 'windows-x86_64'
$DistRoot = [IO.Path]::GetFullPath("$RepositoryRoot/dist")
$OutputDirectory = Join-Path $DistRoot "HTNSDK-$Version-$Platform"
$ArchivePath = "$OutputDirectory.zip"
function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing SDK file: $Path" }
}
function Copy-PackageFile([string]$Source, [string]$Relative) {
    Require-File $Source
    $destination = Join-Path $OutputDirectory $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination
}
function Read-Abi([string]$Header, [string]$Macro, [bool]$Instrumented) {
    $values = [regex]::Matches((Get-Content -LiteralPath $Header -Raw), "#define\s+$Macro\s+UINT32_C\((0x[0-9A-Fa-f]+)\)")
    if ($values.Count -ne 4) { throw "Unexpected ABI definitions: $Header" }
    if ($Instrumented) { return $values[1].Groups[1].Value }
    return $values[3].Groups[1].Value
}
# Check every artifact and the generated configuration before replacing any package.
foreach ($variant in $HTNVariants) {
    $id = $variant.id
    foreach ($component in @('HTNFramework','HTNIntegration','HTNRuntimeBridge')) {
        $root = "$RepositoryRoot/bin/sdk/$id-$Platform/$component"
        Require-File "$root/$component.lib"
        Require-File "$root/$component.pdb"
        if ($component -eq 'HTNRuntimeBridge') { Require-File "$root/$component.dll" }
        [xml]$project = Get-Content "$RepositoryRoot/build/sdk/$component/$component.vcxproj"
        $groups = @($project.Project.ItemDefinitionGroup | Where-Object { $_.Condition -match "'$id\|x64'" })
        if ($groups.Count -ne 1) { throw "Missing project configuration $id/$component" }
        $compile = $groups[0].ClCompile
        if ($compile.RuntimeLibrary -ne $variant.msvc_runtime_library) { throw "Wrong runtime in $id/$component" }
        $definitions = @($compile.PreprocessorDefinitions -split ';')
        foreach ($define in $variant.defines) {
            if ($define -notin $definitions) { throw "Missing $define in $id/$component" }
        }
        if ((('HTN_DEBUG_DECOMPOSITION' -in $definitions) -ne $variant.instrumentation) -or
            ('HTN_GENERATED_EXECUTION_PROFILING' -in $definitions) -or
            ('HTN_MEMORY_ATOM_DIAGNOSTICS' -in $definitions)) { throw "Unsupported ABI options in $id/$component" }
    }
    $variant.generated_planner_abi = Read-Abi "$RepositoryRoot/HTNFramework/src/Translator/HTNGeneratedPlanner.h" 'HTN_GENERATED_PLANNER_ABI_VERSION' $variant.instrumentation
    $variant.runtime_bridge_abi = Read-Abi "$RepositoryRoot/HTNFramework/src/Translator/HTNRuntimeBridge.h" 'HTN_RUNTIME_BRIDGE_ABI_VERSION' $variant.instrumentation
}
$toolRoot = "$RepositoryRoot/bin/sdk/StaticReleasePlain-$Platform/HTNTranslator"
Require-File "$toolRoot/HTNTranslator.exe"
foreach ($path in @($OutputDirectory, $ArchivePath)) {
    $resolved = [IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith($DistRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Package output escapes dist' }
    if (Test-Path -LiteralPath $resolved) {
        if (-not $Force) { throw "Package already exists: $resolved. Use -Force explicitly to replace it." }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
foreach ($component in @('HTNFramework','HTNIntegration')) {
    $source = [IO.Path]::GetFullPath("$RepositoryRoot/$component/src")
    Get-ChildItem -LiteralPath $source -Recurse -File -Filter *.h | Where-Object {
        if ($_.Name -eq 'pch.h') { return $false }
        $relative = $_.FullName.Substring($source.Length).TrimStart('\','/').Replace('\','/')
        return $true
    } | ForEach-Object {
        $relative = $_.FullName.Substring($source.Length).TrimStart('\','/')
        Copy-PackageFile $_.FullName "include/$component/$relative"
    }
}

$forbiddenReferences = Get-ChildItem "$OutputDirectory/include" -Recurse -File | Select-String -SimpleMatch @(
    'HTNNodeVisitorContextBase')
if ($forbiddenReferences) {
    throw "Generated SDK headers reference legacy node APIs: $($forbiddenReferences[0].Path):$($forbiddenReferences[0].LineNumber)"
}
foreach ($variant in $HTNVariants) {
    $id = $variant.id
    foreach ($component in @('HTNFramework','HTNIntegration','HTNRuntimeBridge')) {
        $root = "$RepositoryRoot/bin/sdk/$id-$Platform/$component"
        Copy-PackageFile "$root/$component.lib" "lib/$Platform/$id/$component.lib"
        if ($component -eq 'HTNRuntimeBridge') {
            Copy-PackageFile "$root/$component.dll" "bin/$Platform/$id/$component.dll"
            Copy-PackageFile "$root/$component.pdb" "bin/$Platform/$id/$component.pdb"
        } else { Copy-PackageFile "$root/$component.pdb" "lib/$Platform/$id/$component.pdb" }
    }
}
Copy-PackageFile "$toolRoot/HTNTranslator.exe" "bin/$Platform/tools/HTNTranslator.exe"
Copy-PackageFile "$toolRoot/HTNTranslator.pdb" "bin/$Platform/tools/HTNTranslator.pdb"
Copy-PackageFile "$RepositoryRoot/LICENSE" 'LICENSE'
Copy-PackageFile "$RepositoryRoot/NOTICE.md" 'NOTICE.md'
Copy-PackageFile "$RepositoryRoot/ThirdParty/optick/LICENSE" 'THIRD_PARTY_NOTICES/Optick-LICENSE.txt'
Copy-PackageFile "$PSScriptRoot/PackageREADME.md" 'README.md'
Copy-PackageFile "$PSScriptRoot/HTNConfig.cmake" 'cmake/HTNConfig.cmake'
Copy-PackageFile "$PSScriptRoot/ValidatePackage.cmd" 'ValidatePackage.cmd'
Copy-PackageFile "$PSScriptRoot/ValidatePackage.ps1" 'ValidatePackage.ps1'
Copy-PackageFile "$RepositoryRoot/docs/SDK_VARIANTS.md" 'docs/SDK_VARIANTS.md'
Get-ChildItem "$PSScriptRoot/Examples" -Recurse -File | Where-Object Extension -in @('.cpp','.domain','.txt','.cmake') | ForEach-Object {
    $relative = $_.FullName.Substring(("$PSScriptRoot/Examples").Length).TrimStart('\','/')
    Copy-PackageFile $_.FullName "examples/$relative"
}
$manifest = [ordered]@{
    schema_version = 2; sdk_version = $Version; platform = $Platform; architecture = 'x86_64'
    compiler = 'MSVC'; toolset = 'v143'; cpp_standard = 'C++20'; c_standard = 'C11'
    variants = $HTNVariants
    components = @(
        @{name='HTNFramework';kind='static-library';required=$true},
        @{name='HTNIntegration';kind='static-library';required=$false},
        @{name='HTNTranslator';kind='executable';required=$true},
        @{name='HTNRuntimeBridge';kind='dynamic-bridge';required=$false})
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content "$OutputDirectory/manifest.json" -Encoding UTF8
Get-ChildItem $OutputDirectory -Recurse -File | Where-Object Name -ne 'CHECKSUMS.sha256' | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($OutputDirectory.Length).TrimStart('\','/').Replace('\','/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
} | Set-Content "$OutputDirectory/CHECKSUMS.sha256" -Encoding ASCII
Compress-Archive -LiteralPath $OutputDirectory -DestinationPath $ArchivePath -CompressionLevel Optimal
Write-Host "SDK package: $ArchivePath"

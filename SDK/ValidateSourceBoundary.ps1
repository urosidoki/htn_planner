# Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.
param([string]$ProjectPath = "$PSScriptRoot/../build/sdk/HTNFramework/HTNFramework.vcxproj")
$ErrorActionPreference = 'Stop'
[xml]$project = Get-Content -LiteralPath $ProjectPath -Raw
foreach ($node in $project.SelectNodes("//*[local-name()='ClCompile' and @Include]")) {
    $source = $node.GetAttribute('Include').Replace('\', '/')
    if ($source -match '(?i)(^|/)Domain/(Interpreter|Nodes|Parser|Loader|Semantic|Tooling)/' -or
        $source -match '(?i)(^|/)Domain/HTNDomainHelpers\.cpp$') {
        throw "Isolated SDK compiles forbidden frontend source: $source"
    }
}
Write-Host 'PASS: isolated SDK source boundary'

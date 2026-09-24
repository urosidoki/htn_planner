# Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.
param([string]$ObjectRoot, [string]$Bridge, [string]$ImportLibrary, [string]$Dumpbin)
$ErrorActionPreference = 'Stop'
function Inspect([string]$Option, [string]$Path) {
    $result = & $Dumpbin $Option $Path 2>&1
    if ($LASTEXITCODE) { throw "dumpbin failed: $Option $Path" }
    return $result -join "`n"
}
$exports = Inspect '/exports' $Bridge
$imports = Inspect '/linkermember:1' $ImportLibrary
$required = [Collections.Generic.HashSet[string]]::new()
$numeric = [Collections.Generic.HashSet[string]]::new()
$objects = @(Get-ChildItem -LiteralPath $ObjectRoot -Recurse -Filter *.obj)
if (!$objects.Count) { throw 'No compiled domain objects found' }
foreach ($object in $objects) {
    $symbols = Inspect '/symbols' $object.FullName
    foreach ($match in [regex]::Matches($symbols, 'UNDEF[^\r\n]*External\s+\|\s+(?:__imp_)?((?:HTN|Htn)[A-Za-z0-9_]+)\b')) {
        $name = $match.Groups[1].Value
        [void]$required.Add($name)
        if ($object.Name -match 'coverage\.generated') { [void]$numeric.Add($name) }
    }
}
if (!$required.Count) { throw 'No external HTN symbols found; symbol validation did not run' }
foreach ($name in @('HTNAtom_SetInt', 'HTNAtom_SetFloat')) {
    if (!$numeric.Contains($name)) { throw "Numeric regression fixture no longer references $name" }
}
foreach ($name in $required) {
    $pattern = '(?m)\b' + [regex]::Escape($name) + '\s*(?:\r?$|=)'
    if ($exports -notmatch $pattern -or $imports -notmatch $pattern) {
        throw "Domain object requires $name, missing from bridge DLL or import library"
    }
}
Write-Host "PASS: $($required.Count) external HTN symbols available in bridge DLL and import library"
Write-Host (($required | Sort-Object) -join ', ')

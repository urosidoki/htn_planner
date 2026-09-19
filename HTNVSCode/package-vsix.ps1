param(
    [ValidateSet('Debug','Profile','ProfileDetailed','Release')]
    [string]$Configuration = 'Release',
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$ExtensionDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ExtensionDir
$ServerDir = Join-Path $ExtensionDir 'server\win32-x64'
$ServerExe = Join-Path $RepoRoot "bin\$Configuration-windows-x86_64\HTNLanguageServer\HTNLanguageServer.exe"

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($installationPath) {
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'MSBuild.exe was not found. Install Visual Studio with the Desktop development with C++ workload.'
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This packaging script currently builds the win32-x64 VSIX and must be run on Windows.'
}

$msbuild = Find-MSBuild
Write-Host "Building HTNLanguageServer ($Configuration x64)..."
& $msbuild (Join-Path $RepoRoot 'HTN.sln') /m /t:HTNLanguageServer /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'HTNLanguageServer build failed.' }
if (-not (Test-Path $ServerExe)) { throw "Expected server executable was not produced: $ServerExe" }

New-Item -ItemType Directory -Force -Path $ServerDir | Out-Null
Copy-Item $ServerExe (Join-Path $ServerDir 'HTNLanguageServer.exe') -Force
$BuiltServerDir = Split-Path -Parent $ServerExe
Get-ChildItem $BuiltServerDir -Filter '*.dll' -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $ServerDir $_.Name) -Force
}

Push-Location $ExtensionDir
try {
    Write-Host 'Installing extension dependencies...'
    npm ci
    if ($LASTEXITCODE -ne 0) { throw 'npm ci failed.' }

    if (-not $Output) {
        $pkg = Get-Content (Join-Path $ExtensionDir 'package.json') -Raw | ConvertFrom-Json
        $Output = Join-Path $ExtensionDir "$($pkg.name)-$($pkg.version)-win32-x64.vsix"
    }

    Write-Host "Packaging VSIX -> $Output"
    npx @vscode/vsce package --target win32-x64 --out $Output
    if ($LASTEXITCODE -ne 0) { throw 'vsce package failed.' }

    Write-Host ''
    Write-Host 'Done.' -ForegroundColor Green
    Write-Host "Install with: code --install-extension `"$Output`" --force"
}
finally {
    Pop-Location
}

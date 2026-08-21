param(
    [switch]$SkipGitUpdate,
    [string]$Config = 'Release',
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

# PowerShell treats an unrecognised --option as a positional argument.  Keep
# the existing PowerShell-style parameters, but normalize the requested
# double-dash CLI spelling before doing any work.
$rawArguments = @()
if ($Config -match '^-') {
    $rawArguments += $Config
    $Config = 'Release'
}
$rawArguments += @($args)

$helpRequested = $Help
for ($index = 0; $index -lt $rawArguments.Count; $index++) {
    $argument = [string]$rawArguments[$index]

    if ($argument -match '^--?help$') {
        $helpRequested = $true
        continue
    }

    if ($argument -match '^--?skip-git-update$') {
        $SkipGitUpdate = $true
        continue
    }

    if ($argument -match '^--?config=(.+)$') {
        $Config = $Matches[1]
        continue
    }

    if ($argument -match '^--?config$') {
        if ($index + 1 -ge $rawArguments.Count) {
            throw 'The --config option requires one of: Debug, Release, RelWithDebInfo, MinSizeRel.'
        }

        $index++
        $Config = [string]$rawArguments[$index]
        continue
    }

    throw "Unknown argument '$argument'. Use --help for usage."
}

function Show-Help {
    @'
Usage:
  .\build.ps1 [--config <configuration>] [-SkipGitUpdate]
  .\build.ps1 --help

Builds reuse the dedicated Win32 C++20 tree in build_C20.
The native build profile is MakeFile_C20.cmake.

Options:
  --config <configuration>  Build one of the Visual Studio configurations below.
                            Default: Release.
  --help                    Show this help text and exit without changing anything.
  -SkipGitUpdate            Skip the upstream pull and origin push step.
  --skip-git-update         Double-dash spelling of -SkipGitUpdate.

Configurations:
  Debug                     Unoptimized build for source-level debugging.
  Release                   Optimized build; PDBs stay in the build directory and
                            matching PDBs are removed from the deployed WoW client.
  RelWithDebInfo            Optimized build with debug information; created PDBs
                            are copied to the deployed WoW client.
  MinSizeRel                Size-optimized build; created PDBs are copied to the
                            deployed WoW client.
'@
}

if ($helpRequested) {
    Show-Help
    exit 0
}

$validConfigurations = @('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')
$normalizedConfig = $validConfigurations |
    Where-Object { $_ -ieq $Config } |
    Select-Object -First 1

if (!$normalizedConfig) {
    throw "Invalid configuration '$Config'. Valid configurations: $($validConfigurations -join ', ')."
}
$Config = [string]$normalizedConfig

Set-Location $PSScriptRoot

$wowClient = 'C:\ProgramData\WOW\WOWClient'
Set-Variable -Name BuildDirectory -Value (Join-Path $PSScriptRoot 'build_C20') -Option Constant
Set-Variable -Name LegacyBuildDirectory -Value (Join-Path $PSScriptRoot 'build') -Option Constant
$output    = Join-Path $BuildDirectory $Config
$launcherPdb = Join-Path $output 'wow_optimize_launcher.pdb'
$vswhere   = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cmakeProfile = Join-Path $PSScriptRoot 'MakeFile_C20.cmake'

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments
    if ($LASTEXITCODE) {
        throw "$Command failed with exit code $LASTEXITCODE."
    }
}

if (!$SkipGitUpdate) {
    Invoke-Checked git @('pull', '--no-rebase', '--autostash', '--no-edit', 'upstream', 'main')

    $unmergedFiles = @(& git diff --name-only --diff-filter=U)
    if ($LASTEXITCODE) {
        throw "git diff failed with exit code $LASTEXITCODE."
    }
    if ($unmergedFiles.Count -gt 0) {
        throw "Autostash reapplied with conflicts. Resolve them before pushing: $($unmergedFiles -join ', ')"
    }

    Invoke-Checked git @('push', 'origin', 'main')
}

Get-Command cmake.exe -ErrorAction Stop | Out-Null

$vs = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (!$vs) { throw 'Visual Studio 2026 C++ installation not found.' }

$csc  = Join-Path $vs 'MSBuild\Current\Bin\Roslyn\csc.exe'
$refs = "${env:ProgramFiles(x86)}\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8"

if (!(Test-Path $csc))                  { throw "Roslyn compiler not found: $csc" }
if (!(Test-Path "$refs\mscorlib.dll")) { throw '.NET Framework 4.8 targeting pack not found.' }
if (!(Test-Path $cmakeProfile))         { throw "C++20 CMake profile not found: $cmakeProfile" }

# FetchContent keeps nested caches with absolute paths. Reuse the dedicated
# C++20 tree unless a cache proves that the checkout moved.
$staleCache = Get-ChildItem -LiteralPath $BuildDirectory -Filter CMakeCache.txt -Recurse -ErrorAction SilentlyContinue |
    Where-Object {
        $cacheDirectory = Select-String -LiteralPath $_.FullName `
            -Pattern '^CMAKE_CACHEFILE_DIR:INTERNAL=(.+)$' |
            Select-Object -First 1 -ExpandProperty Matches |
            ForEach-Object { $_.Groups[1].Value }

        $expectedDirectory = Split-Path $_.FullName -Parent
        $cacheDirectory -and
            ($cacheDirectory.Replace('/', '\') -ine $expectedDirectory.Replace('/', '\'))
    } |
    Select-Object -First 1

if ($staleCache) {
    Write-Host 'Checkout moved; removing stale generated C++20 build state.' -ForegroundColor Yellow
    if (Test-Path -LiteralPath $LegacyBuildDirectory) {
        $legacyBuildItem = Get-Item -LiteralPath $LegacyBuildDirectory -Force
        if ($legacyBuildItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            Remove-Item -LiteralPath $LegacyBuildDirectory -Force
        }
    }
    Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

# One upstream source file includes MinHook through ../../build/_deps. Keep that
# generated compatibility path without moving C++20 output out of build_C20.
if (Test-Path -LiteralPath $LegacyBuildDirectory) {
    $legacyBuildItem = Get-Item -LiteralPath $LegacyBuildDirectory -Force
    $legacyTarget = @($legacyBuildItem.Target) | Select-Object -First 1
    $expectedTarget = [System.IO.Path]::GetFullPath($BuildDirectory)
    $actualTarget = if ($legacyTarget) { [System.IO.Path]::GetFullPath($legacyTarget) } else { '' }

    if (!($legacyBuildItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -or
        $actualTarget -ine $expectedTarget) {
        throw "The compatibility path '$LegacyBuildDirectory' exists and does not target '$BuildDirectory'."
    }
}
else {
    New-Item -ItemType Junction -Path $LegacyBuildDirectory -Target $BuildDirectory | Out-Null
}

Write-Host "[1/4] Configuring x86 $Config build..."
Invoke-Checked cmake.exe @(
    '-C', $cmakeProfile
    '-S', $PSScriptRoot
    '-B', $BuildDirectory
    '-G', 'Visual Studio 18 2026'
    '-A', 'Win32'
    "-DCMAKE_GENERATOR_INSTANCE=$vs"
    "-DCMAKE_BUILD_TYPE=$Config"
    '-DMI_USE_CXX=ON'
)

Write-Host "[2/4] Building $Config..."
Invoke-Checked cmake.exe @(
    '--build', $BuildDirectory
    '--config', $Config
    '--'
    '/p:LanguageStandard=stdcpp20'
    '/p:UseMultiToolTask=true'
)

New-Item $output -ItemType Directory -Force | Out-Null

$launcher   = Join-Path $PSScriptRoot 'src\launcher\Launcher.cs'
$background = Join-Path $PSScriptRoot 'src\launcher\wotlk_background.jpg'
$launcherEXE = Join-Path $output 'wow_optimize_launcher.exe'

Write-Host "[3/4] Compiling $Config launcher..."
Invoke-Checked $csc @(
    '/noconfig'
    '/langversion:latest'
    '/nowarn:1701,1702'
    '/nostdlib+'
    '/errorreport:prompt'
    '/warn:4'
    '/define:TRACE'
    '/debug:full'
    "/pdb:$launcherPdb"
    "/reference:$refs\mscorlib.dll"
    "/reference:$refs\System.Core.dll"
    "/reference:$refs\System.dll"
    "/reference:$refs\System.Drawing.dll"
    "/reference:$refs\System.Windows.Forms.dll"
    "/resource:$background,wotlk_background.jpg"
    '/target:winexe'
    "/out:$launcherEXE"
    $launcher
)

Copy-Item $background $output -Force

Write-Host "[4/4] Copying to $wowClient..."
New-Item $wowClient -ItemType Directory -Force | Out-Null

$files = @(
    "$output\wow_optimize.dll"
    "$output\version.dll"
    $launcherEXE
    "$output\wotlk_background.jpg"
)

$pdbFiles = @(
    "$output\wow_optimize.pdb"
    "$output\version.pdb"
    $launcherPdb
)

if ($Config -eq 'Release') {
    # Release artifacts remain available under build\Release for local symbol
    # loading, but shipped client directories should not retain project PDBs.
    foreach ($pdbFile in $pdbFiles) {
        $clientPdb = Join-Path $wowClient (Split-Path $pdbFile -Leaf)
        if (Test-Path -LiteralPath $clientPdb -PathType Leaf) {
            Remove-Item -LiteralPath $clientPdb -Force
            Write-Host "  Removed deployed PDB: $clientPdb"
        }
    }
} else {
    # Do not make non-Release deployment depend on every compiler producing a
    # PDB; copy only symbols that actually exist for this configuration.
    $createdPdbs = $pdbFiles |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
    $files += @($createdPdbs)
}

Copy-Item -Path $files -Destination $wowClient -Force

Write-Host 'Build complete:' -ForegroundColor Green
$files | ForEach-Object { Write-Host "  $_" }

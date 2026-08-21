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

Options:
  --config <configuration>  Build one of the Visual Studio configurations below.
                            Default: Release.
  --help                    Show this help text and exit without changing anything.
  -SkipGitUpdate            Skip the upstream fetch/merge/push step.
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
$build     = Join-Path $PSScriptRoot 'build'
$output    = Join-Path $build $Config
$launcherPdb = Join-Path $output 'wow_optimize_launcher.pdb'
$vswhere   = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments
    if ($LASTEXITCODE) {
        throw "$Command failed with exit code $LASTEXITCODE."
    }
}

if (!$SkipGitUpdate) {
    $protectedFiles = @('build.ps1', 'wow-optimize.code-workspace')
    $protectedFileStashed = $false

    $trackedChanges = @(& git status --porcelain=v1 --untracked-files=no -- . `
        ':(exclude)build.ps1' ':(exclude)wow-optimize.code-workspace')
    if ($LASTEXITCODE) {
        throw "git status failed with exit code $LASTEXITCODE."
    }

    $trackedChanges = @($trackedChanges | Where-Object { $_ })
    if ($trackedChanges.Count -gt 0) {
        Write-Host 'Upstream update stopped: tracked files have local changes.' -ForegroundColor Yellow
        $trackedChanges | ForEach-Object { Write-Host "  $_" }
        Write-Host 'Commit, stash, or restore those files, then run the build again.'
        Write-Host 'To build the current checkout without Git updates, use --skip-git-update.'
        exit 1
    }

    Invoke-Checked git @('fetch', 'upstream')

    # Keep local customizations to these files out of the merge so they are not overwritten.
    & git diff --quiet -- @protectedFiles
    if ($LASTEXITCODE -ne 0) {
        $stashMessage = "auto-stash protected files $(Get-Date -Format o)"
        Invoke-Checked git (@('stash', 'push', '--message', $stashMessage, '--') + $protectedFiles)
        $protectedFileStashed = $true
    }

    try {
        & git merge --no-edit --message 'Upstream Merge' upstream/main
        if ($LASTEXITCODE) {
            $mergeExitCode = $LASTEXITCODE
            Write-Host "Upstream merge failed (exit code $mergeExitCode); aborting it." -ForegroundColor Yellow

            & git rev-parse --verify --quiet MERGE_HEAD *> $null
            if ($LASTEXITCODE -eq 0) {
                Invoke-Checked git @('merge', '--abort')
            }

            throw "git merge failed with exit code $mergeExitCode."
        }

        Invoke-Checked git @('push', 'origin', 'main')
    }
    finally {
        if ($protectedFileStashed) {
            & git stash pop
            if ($LASTEXITCODE) {
                throw 'Failed to restore local protected files from stash. Resolve conflicts, then run git stash list to verify stash state.'
            }
        }
    }
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

# CMake's --fresh option resets only the top-level cache. FetchContent keeps
# nested caches under build\_deps, and those contain absolute paths that become
# invalid when this checkout is moved.
$staleCache = Get-ChildItem -LiteralPath $build -Filter CMakeCache.txt -Recurse -ErrorAction SilentlyContinue |
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
    Write-Host "Checkout moved; removing stale generated CMake build state." -ForegroundColor Yellow
    Remove-Item -LiteralPath $build -Recurse -Force
}

Write-Host "[1/4] Configuring x86 $Config build..."
Invoke-Checked cmake.exe @(
    '--fresh'
    '-S', $PSScriptRoot
    '-B', $build
    '-G', 'Visual Studio 18 2026'
    '-A', 'Win32'
    "-DCMAKE_GENERATOR_INSTANCE=$vs"
    "-DCMAKE_BUILD_TYPE=$Config"
    '-DMI_USE_CXX=ON'
)

Write-Host "[2/4] Building $Config..."
Invoke-Checked cmake.exe @(
    '--build', $build
    '--config', $Config
    '--'
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

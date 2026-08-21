# PowerShell build automation

Applies to `build.ps1`.

## Contract

- Preserve both PowerShell-style parameters and the documented short double-dash spellings.
- `--help` must exit before Git, CMake, compilation, deployment, or file changes.
- `--skip-git-update` must bypass fetch, merge, and push. Without it, the script fetches `upstream`, merges `upstream/main`, and pushes `origin/main`.
- Check every external command exit code. A failed merge must abort before push, build, or deployment.
- Preserve protected-file stashing and restoration. Never leave a successful stash hidden after the build flow exits.
- Stale-cache cleanup can remove only generated `build` state after it proves that a nested `CMakeCache.txt` belongs to another checkout path.
- Deployment targets `C:\ProgramData\WOW\WOWClient`. Release removes deployed project PDBs; other configurations copy PDBs that exist.

## Verification

```powershell
$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path '.\build.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { $errors; exit 1 }

.\build.ps1 --help
git diff --check
```

- Use `--skip-git-update` for build verification unless the requested task explicitly includes upstream integration and push.
- A full build also deploys. State that side effect before using it only as a compile check.
- Test argument normalization, invalid configuration handling, and `--help` without invoking deployment.

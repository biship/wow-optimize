# wow_optimize

This repository builds and deploys a native performance-injection suite and launcher for World of Warcraft 3.3.5a build 12340.

## Stack

- Native: C and C++ with CMake, MSVC, MinHook, mimalloc, Win32, and SSE2.
- Launcher: one C# WinForms source file compiled with Roslyn against .NET Framework 4.8.
- Automation: PowerShell 7 in `build.ps1`.
- Output: 32-bit `wow_optimize.dll`, `version.dll`, `wow_loader.exe`, and `wow_optimize_launcher.exe`.
- Runtime data: `WTF\wow_opt.ini`; session evidence is under the client's `Logs\` and `Crashes\` directories.

## Instruction routing

| Work | Read |
| --- | --- |
| Native code, hooks, SIMD, allocators, or CMake | `AGENTS.cpp.md` |
| WinForms launcher | `AGENTS.csharp.md` |
| Build and Git automation | `AGENTS.powershell.md` |

## Commands

```powershell
# inspect build options without changing the checkout or client
.\build.ps1 --help

# build Release without the fetch, merge, and push phase; this still deploys to the configured WoW client
.\build.ps1 --skip-git-update --config Release

# validate patch whitespace
git diff --check
```

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | DLL entry, configuration, version metadata, proxy, and loader |
| `src/hooks_subsystems/` | Engine, renderer, I/O, sound, event, and data-path hooks |
| `src/runtime_vm/` | Lua VM and Lua C API hooks and caches |
| `src/allocators/` | Heap, allocation, texture-cache, and memory-pressure work |
| `src/simd_math/` | SSE2 replacements and their startup checks |
| `src/diagnostics/` | Session measurements, crash capture, and profiling |
| `src/launcher/` | C# WinForms launcher and embedded background image |
| `cmake/` | Upstream cross-compilation support; the local supported build path is MSVC |

## Invariants

- The runtime is a 32-bit process. Preserve Win32 pointer sizes, calling conventions, fixed-address assumptions, and the x86 build target.
- Runtime addresses and byte/prologue expectations are specific to compatible 3.3.5a build-12340 executables. Validate before installing a hook or patch and fail closed on a mismatch.
- Preserve `/fp:precise` and `/arch:SSE2`. Startup comparisons depend on the client's floating-point behavior.
- Keep a setting's section, key, default, and meaning synchronized across `src/core/config.h`, `src/core/config.cpp`, and `src/launcher/Launcher.cs`.
- Keep launcher and DLL INI path resolution synchronized. The preferred configuration path is `WTF\wow_opt.ini`.
- A release version change must update `version.txt`, `src/core/version.h`, `src/core/version.rc`, and the release text in `README.md` together.
- `build.ps1` without `--skip-git-update` fetches and merges `upstream/main`, then pushes `origin/main`. Do not use that mode for routine verification.
- Every non-help `build.ps1` run deploys files to `C:\ProgramData\WOW\WOWClient` and can remove or replace deployed PDB files.

## Testing

- There is no standalone automated test suite or CTest target.
- For source changes, require a successful Win32 build and `git diff --check`.
- For hook, allocator, threading, renderer, Lua VM, or launcher behavior, a successful build is not runtime proof. Verify in the client and inspect the matching timestamped session log.
- For crash fixes, correlate the exact build hash in the log with the matching dump and PDB files.

## Project references

| Document | Covers |
| --- | --- |
| `README.md` | Features, installation, compatibility, build outputs, runtime evidence, architecture, and troubleshooting |


# Native C++ and CMake

Applies to `CMakeLists.txt`, `cmake/`, and native sources under `src/`.

## Build contract

- CMake generates Visual Studio 2026 Win32 projects. The DLL and native executables use the static MSVC runtime.
- MinHook and mimalloc are fetched by CMake. Keep their declared tags and build options unless dependency work is requested.
- Add each new native source to the correct target in `CMakeLists.txt`.
- Keep Release builds debuggable and retain the PDB that matches each built binary.

## Runtime safety

- Treat raw addresses, structure layouts, Lua internals, vtables, prologues, and calling conventions as executable-build contracts.
- Check target bytes or callable behavior before detouring. If validation fails, leave the original path active and report the reason in the log.
- Do not publish a partial multi-hook feature. Roll back successfully created hooks when a later required hook fails.
- Keep client-owned objects, Lua states, D3D devices, and worker-thread data valid across reload, logout, loading-screen, and device-reset transitions.
- Preserve ownership boundaries between the WoW CRT, mimalloc, Win32 heaps, and virtual allocations. Allocate and release through the same owner.
- Keep experimental or unproven behavior default-off. Do not make **Enable All** activate experimental launcher entries.

## Verification

```powershell
.\build.ps1 --skip-git-update --config Release
git diff --check
```

- Confirm the build produced Win32 artifacts under `build\Release`.
- Exercise changed hooks in a compatible client. Use the timestamped session log, startup self-check messages, build hash, and crash dump as evidence.
- Compare performance changes with repeatable A/B sessions. Do not infer a gain from compilation or hook installation alone.

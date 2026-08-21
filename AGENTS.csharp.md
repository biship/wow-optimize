# C# WinForms launcher

Applies to `src/launcher/Launcher.cs` and its assets.

## Contract

- The launcher is a single source file compiled directly by `build.ps1`; there is no project file or package restore step.
- Target .NET Framework 4.8 WinForms. Keep APIs available in that framework.
- `wotlk_background.jpg` is embedded and copied beside the launcher.
- Keep each `SettingItem` section, key, default, experimental flag, and user-facing meaning consistent with the native configuration and implementation.
- Keep `ResolveIniPath` behavior identical to `Config::ResolveIniPath` in `src/core/config.cpp`.
- Preserve user settings and profiles. Do not reset or overwrite unrelated INI keys.

## Verification

```powershell
.\build.ps1 --skip-git-update --config Release
git diff --check
```

- Treat a successful Roslyn compile as structural verification only.
- Launch the built executable to verify layout, scaling, control state, profile operations, and save/load behavior after UI changes.
- Confirm that experimental entries remain excluded from **Enable All** and that defaults match the DLL when no INI key exists.


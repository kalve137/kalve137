# Debugging

Kalve 137 logs through spdlog to a rotating file, keeps last 2000 lines in memory and writes a minidump when it crashes.

## Log files

Logs and crash files live under app data folder:

```powershell
explorer "$env:LOCALAPPDATA\Kalve 137\Kalve 137"
```

- `logs\kalve137.log` is current log. It rotates at 5 MB and keeps 5 files.
- `crashes\` holds one `.dmp` and one `.log` per crash. `.log` is the in-memory ring at crash time, so it includes lines below file level.

Help > Open Log Folder opens `logs\`. With a debugger attached every line also reaches its output window.

## Channels and levels

Each subsystem logs to its own channel:

| Channel | Covers |
| --- | --- |
| `app` | Commands, feature panel, dialogs, Qt messages |
| `core` | Design edits, undo, recompute |
| `geometry` | Modeling failures |
| `sketch` | Constraint solver |
| `viewer` | OpenGL setup, OpenCascade messages |
| `input` | Mouse, picking and selection |
| `io` | Save, open, autosave, import and export |

Debug builds default to `debug`, release builds to `info`. A level spec sets a default and per-channel overrides:

```text
info,viewer=debug,input=trace
```

Levels are `trace`, `debug`, `info`, `warn`, `error`, `critical` and `off`. Release builds compile `trace` lines out.

Set levels in Preferences > Diagnostics > Log levels, or for one run with `KALVE137_LOG`. Environment variable wins over Preferences:

```powershell
$env:KALVE137_LOG = 'input=trace,viewer=debug'
.\build\windows-debug\kalve137.exe
```

Debug builds also have Help > Diagnostics. It toggles trace per channel, opens live log and triggers an exception or a crash on purpose.

## Logging from code

Link `kalve137_log`, include `"log/log.h"` and log with a channel:

```cpp
K137_INFO(Io, "Saved {} features to {}", count, log::utf8(path));
K137_TRACE(Input, "Press at {},{}", x, y);
```

Use `log::utf8()` for paths. `path::string()` throws on Windows for names outside code page.

## Crash reports

Crash handler catches access violations, `std::terminate`, pure virtual calls, invalid CRT parameters and `abort()`. Next launch offers to report it.

Help > Report a Problem builds a zip with a description, logs, crash files and system info. Design is opt-in. Copy Issue Text puts a markdown summary on clipboard for an issue.

## Reading a dump

Every RelWithDebInfo build copies its executable and PDB to `build\windows-release\symbols\<version>-<hash>\`. Keep that folder for every build handed out, since a dump only symbolizes against the exact PDB that built it.

Print crashing stack:

```powershell
.\scripts\symbolize.ps1 -Dump "$env:LOCALAPPDATA\Kalve 137\Kalve 137\crashes\kalve137-0.1.0-20261109-120000.dmp"
```

Add `-Analyze` for full `!analyze -v` report or `-Symbols <folder>` for PDBs kept elsewhere. Script needs WinDbg:

```powershell
winget install Microsoft.WinDbg
```

Opening `.dmp` in Visual Studio or WinDbg works too, with symbol path pointing at matching `symbols\` folder.

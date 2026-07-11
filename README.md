# tfm — Terminal File Manager

A modern two-pane terminal file manager for Windows 11 (Windows Terminal and legacy cmd).

Built in C11 with GCC, statically linked — no runtime dependencies beyond Windows system DLLs.

## Building

```powershell
.\build.ps1
```

Requires GCC (MinGW-w64 via MSYS2 recommended). Produces `tfm.exe` (~77 KB, static).

Options:
- `.\build.ps1 -Debug` — debug build with `-g -O0`
- `.\build.ps1 -Clean` — remove build artifacts
- `.\build.ps1 -NoSign` — skip code signing

## Keyboard Shortcuts

### Navigation
| Key | Action |
|-----|--------|
| `↑`/`↓` | Move cursor |
| `PgUp`/`PgDn` | Page up/down |
| `Home`/`End` | First/last entry |
| `Enter` | Enter directory / open file |
| `Backspace` | Parent directory |
| `Ctrl+D` | Drive selector |
| `Tab` | Rotate focus (Left → Right → CmdLine) |

### File Operations
| Key | Action |
|-----|--------|
| `F5` | Copy to opposite panel |
| `F6` | Move to opposite panel |
| `F7` | Create directory |
| `F8` | Delete (with confirmation) |
| `Space` | Toggle tag (multi-select) |
| `F3` | Progress/history panel |
| `F2` | Refresh panel |

### Tabs (1 main + up to 4 extra)
| Key | Action |
|-----|--------|
| `Alt+Shift+N` | New tab (starts in home directory) |
| `Alt+Shift+B` | Cycle to next tab |
| `Alt+Shift+M` | Close current tab (main tab locked) |

### Shell Line
| Key | Action |
|-----|--------|
| `Enter` | Execute command via `cmd.exe` |
| `↑`/`↓` | Command history |
| `Esc` | Clear command line |

### System
| Key | Action |
|-----|--------|
| `F1` | Toggle help screen |
| `F12` | Exit |
| `Esc` | Clear tags / dismiss overlays |

## Configuration

Saved to `%USERPROFILE%\.tfm\config.json` on exit:
- Startup directory per panel (main tab only)
- Per-drive last-visited paths (26 drive letters per panel)
- Theme selection
- Sort preferences

## Theming

Themes are JSON files in `themes/`. Default: `themes/default.json` (Catppuccin-inspired dark).

Create your own by copying the default and editing the 21 named color slots.

## Features
- Two-pane MC-style layout with ANSI box-drawing
- Background file operations (copy/move/delete) with progress bar
- Operation history (session-only, F3 to view)
- Multi-select via Space key
- Recursive directory copy/move/delete
- Drive switching with per-drive path memory
- Terminal resize support
- Themeable with JSON color files

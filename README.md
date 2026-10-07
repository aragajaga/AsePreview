# AsePreview

AsePreview is a native Shell preview handler for ASE color palettes.

## Features

- A grid of color swatches with names and group headings.
- Right-click a supported color tile to copy its displayed sRGB as HEX (`#FF8000`) or RGB (`255, 128, 0`).
- Scrolling with the mouse wheel, arrow keys, Page Up/Down, and Home/End.
- Tooltips with the full name, color model, original components, and color type.
- Light and dark system app themes, with support for high contrast.
- Buffered rendering during scrolling and resizing.

The native scrollbar may retain its system appearance depending on the system and preview host. Swatch colors are unaffected by the theme.

## Supported files

Supports ASE 1.0 palettes with RGB, Gray, CMYK, and Lab colors. RGB is interpreted as sRGB. CMYK and Lab are rendered approximately without ICC profiles; the Lab L component is stored as L*/100.

Unknown color models are shown with a question mark, unknown blocks are skipped, and malformed files display a diagnostic message. Files are limited to 64 MiB and 100,000 blocks.

Source ASE files are never modified. Color and group names are preserved in their original language, including Unicode characters.

## Build and test

Requires C++ build tools with the v145 toolset and the platform SDK. Run all commands from the repository root:

```powershell
.\Build-Verify.ps1
```

The script builds the DLL and tests for x64 and Win32 in Debug and Release. Automated checks cover parsing, streams, COM interfaces and lifetime, rendering, themes, and scrolling. Logs are saved in `_maintenance\verification`.

## Register the preview handler

After building, run this command in an **elevated terminal session**:

```powershell
.\Register-Preview.ps1
```

The script registers the x64 Release DLL from `x64\Release\AsePreview.dll`. It saves the previous registry values and key exports in `_maintenance\backups`, verifies registration, and notifies Shell. Failed registry writes restore the previous values. The default application for `.ase` files is not changed.

Use `-BackupOnly` to save the current registration without changing it, or `-DllPath` to specify another x64 DLL. Win32 builds are tested, but the script registers only x64. The DLL does not support registration through `regsvr32`.

To restore a previous registration, use the `values.json` path printed when the backup was created. Replace `<timestamp>` with the corresponding backup directory suffix:

```powershell
.\Register-Preview.ps1 -RestoreBackup '.\_maintenance\backups\registry-<timestamp>\values.json'
```

Rollback restores only the handler's registration values, leaving other preview handlers unchanged.

## Use and verify

Enable the file manager's preview pane with **Alt+P**, then select an `.ase` file. Check file switching, resizing, scrolling, tooltips, and system theme changes. Empty and malformed palettes display explanatory messages.

The registration script does not terminate the file manager or `prevhost.exe`. If an older DLL is already loaded, close the window showing its preview and reopen it.

### Copy colors

Right-click a color tile, including its name, and choose:

| Command | Copied text |
| --- | --- |
| Copy as HEX | `#FF8000` (uppercase `#RRGGBB`) |
| Copy as RGB | `255, 128, 0` (integer components from 0 to 255) |

Both commands copy the displayed sRGB color. CMYK and Lab colors use their approximate sRGB rendering. Group headings, empty space, and unsupported colors do not open a color menu. Cancelling the menu leaves the clipboard unchanged.

### Standalone test host

To check the preview in a standalone test host without changing registration:

```powershell
.\tests\_build\x64\Release\AsePreviewTests.exe .\x64\Release\AsePreview.dll --show '.\sample.ase'
```

Replace `sample.ase` with an existing palette. Plain `--show` follows the current system theme. `--show-dark` and `--show-light` supply fallback host colors; an available system theme takes priority over them.

Successful builds and automated tests do not replace manual verification in the Shell preview pane.

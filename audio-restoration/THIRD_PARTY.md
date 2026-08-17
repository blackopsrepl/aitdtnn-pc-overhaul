# Third-party notices

## Highly Theoretical

The Dreamcast AICA/ARM renderer uses source from the Highly Theoretical emulation project, included under `third_party/highly_theoretical/Core`.

- Upstream copyright notices are retained in the source files.
- License: GNU General Public License version 3.
- The complete license text is included at `third_party/highly_theoretical/Core/COPYING.txt` and `licenses/GPL-3.0.txt`.

Only the Dreamcast sound components required by this patch (`arm`, `dcsound`, and `yam`) are compiled.

## PyInstaller

Release builds use PyInstaller to package the asset-building Python script as a standalone Windows executable. PyInstaller's bootloader exception permits distribution of the resulting executable under this project's license. PyInstaller itself is a build dependency and is not included as source in this repository.

The packaged builder embeds CPython. Its license is included at `licenses/PYTHON-3.10.txt`; the PyInstaller license and bootloader exception are included at `licenses/PYINSTALLER.txt`.

## Inno Setup

The Windows installer is compiled with Inno Setup 6. Its license is included at `licenses/INNO-SETUP.txt`. Inno Setup is a build dependency and is not part of the patch runtime.

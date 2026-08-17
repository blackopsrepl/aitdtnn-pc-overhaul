# Third-party notices

## Highly Theoretical

The Dreamcast AICA/ARM renderer uses source from the Highly Theoretical emulation project, included under `third_party/highly_theoretical/Core`.

- Upstream: <https://gitlab.com/kode54/highly_theoretical>
- Upstream project: Christopher Snowhill / Highly_Theoretical.
- Imported base: upstream `main` at `0e4c18c5b757b04dbcb68c572c5a4f6fd803283c`.
- Local modification: on 2026-08-16 this project added the
  `YAM_DISABLE_DYNAREC` build guard in `Core/yam.c`; the source file carries the
  corresponding dated modification notice.
- License: GNU General Public License version 3.
- The complete license text is included at `third_party/highly_theoretical/Core/COPYING.txt` and `licenses/GPL-3.0.txt`.

Only the Dreamcast sound components required by this patch (`arm`, `dcsound`, and `yam`) are compiled.

## PyInstaller

Release builds use PyInstaller to package the asset-building Python script as a standalone Windows executable. PyInstaller's bootloader exception permits distribution of the resulting executable under this project's license. PyInstaller itself is a build dependency and is not included as source in this repository.

The packaged builder embeds 64-bit CPython 3.10.2. Its license is included at
`licenses/PYTHON-3.10.txt`; the PyInstaller license and bootloader exception are
included at `licenses/PYINSTALLER.txt`.

The complete release-build environment is pinned in `tools/requirements-build.txt`:
PyInstaller, pyinstaller-hooks-contrib, altgraph, packaging, pefile and
pywin32-ctypes. These packages are build tools; the asset builder imports only
the Python standard library, so their Python modules are not application runtime
dependencies. PyInstaller's generated analysis must remain part of release
verification.

## Inno Setup

The Windows installer is compiled with Inno Setup 6. Its license is included at `licenses/INNO-SETUP.txt`. Inno Setup is a build dependency and is not part of the patch runtime.

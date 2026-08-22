"""Tested recovery code for obsolete executable-patching development builds.

Production installation never calls this module and never modifies alone4.exe.
"""
from __future__ import annotations
import json
import os
import shutil
import struct
from pathlib import Path
from .pe import *

PATCH_SECTION = b".aitdaud"
PATCH_MARKER = b"AITDTNN_AUDIO_PATCH_V1\0"
PATCH_HOOK_NAME = "aitd4-audio-hook.dll"
PATCH_BACKUP_SUFFIX = ".aitdtnn-original"
PATCH_METADATA_SUFFIX = ".aitdtnn-patch.json"
PATCH_APPLIED = 0
PATCH_ALREADY_APPLIED = 10

# The PE patcher below is retained only as source-level history and as a tested
# recovery reference for pre-monorepo development builds. It is intentionally
# absent from main(), the packaged asset-builder CLI and the release installer.
# The published architecture always leaves alone4.exe byte-for-byte unchanged.


def has_audio_patch(image: bytes) -> bool:
    """Return whether an old development patch marker is present in the PE."""
    try:
        layout = parse_pe_layout(image)
    except ValueError:
        return False
    for section in layout.sections:
        if section.name == PATCH_SECTION:
            start = section.raw_address
            end = min(len(image), start + section.raw_size)
            return start < end and PATCH_MARKER in image[start:end]
    return False


def build_loader_stub(
    image: bytes, new_section_rva: int, hook_name: str = PATCH_HOOK_NAME
) -> bytes:
    layout = parse_pe_layout(image)
    load_library = find_import_iat_va(image, "KERNEL32.dll", "LoadLibraryA")
    exit_process = find_import_iat_va(image, "KERNEL32.dll", "ExitProcess")
    message_box = find_import_iat_va(image, "USER32.dll", "MessageBoxA")
    hook = hook_name.encode("ascii") + b"\0"
    title = b"AITD:TNN Dreamcast Music Fix\0"
    message = b"The Dreamcast music runtime could not be loaded. Reinstall the patch.\0"
    code_size = 52
    section_va = layout.image_base + new_section_rva
    hook_va = section_va + code_size
    title_va = hook_va + len(hook)
    message_va = title_va + len(title)

    code = bytearray((0x9C, 0x60, 0x68))
    code.extend(struct.pack("<I", hook_va))
    code.extend((0xFF, 0x15))
    code.extend(struct.pack("<I", load_library))
    code.extend((0x85, 0xC0, 0x75, 0x1C, 0x6A, 0x10, 0x68))
    code.extend(struct.pack("<I", title_va))
    code.append(0x68)
    code.extend(struct.pack("<I", message_va))
    code.extend((0x6A, 0x00, 0xFF, 0x15))
    code.extend(struct.pack("<I", message_box))
    code.extend((0x6A, 0x01, 0xFF, 0x15))
    code.extend(struct.pack("<I", exit_process))
    code.extend((0x61, 0x9D, 0xE9))
    jump_from = section_va + len(code) + 4
    jump_to = layout.image_base + layout.entry_rva
    code.extend(struct.pack("<i", jump_to - jump_from))
    if len(code) != code_size:
        raise AssertionError(f"loader stub size changed unexpectedly: {len(code)}")
    return bytes(code) + hook + title + message + PATCH_MARKER


def patch_executable_image(image: bytes) -> bytes:
    if has_audio_patch(image):
        raise ValueError("alone4.exe is already patched")
    layout = parse_pe_layout(image)
    new_header = layout.section_table + layout.section_count * 40
    if new_header + 40 > layout.size_of_headers:
        raise ValueError("PE headers do not have room for the audio loader section")
    highest_virtual_end = max(
        section.virtual_address + max(section.virtual_size, section.raw_size)
        for section in layout.sections
    )
    new_rva = align_up(highest_virtual_end, layout.section_alignment)
    payload = build_loader_stub(image, new_rva)
    new_raw = align_up(len(image), layout.file_alignment)
    new_raw_size = align_up(len(payload), layout.file_alignment)
    patched = bytearray(image)
    patched.extend(b"\0" * (new_raw - len(patched)))
    patched.extend(payload)
    patched.extend(b"\0" * (new_raw_size - len(payload)))
    section_header = struct.pack(
        "<8sIIIIIIHHI",
        PATCH_SECTION,
        len(payload),
        new_rva,
        new_raw_size,
        new_raw,
        0,
        0,
        0,
        0,
        0x60000020,
    )
    patched[new_header : new_header + 40] = section_header
    struct.pack_into("<H", patched, layout.pe_offset + 6, layout.section_count + 1)
    struct.pack_into("<I", patched, layout.optional_offset + 16, new_rva)
    struct.pack_into(
        "<I",
        patched,
        layout.optional_offset + 56,
        align_up(new_rva + len(payload), layout.section_alignment),
    )
    struct.pack_into("<I", patched, layout.optional_offset + 64, 0)
    return bytes(patched)


def executable_patch_paths(executable: Path) -> tuple[Path, Path]:
    return (
        executable.with_name(executable.name + PATCH_BACKUP_SUFFIX),
        executable.with_name(executable.name + PATCH_METADATA_SUFFIX),
    )


def atomic_write(path: Path, data: bytes) -> None:
    temporary = path.with_name(path.name + ".aitdtnn-new")
    try:
        with temporary.open("xb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def patch_executable(executable: Path) -> tuple[int, str]:
    executable = executable.resolve()
    image = executable.read_bytes()
    backup, metadata_path = executable_patch_paths(executable)
    if has_audio_patch(image):
        if not backup.is_file() or not metadata_path.is_file():
            raise ValueError("patched alone4.exe is missing its restoration metadata")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata.get("patched_sha256") != sha256(image):
            raise ValueError("patched alone4.exe was modified after installation")
        return PATCH_ALREADY_APPLIED, str(metadata.get("profile", "patched"))

    profile = detect_executable_profile(executable)
    original_hash = sha256(image)
    if backup.exists():
        if sha256(backup.read_bytes()) != original_hash:
            raise ValueError(f"refusing to overwrite unrelated backup {backup}")
    else:
        shutil.copy2(executable, backup)
    if metadata_path.exists():
        raise ValueError(f"stale patch metadata exists at {metadata_path}")

    patched = patch_executable_image(image)
    metadata = {
        "format": 1,
        "profile": profile,
        "hook": PATCH_HOOK_NAME,
        "original_sha256": original_hash,
        "patched_sha256": sha256(patched),
    }
    try:
        atomic_write(executable, patched)
        atomic_write(metadata_path, (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    except Exception:
        shutil.copy2(backup, executable)
        raise
    return PATCH_APPLIED, profile


def unpatch_executable(executable: Path) -> int:
    executable = executable.resolve()
    backup, metadata_path = executable_patch_paths(executable)
    if not backup.is_file():
        if executable.is_file() and not has_audio_patch(executable.read_bytes()):
            return PATCH_ALREADY_APPLIED
        raise ValueError("the original alone4.exe backup is missing")
    backup_image = backup.read_bytes()
    detect_executable_profile(backup)
    if has_audio_patch(backup_image):
        raise ValueError("the alone4.exe backup is itself patched")

    if executable.is_file():
        current = executable.read_bytes()
        if not has_audio_patch(current):
            if sha256(current) == sha256(backup_image):
                backup.unlink()
                if metadata_path.exists():
                    metadata_path.unlink()
                return PATCH_ALREADY_APPLIED
            raise ValueError("alone4.exe is no longer the executable installed by this patch")
        if not metadata_path.is_file():
            raise ValueError("patch metadata is missing; refusing to overwrite alone4.exe")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata.get("patched_sha256") != sha256(current):
            raise ValueError("alone4.exe was modified after patch installation; original backup was preserved")
        if metadata.get("original_sha256") != sha256(backup_image):
            raise ValueError("alone4.exe backup does not match the patch metadata")

    temporary = executable.with_name(executable.name + ".aitdtnn-restore")
    try:
        shutil.copy2(backup, temporary)
        os.replace(temporary, executable)
        backup.unlink()
        if metadata_path.exists():
            metadata_path.unlink()
    finally:
        if temporary.exists():
            temporary.unlink()
    return PATCH_APPLIED

from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_assets  # noqa: E402


def fixture_executable(dispatch_rva: int) -> bytes:
    pe_offset = 0x80
    optional_offset = pe_offset + 24
    section_table = optional_offset + 0xE0
    headers_size = 0x400
    text_rva = 0x1000
    text_raw = 0x400
    text_size = 0x99000
    rdata_rva = 0x9A000
    rdata_raw = text_raw + text_size
    rdata_size = 0x400
    image = bytearray(rdata_raw + rdata_size)

    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, pe_offset)
    image[pe_offset : pe_offset + 4] = b"PE\0\0"
    struct.pack_into("<HH", image, pe_offset + 4, 0x14C, 2)
    struct.pack_into("<H", image, pe_offset + 20, 0xE0)
    struct.pack_into("<H", image, pe_offset + 22, 0x010F)

    struct.pack_into("<H", image, optional_offset, 0x10B)
    struct.pack_into("<I", image, optional_offset + 16, text_rva)
    struct.pack_into("<I", image, optional_offset + 20, text_rva)
    struct.pack_into("<I", image, optional_offset + 24, rdata_rva)
    struct.pack_into("<I", image, optional_offset + 28, 0x400000)
    struct.pack_into("<I", image, optional_offset + 32, 0x1000)
    struct.pack_into("<I", image, optional_offset + 36, 0x200)
    struct.pack_into("<I", image, optional_offset + 56, 0x9B000)
    struct.pack_into("<I", image, optional_offset + 60, headers_size)
    struct.pack_into("<H", image, optional_offset + 68, 2)
    struct.pack_into("<I", image, optional_offset + 92, 16)
    struct.pack_into("<II", image, optional_offset + 104, rdata_rva, 60)

    struct.pack_into(
        "<8sIIIIIIHHI",
        image,
        section_table,
        b".text\0\0\0",
        text_size,
        text_rva,
        text_size,
        text_raw,
        0,
        0,
        0,
        0,
        0x60000020,
    )
    struct.pack_into(
        "<8sIIIIIIHHI",
        image,
        section_table + 40,
        b".rdata\0\0",
        rdata_size,
        rdata_rva,
        rdata_size,
        rdata_raw,
        0,
        0,
        0,
        0,
        0x40000040,
    )
    dispatch_offset = text_raw + dispatch_rva - text_rva
    image[dispatch_offset : dispatch_offset + len(build_assets.DISPATCH_SIGNATURE)] = (
        build_assets.DISPATCH_SIGNATURE
    )

    def rdata(offset: int) -> int:
        return rdata_rva + offset

    struct.pack_into("<IIIII", image, rdata_raw, rdata(0x80), 0, 0, rdata(0x40), rdata(0x90))
    struct.pack_into("<IIIII", image, rdata_raw + 20, rdata(0xA0), 0, 0, rdata(0x50), rdata(0xB0))
    image[rdata_raw + 0x40 : rdata_raw + 0x4D] = b"KERNEL32.dll\0"
    image[rdata_raw + 0x50 : rdata_raw + 0x5B] = b"USER32.dll\0"
    struct.pack_into("<III", image, rdata_raw + 0x80, rdata(0x100), rdata(0x120), 0)
    struct.pack_into("<III", image, rdata_raw + 0x90, rdata(0x100), rdata(0x120), 0)
    struct.pack_into("<II", image, rdata_raw + 0xA0, rdata(0x140), 0)
    struct.pack_into("<II", image, rdata_raw + 0xB0, rdata(0x140), 0)
    image[rdata_raw + 0x100 : rdata_raw + 0x100 + 15] = b"\0\0LoadLibraryA\0"
    image[rdata_raw + 0x120 : rdata_raw + 0x120 + 14] = b"\0\0ExitProcess\0"
    image[rdata_raw + 0x140 : rdata_raw + 0x140 + 14] = b"\0\0MessageBoxA\0"
    return bytes(image)


class ExecutablePatchTests(unittest.TestCase):
    def test_patch_is_idempotent_and_unpatch_is_exact(self) -> None:
        for profile, dispatch_rva in build_assets.EXECUTABLE_PROFILES:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory) / "alone4.exe"
                original = fixture_executable(dispatch_rva)
                executable.write_bytes(original)

                result, detected = build_assets.patch_executable(executable)
                self.assertEqual(build_assets.PATCH_APPLIED, result)
                self.assertEqual(profile, detected)
                patched = executable.read_bytes()
                layout = build_assets.parse_pe_layout(patched)
                self.assertTrue(build_assets.has_audio_patch(patched))
                self.assertEqual(build_assets.PATCH_SECTION, layout.sections[-1].name)
                self.assertEqual(layout.sections[-1].virtual_address, layout.entry_rva)
                self.assertIn(build_assets.PATCH_HOOK_NAME.encode() + b"\0", patched)

                second_result, second_profile = build_assets.patch_executable(executable)
                self.assertEqual(build_assets.PATCH_ALREADY_APPLIED, second_result)
                self.assertEqual(profile, second_profile)

                restore_result = build_assets.unpatch_executable(executable)
                self.assertEqual(build_assets.PATCH_APPLIED, restore_result)
                self.assertEqual(original, executable.read_bytes())
                backup, metadata = build_assets.executable_patch_paths(executable)
                self.assertFalse(backup.exists())
                self.assertFalse(metadata.exists())

    def test_unpatch_rejects_a_modified_patched_executable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "alone4.exe"
            executable.write_bytes(fixture_executable(build_assets.EXECUTABLE_PROFILES[0][1]))
            build_assets.patch_executable(executable)
            patched = bytearray(executable.read_bytes())
            section = build_assets.parse_pe_layout(patched).sections[-1]
            patched[section.raw_address + 10] ^= 0x01
            executable.write_bytes(patched)

            with self.assertRaisesRegex(ValueError, "modified after patch installation"):
                build_assets.unpatch_executable(executable)
            backup, metadata = build_assets.executable_patch_paths(executable)
            self.assertTrue(backup.is_file())
            self.assertTrue(metadata.is_file())
            self.assertEqual(
                json.loads(metadata.read_text(encoding="utf-8"))["original_sha256"],
                hashlib.sha256(backup.read_bytes()).hexdigest(),
            )


if __name__ == "__main__":
    unittest.main()

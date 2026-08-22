"""Decode Dreamcast music containers and write the runtime catalog."""
from __future__ import annotations
import csv
import json
import struct
from pathlib import Path
from typing import Iterable
from .disc import DiscFiles
from .iso9660 import decompress_padded_gzip
from .pe import sha256

EXPECTED_MINIMUM_SCENE_BANKS = 60

def parse_midb(data: bytes, container: str) -> tuple[int, list[bytes], list[tuple[str, bytes]], bytes]:
    if len(data) < 7 or data[:4] != b"MIDB":
        raise ValueError(f"{container}: invalid MIDB header")
    bank_id, sequence_count, map_count = data[4:7]
    maps_end = 7 + map_count * 128
    if maps_end > len(data):
        raise ValueError(f"{container}: truncated program maps")
    maps = [data[7 + index * 128 : 7 + (index + 1) * 128] for index in range(map_count)]
    offset = maps_end
    sequences: list[tuple[str, bytes]] = []
    for sequence_index in range(sequence_count):
        if offset + 12 > len(data):
            raise ValueError(f"{container}: truncated sequence header {sequence_index}")
        size = struct.unpack_from("<h", data, offset)[0]
        if size < 12 or offset + 12 + size > len(data):
            raise ValueError(f"{container}: invalid sequence size {size}")
        name = data[offset + 4 : offset + 12].split(b"\0", 1)[0].decode("ascii").lower()
        payload = data[offset + 12 : offset + 12 + size]
        if payload[:4] != b"DSEQ":
            raise ValueError(f"{container}: {name} is not a DSEQ")
        sequences.append((name, payload))
        offset += 12 + size
    bank_offset = data.find(b"SMPB", offset)
    if bank_offset < 0 or bank_offset + 12 > len(data):
        raise ValueError(f"{container}: embedded SMPB bank not found")
    bank_size = struct.unpack_from("<I", data, bank_offset + 8)[0]
    bank = data[bank_offset : bank_offset + bank_size]
    if bank_size < 16 or len(bank) != bank_size or bank[-4:] != b"ENDB":
        raise ValueError(f"{container}: malformed embedded SMPB bank")
    return bank_id, maps, sequences, bank


def write_tsv(path: Path, fieldnames: Iterable[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(fieldnames), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def build_assets(disc: DiscFiles, output: Path, source_label: str) -> dict[str, object]:
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"{output}: output directory is not empty")
    output.mkdir(parents=True, exist_ok=True)
    banks_dir = output / "banks"
    sequences_dir = output / "sequences"
    banks_dir.mkdir(exist_ok=True)
    sequences_dir.mkdir(exist_ok=True)

    driver = disc.read_file("MANATEE.DRV")
    allocation = disc.read_file("ALONE4.MLT")
    if len(driver) < 0x20 or driver[:4] != b"SDRV":
        raise ValueError("MANATEE.DRV has an invalid SDRV header")
    if len(allocation) < 0x20 or allocation[:4] != b"SMLT":
        raise ValueError("ALONE4.MLT has an invalid SMLT header")
    (output / "MANATEE.DRV").write_bytes(driver)
    (output / "ALONE4.MLT").write_bytes(allocation)

    scene_names = disc.list_files("MIDI/ALINE")
    if len(scene_names) < EXPECTED_MINIMUM_SCENE_BANKS:
        raise ValueError(f"Dreamcast MIDI/ALINE contains only {len(scene_names)} files")

    bank_rows: list[dict[str, object]] = []
    map_rows: list[dict[str, object]] = []
    sequence_rows: list[dict[str, object]] = []
    bank_hashes: dict[str, str] = {}
    for source_name in scene_names:
        container = source_name.lower()
        packed = disc.read_file(f"MIDI/ALINE/{source_name}")
        decoded = decompress_padded_gzip(packed, f"MIDI/ALINE/{source_name}")
        bank_id, maps, sequences, bank = parse_midb(decoded, container)
        bank_path = banks_dir / f"{container}.mpb"
        bank_path.write_bytes(bank)
        bank_hashes[container] = sha256(bank)
        bank_rows.append({"container": container, "bank_id": bank_id, "map_count": len(maps)})
        for map_index, voice_map in enumerate(maps):
            for global_voice, local_program in enumerate(voice_map):
                if local_program != 0xFF:
                    map_rows.append({
                        "container": container,
                        "map_index": map_index,
                        "global_voice": global_voice,
                        "local_program": local_program,
                    })
        for sequence_index, (name, payload) in enumerate(sequences):
            filename = f"{container}__{sequence_index:02d}__{name}.dseq"
            (sequences_dir / filename).write_bytes(payload)
            sequence_rows.append({
                "container": container,
                "sequence_index": sequence_index,
                "sequence_name": name,
                "size": len(payload),
                "sha256": sha256(payload),
                "file": f"sequences/{filename}",
            })

    gamesnd_decoded = decompress_padded_gzip(disc.read_file("MENU/GAMESND"), "MENU/GAMESND")
    gamesnd_offset = gamesnd_decoded.find(b"SMPB")
    if gamesnd_offset < 0 or gamesnd_offset + 12 > len(gamesnd_decoded):
        raise ValueError("MENU/GAMESND: SMPB bank not found")
    gamesnd_size = struct.unpack_from("<I", gamesnd_decoded, gamesnd_offset + 8)[0]
    gamesnd = gamesnd_decoded[gamesnd_offset : gamesnd_offset + gamesnd_size]
    if len(gamesnd) != gamesnd_size or gamesnd[-4:] != b"ENDB":
        raise ValueError("MENU/GAMESND: malformed SMPB bank")
    (banks_dir / "gamesnd.mpb").write_bytes(gamesnd)
    bank_hashes["gamesnd"] = sha256(gamesnd)

    write_tsv(output / "banks.tsv", ("container", "bank_id", "map_count"), bank_rows)
    write_tsv(output / "maps.tsv", ("container", "map_index", "global_voice", "local_program"), map_rows)
    write_tsv(
        output / "sequences.tsv",
        ("container", "sequence_index", "sequence_name", "size", "sha256", "file"),
        sequence_rows,
    )

    manifest: dict[str, object] = {
        "format": 1,
        "source": Path(source_label).name,
        "driver_sha256": sha256(driver),
        "allocation_sha256": sha256(allocation),
        "scene_banks": len(bank_rows),
        "bank_files": len(bank_rows) + 1,
        "program_map_entries": len(map_rows),
        "sequences": len(sequence_rows),
        "bank_sha256": bank_hashes,
    }
    (output / "asset-manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest

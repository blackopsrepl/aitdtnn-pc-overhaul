# FMV inventory and trigger audit

This audit targets the supported `alone4.exe` SHA-256
`5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672`.
Addresses below are image virtual addresses for the preferred base `0x00400000`.

## Native PC start paths

Every statically identified PC movie request converges on the native controller at
`0x004812CF`. The renderer detours that controller only to create an event ledger;
the trampoline continues through the original implementation and native Bink.

| Source | Calls | Movie / mask |
|---|---|---|
| Startup state machine `0x00479939` | `0x004799A7`, `0x004799C4` | ID 0 `LOGOS`; mask `0x40800000` or 0 according to the existing startup flag |
| Startup state machine `0x00479939` | `0x004799B6`, `0x004799D0` | ID 56 `SPIRAL`; mask `0x40800000` or 0 according to the same flag |
| Startup state machine `0x00479939` | `0x004799E9`, `0x00479A04` | ID 3 `TRAILER`; mask `0x40800000` |
| Ending branch in `0x00479939` | `0x00479B93` | ID 1 `EXTRO`; mask `0x00800000` |
| New-game controller `0x00480C36` | `0x00480D0D` | ID 2 `INTRO`; mask `0x40800000` |
| Script opcode 42 handler `0x00463A01` | `0x00463A71`, `0x00463A83` | evaluated movie ID and authored mask; ID 24 forces mask 0 |
| Restored character-select transition | confirmation `0x0047FCE5`, deferred at the new-game continuation `0x004BAE94` | ID 4 `SELECT_A` or ID 5 `SELECT_C`; Dreamcast-authored skip mask 0 |

The only two `BinkOpen` IAT callsites, `0x0049DC1C` and `0x0049DC92`, are the
primary and alternate-path attempts inside the same native player. They are not
independent movie start routes.

## Character-select proof

The PC current-item getter at `0x004A82D1` is side-effect free. The confirmation
path beginning at `0x0047FCE5` advances selection state from 2 to 3, then the
branch at `0x0047FD0E` tests item `0x2001` and sets character flag bit 4. The
other authored item is `0x2002`.

The homologous Dreamcast control flow supplies the missing movie request and proves
the names:

1. Dreamcast character select `0x8C04A010` sets flag bit 4 for item `0x2001` and
   clears it for item `0x2002`.
2. `0x8C032824` stores `(flags & 4) != 0` into the selected-game-state byte at
   offset `+8`.
3. `0x8C032E74` calls movie ID 5 when that byte is nonzero and identifies the
   route as `CARNBY`; it calls movie ID 4 when zero and identifies it as `ALINE`.
4. The PC `MOVIES.TXT` table names ID 5 `SELECT_C` and ID 4 `SELECT_A`.

The frontend then runs state 3 for the native `0x80`-tick portrait/title/voice
interstitial and returns through the higher new-game controller.  Its cold
continuation at `0x004BAE83` rejects load-game/cancelled paths, and the call at
`0x004BAE94` is the exact post-interstitial position where Dreamcast requests the
selection movie before route setup.  The PC restoration replaces that call with
a helper that plays the movie and then preserves the original call and return
value.  The mapping is exactly `0x2001 -> 5 SELECT_C` and
`0x2002 -> 4 SELECT_A`.

## Script route inventory

Opcode 42 is the only script-dispatch entry that calls the PC movie controller.
Fourteen unique serialized opcode-42 nodes exist. PC and Dreamcast copies are
byte-identical; both Dreamcast discs duplicate the same script nodes. Six nodes
belong to the Aline route and eight to the Carnby route.

| Aline scripts | Carnby scripts |
|---|---|
| `A101_092` | `C102_017` |
| `A112_020` | `C112_020` |
| `A120_066` | `C120_141` |
| `A120_068` | `C201_008` |
| `A303_100` | `C303_037` |
| `A703_131` | `C503_042` |
| | `C610_000` |
| | `C703_131` |

The raw on-disc operand payloads are serialized expression references, not
runtime movie IDs. The handler evaluates them through `0x00411AE8` before calling
`0x004812CF`. This audit intentionally does not mislabel those references as
movie numbers. Runtime reachability and the resolved ID/mask are captured by the
request ledger at the common controller.

## PC / Dreamcast asset inventory

Legend: `P` is present in the installed PC `movies` directory, `U` is only in
its `Unused` subdirectory, `D1`/`D2` are present on the corresponding Dreamcast
disc, and `-` is absent. The table is the complete 55-entry PC `MOVIES.TXT`
catalog. `Retail frames` is the frame count declared by that retail table. The
installed 2016 replacement pack uses 30 fps encodes and therefore has different
physical frame counts; those Bink header counts are listed separately so runtime
completion can be checked against the files actually being played.

| ID | Name | PC | DC | Retail frames | Installed frames |
|---:|---|:---:|:---:|---:|---:|
| 0 | LOGOS | P | D1,D2 | 251 | 601 |
| 1 | EXTRO | P | D2 | 1419 | 3404 |
| 2 | INTRO | P | D1 | 3323 | 7975 |
| 3 | TRAILER | P | D1,D2 | 385 | 924 |
| 4 | SELECT_A | P | D1 | 35 | 87 |
| 5 | SELECT_C | P | D1 | 37 | 87 |
| 10 | A420_115 | P | D2 | 118 | 281 |
| 11 | A903_105 | P | D2 | 104 | 248 |
| 12 | C114_025 | P | D1 | 91 | 217 |
| 13 | C114_026 | P | D1 | 88 | 211 |
| 14 | C121_040 | P,U | D1 | 88 | 202 |
| 15 | C200_009 | P | D1 | 382 | 917 |
| 16 | C205_013 | P | D1 | 55 | 131 |
| 17 | C206_011 | P | D1 | 93 | 222 |
| 18 | C210_010 | P | D1 | 75 | 180 |
| 19 | C503_046 | P | D2 | 593 | 1421 |
| 20 | C801_045 | P | D2 | 171 | 409 |
| 21 | C803_049 | P | D2 | 166 | 398 |
| 22 | C808_052 | P | D2 | 87 | 207 |
| 23 | C825_057 | P | D2 | 68 | 162 |
| 24 | C101_015 | P | D1 | 136 | 326 |
| 25 | A125_099 | P | D1 | 133 | 319 |
| 26 | A904_148 | P | D2 | 213 | 511 |
| 27 | A125_097 | P,U | D1 | 97 | 222 |
| 28 | A411_120 | P | D2 | 101 | 241 |
| 29 | A411_119 | P | D2 | 126 | 301 |
| 30 | A411_118 | P | D2 | 45 | 106 |
| 31 | A411_117 | P | D2 | 63 | 151 |
| 32 | A410_121 | P | D2 | 205 | 492 |
| 33 | TR114B | P | D1 | 43 | 116 |
| 34 | C813_061 | P | D2 | 430 | 1031 |
| 35 | C503_042 | - | - | 210 | - |
| 36 | C703_058 | P | D2 | 257 | 616 |
| 37 | C300_033 | P | D1,D2 | 297 | 711 |
| 38 | C105_023 | P | D1 | 244 | 584 |
| 39 | A902_104 | P | D2 | 68 | 150 |
| 40 | C105_022 | P | D1 | 213 | 502 |
| 41 | A820_103 | P | D2 | 220 | 516 |
| 42 | A404_126 | P | D2 | 232 | 556 |
| 43 | A404_116 | P | D2 | 101 | 241 |
| 44 | A304_114 | P | D2 | 105 | 251 |
| 45 | A125_098 | P | D1 | 202 | 483 |
| 46 | A120_072 | P | D1 | 118 | 282 |
| 47 | A112_167 | P | D1 | 222 | 460 |
| 48 | A105_087 | P,U | D1 | 125 | 291 |
| 49 | A105_083 | P | D1 | 323 | 775 |
| 50 | C112_020 | U | D1 | 107 | 90 |
| 51 | A111_122 | P | D1 | 490 | 1176 |
| 52 | A825_165 | P | D2 | 549 | 1317 |
| 53 | C128_028 | P | D1 | 238 | 571 |
| 54 | C302_034 | P | D2 | 70 | 168 |
| 55 | C302_035 | P | D2 | 170 | 407 |
| 56 | SPIRAL | P | - | 180 | 206 |
| 57 | RAD | - | - | 150 | - |
| 58 | DARK | - | - | 342 | - |

The Dreamcast movie directories additionally contain `A112_137` and
`C125_097`, which are not entries in the PC catalog; matching files exist only
under the active PC install's `Unused` directory. Missing catalog entries are
reported as evidence, not synthesized or supplied by the renderer.

## Runtime ledger contract

For each request the log records `expected` (for the restored selection movies),
`request`, `open`, `first-frame`, `close`, and `frames`, followed by
`request-complete`. A request that never opens is therefore distinguishable from
a Bink open/decode/skip failure without replacing or bypassing native Bink.

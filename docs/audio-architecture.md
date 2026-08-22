# Audio restoration: investigation and architecture

## 4. Music and ambience restoration

### 4.1 Initial problem

The PC release does not reproduce the Dreamcast soundtrack correctly on modern
systems. The relevant content is not a set of ordinary streamed tracks: it is an
interactive MIDI-like sequence system with per-container program maps, sample
banks and scene state. A simple replacement of loose files or pre-rendered audio
cannot preserve transitions and authored interactivity.

The first investigation overemphasized the loose `Sound2` directory and assumed
that matching filenames or hashes would be enough. Extraction of the retail PC
data instead exposed 233 per-scene program-bank records representing 209 distinct
banks. That made a blanket sample replacement unsafe: the runtime container,
program map and sequence identity all matter.

### 4.2 What comparison established

A local PC-versus-Dreamcast development audit was performed for all 69 Aline and
69 Carnby music containers. After decompression and parsing, that audit reported
the following byte-identical between the compared releases:

- MIDB bank identifiers;
- program maps;
- sequence names, order and payloads;
- SMPB sample-bank payloads.

This corrected the working hypothesis that the loose PC MIDI assets themselves
were generally swapped. The compared copyrighted corpora are not retained in
the repository, and the one-off comparison has not yet been converted into a
BYO-data reproducibility command. It is therefore development evidence, not an
independently reproducible source-tree proof. The remaining observed failure
surface was runtime selection and rendering: which container the PC engine had
loaded, how ambiguous containers were identified, and how sequence events were
translated into synthesis.

#### DSEQ is not Manatee SMSD

The similar names and event-like payload initially suggested that the extracted
game-level DSEQ data could be wrapped in a Manatee SMSB and handed to the
driver's native sequence-start command. Direct validation against the retail ARM
driver disproved that assumption. Its native sequencer requires an SMSB v2 bank
containing SMSD tracks; the first SMSD words are consumed as native timing
fields. The corresponding DSEQ bytes instead contain game-specific metadata and
cannot be reinterpreted as those fields.

Therefore the correct boundary is not a new SMSB converter or a replacement
sequencer. DSEQ remains on the game side of the boundary, where the existing
game parser turns it into timed events. Manatee/AICA remains on the audio-backend
side, where those events select voices and produce PCM.

Six map-identity collision groups demonstrated why inference cannot be the
authoritative solution. Some containers have identical program maps, and some
also have identical sequence and bank payloads. The runtime now has one identity
source: the game function that loads a named MIDI container. The hook associates
that exact name with every sequence object created by the load and with the
shared bank slot updated by the load. Complete loaded DSEQ validates the former;
complete live program maps validates the latter. The identities are deliberately
separate because the game can keep a DSEQ player active while a room transition
replaces its shared program/sample bank.

If a persistent music bank cannot be resolved exactly, the hook refuses to bind
it instead of silently rendering through a generic bank.

#### Complete collision audit

An audit of all 69 extracted containers, using the runtime resolver's complete
key (`bank_id` plus every 128-byte program map), found exactly six collision
groups:

| Containers | Dreamcast bank | Live DSEQ evidence | Authoritative identity |
| --- | --- | --- | --- |
| `airc0`, `airv0` | byte-identical | distinct | captured load name |
| `jardinc0`, `jardinv0` | byte-identical | distinct | captured load name |
| `manoira0`, `manoirb0` | byte-identical | distinct | captured load name |
| `act_c12`, `act_c13` | byte-identical | all three payloads byte-identical | captured load name |
| `inv_b1`, `jarding1` | byte-identical | all three payloads byte-identical | captured load name |
| `chapelle`, `jardin1` | byte-identical | one shared payload | captured load name |

This is not a PC-versus-Dreamcast catalog difference: the compared platform
data is byte-identical, and 57 of the 69 containers are map-unique. More
importantly, every colliding group has both identical maps and a byte-identical
Dreamcast MPB bank. Content inference therefore cannot recover the authored
name for every case. Capturing the name and resulting object in the same loader
call removes that collision instead of concealing it behind an equivalent alias.

The retained logs contain one unresolved dispatch: slot 3, bank 0, with two
candidates during the transition from `intro_b1` into the three resolved
`jardin2` layers. The obsolete `CreateFileA` heuristic installed but captured no
associated loads, while its DSEQ probe compared the wrong offsets. Both
inference mechanisms were removed. The replacement records identity around the
game's synchronous container parser and validates the full DSEQ after accounting
for the parser's in-place big-endian-to-little-endian offset conversion. The old
log did not record candidate names or the suppressed event bytes, so it cannot
prove which bank-0 pair or how audible that event would have been.

The replacement was then exercised through the same title/menu transition. The
game loaded `\midi\carnby\jardinv0`; the hook associated that name with slot 3's
exact base/table pointers and validated the complete `vent_v` DSEQ. The result
was `match=jardinv0/vent_v evidence=loader-identity candidates=1 bound=yes`.
All three simultaneously loaded `jardin2` layers were independently registered
and validated, with no unresolved, missing-identity or validation-failure entry.
This validates the formerly unresolved position without claiming that the test
entered Carnby gameplay.

A subsequent Aline run exposed the sequence/bank lifetime distinction. On the
`grenier1` to `grenier5` transition, slot 0 retained and continued dispatching
its exact `grenier1/intro_a1` DSEQ while bank 1 and its complete live maps changed
to `grenier5`. The executable's update loop at `0x498877` processes a player only
while its `state+0x134` active flag is set, and its DSEQ parser applies the
current inverse program map before calling the hooked dispatcher. The slot was
therefore neither stale nor unresolved: it was an active `grenier1` sequence
driving the current `grenier5` bank. Treating sequence container and bank
container as one identity caused 213 valid dispatches to be suppressed. The
resolver now validates and tracks those identities independently.

Repeated transitions then exposed a second-order lifetime issue: loading a new
container into a shared Manatee bank invalidates every renderer player attached
to that bank, even if a player's PC sequence pointers and selector do not change.
The hook previously used only those per-player fields to decide whether to bind
again. It now includes the authoritative bank-load serial in that state key, so
every affected active player is rebound after every shared-bank replacement.

### 4.3 Continuous-tone failures

Several prototypes produced unintelligible output or one sustained tone. Logs
and offline event replay showed that this was not simply a volume problem and
not adequately explained by a missing PC note-off. The implementation had
multiple hazards:

- unresolved persistent music could fall through to the `gamesnd` bank;
- scene replacement did not guarantee a complete active-note teardown;
- repeated note-ons were represented as a single active bit, so one note-off
  could not balance multiple starts;
- a sequence diagnostic compared the sequence base rather than the actual table
  at `base + 0x10`, making useful identity evidence appear absent;
- initialization performed too much work in `DllMain` and raced renderer
  readiness.

The final path uses counted note ownership, emits explicit teardown on every
scene-state replacement, initializes unknown programs to an invalid sentinel,
suppresses unsupported messages, and fails closed on unresolved persistent
music. Initialization moved to the explicit loader-controlled export and returns
success only after the Dreamcast renderer is ready.

### 4.4 Final audio division of responsibility

The PC engine remains responsible for gameplay-driven container selection and
its game-side DSEQ parser remains responsible for producing timed interactive
events. This is scheduling, not PC audio rendering. The hook suppresses the PC
music destination, validates the active DSEQ's exact source container, binds the
events to the independently current shared Dreamcast bank, and renders them
through the Dreamcast Manatee/AICA path based on the GPLv3 Highly Theoretical
core. Extracted Dreamcast DSEQ data supplies identity evidence rather than being
misrepresented as native Manatee SMSD.

The resulting music is therefore Dreamcast-driver/bank/AICA synthesis under the
PC executable's unchanged gameplay and DSEQ timing logic. It is neither the PC
music renderer nor an independently recreated Dreamcast scene system. The local
cross-platform comparison found the relevant DSEQ payloads, program maps and
sample banks byte-identical; retaining the existing parser avoids duplicating
that logic. The module would not correct a genuinely different scene-to-cue
decision. That boundary is explicit rather than hidden behind the word
"restoration."

The normalized local extraction result contains 69 scene containers, 70 bank
files including `gamesnd`, 161 DSEQ files and 550 program-map entries. Disc 1
and Disc 2 inputs normalize to the same runtime payload; no extracted file is
committed or packaged with the project.

The division is intentional:

- music and ambience synthesis: Dreamcast Manatee, banks and emulated AICA;
- music-container selection and DSEQ event timing: unchanged game logic;
- ordinary PC sound effects: native PC path;
- movie audio: native Bink path;

The module does not play MP3 replacements and does not ship Dreamcast assets.

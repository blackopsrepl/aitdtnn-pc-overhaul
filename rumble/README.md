# AITD:TNN Dreamcast rumble restoration

This module restores the PC port's compiled-out vibration backend. The PC game
already retains the original authored vibration state, duration counter and
request calls; `aitd4-rumble-hook.dll` intercepts only the dead backend at the
hash-verified address and translates its original selector/value requests into
the same simple `pdVib 2.01` commands used by the Dreamcast build.

The original scheduler submits selector 0 and then selector 1 on each tick.
Dreamcast `MDCF_SetCondition` replaces the active command immediately, so the
second submission wins.  The faithful profile preserves that ordering and
replacement behavior; it does not invent dual-motor mixing.  The final decoded
Dreamcast strength is sent equally to both XInput motors.

The installed module requires neither Flycast nor a Dreamcast image. It ships no
game, console executable, script, movie or other copyrighted asset.

Supported executable SHA-256:

`5668118e0e19d569986500a1c805a85397c8681e7b672b49a68645462eccc672` (15-slot/no-CD)

`320908af4ce5c724b60a7eea6a5aade737d51d65aee8506744fce6e6dd0143e0` (retail CD)

Public initializer:

```cpp
extern "C" DWORD WINAPI AITD4_RumbleInitialize(void* reserved);
```

The shared loader must initialize this module after audio restoration and the
renderer, before entering the original game entrypoint.

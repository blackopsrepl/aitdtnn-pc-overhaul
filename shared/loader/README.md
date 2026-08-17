# Shared loader

`version.dll` is a self-contained x86 `/MT` proxy for the complete overhaul.
It resolves and tail-jumps all 17 exports from the real Windows 32-bit
`version.dll`, loaded by an absolute system-directory path.

During `DLL_PROCESS_ATTACH`, the proxy performs no module initialization. It
only validates the loaded PE and exact five-byte supported entrypoint prologue,
then replaces those bytes in memory with a relative jump. The gate runs after
the loader lock has been released. It SHA-256-validates pristine `alone4.exe`,
loads audio first, renderer second and rumble third, then reproduces
`55 8B EC 6A FF` and continues at the original entrypoint plus five bytes. Every
module must return successful initialization while the game remains suspended;
otherwise the loader fails closed.

Supported executable SHA-256:

`5668118e0e19d569986500a1c805a85397c8681e7b672b49a68645462eccc672`

Static verification:

```powershell
.\verify.ps1 -ProxyPath ..\..\build\shared-loader\version.dll `
  -GameExe 'D:\Games\Alone in the Dark - The New Nightmare\alone4.exe'
```

The loader neither contains nor creates copyrighted assets. It is not an
installer and has no shortcut or launcher behavior.

# CoDGLFix: Modern GPU Crash Fix for Call of Duty (2003) and United Offensive

Lets **Call of Duty (2003)** and **Call of Duty: United Offensive** load missions on modern graphics cards.
Released on Nexus Mods as *Modern GPU Crash Fix for CoD and United Offensive* (`CoDGLFix-1.0.0.zip`).

## The problem

The original games copy the graphics driver's `GL_EXTENSIONS` string into a fixed-size buffer. Today's drivers
report thousands of characters, and the game often crashes while a mission loads. In my tests on an NVIDIA RTX
card, without a fix 3 of 5 Call of Duty launches and all 3 United Offensive launches crashed; with it, none did.

## What it does

* **`CoDGLFix.exe`** is a Steam launch-option wrapper. Steam starts it with the game's own command line
  (`"...\CoDGLFix.exe" %command%`). For the single-player games (`CoDSP.exe`, `CoDUOSP.exe`) it starts the game
  exactly as Steam asked and loads `codglfix.dll` into it (`CreateRemoteThread` + `LoadLibraryA`). Any other exe
  (Multiplayer) is started unchanged. It writes a one-line `CoDGLFix.log` ("fix loaded") and changes no files.
* **`codglfix.dll`** hot-patches `opengl32!glGetString` inside the game, so `glGetString(GL_EXTENSIONS)` returns
  only the extensions an id Tech 3 / Call of Duty renderer looks for (and the driver really has). Every other call
  goes to the real function.
* **`CoDGLFix.ini`**: `borderless=1` removes the title bar of a windowed game and sizes it to the monitor. Off by
  default.

Why a program and not just a file: the fix has to be inside the game process before its renderer starts, and the
game loads `C:\Windows\system32\opengl32.dll` by absolute path, so a drop-in `opengl32.dll` next to the game is
never loaded. The game exe (SteamStub-wrapped) imports only `kernel32.dll`, and the renderer starts before the game
loads any DLL from its own folder.

## Files

| File | |
|---|---|
| `CoDGLFix.cs` | the launch-option wrapper (C#, .NET Framework 4) |
| `codglfix.c` | the DLL (C) |
| `CoDGLFix.ini` | settings shipped with the release (`borderless=0`) |

## Build

Requirements: Windows (the .NET Framework 4 C# compiler is built in) and [Zig 0.17.0](https://ziglang.org/download/).

```bat
:: CoDGLFix.exe (32-bit, so LoadLibraryA matches the 32-bit game)
C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe -nologo -target:winexe -platform:x86 -optimize -out:CoDGLFix.exe -r:System.Windows.Forms.dll CoDGLFix.cs

:: codglfix.dll (32-bit)
zig cc -target x86-windows-gnu -shared -O2 -o codglfix.dll codglfix.c
```

## Released binaries (SHA-256)

The files inside `CoDGLFix-1.0.0.zip`. A rebuild will not match byte for byte, because the compilers embed build
timestamps and IDs, but it behaves the same.

```
c509628fa8dd1b374000bc0189dbe47cc5b568ee4185018d90fdccd330fa3db4  CoDGLFix.exe
880a4efbfd0a992852e41ed2de8b7ed8ea4432894890c29207eb8a5e035846c9  codglfix.dll
```

## Use

1. Extract the release zip into the Call of Duty folder (it adds `CoDGLFix\`).
2. Steam: right-click the game > Properties > General > Launch Options:
   `"<Call of Duty folder>\CoDGLFix\CoDGLFix.exe" %command%`
   (a mod's own options go after `%command%`).

## License

MIT, see LICENSE. Call of Duty is a trademark of Activision; this fix is not affiliated with or endorsed by Activision.

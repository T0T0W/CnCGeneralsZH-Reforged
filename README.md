<div align="center">

# Zero Hour Reforged

Command & Conquer: Generals Zero Hour, rebuilt from the source EA opened and played as a game again.

[![release](https://img.shields.io/github/v/release/olcayseygan/CnCGeneralsZH-Reforged?style=for-the-badge&label=release&labelColor=161b22&color=4459b6)](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/latest)
![platform](https://img.shields.io/badge/platform-Windows%20x86-0d1117?style=for-the-badge&labelColor=161b22)
![renderer](https://img.shields.io/badge/renderer-Direct3D%2011-0d1117?style=for-the-badge&labelColor=161b22)
![license](https://img.shields.io/badge/license-GPL--3.0-0d1117?style=for-the-badge&labelColor=161b22)

[Download](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/latest) · [Every change](CHANGELOG.md) · [Build it](#build-it)

</div>

---

EA released the source of Generals and Zero Hour in 2025. The game itself never left the shelf: it is
still sold, and it still runs on Steam.

Reforged is that source moved to Visual Studio 2022 and worked on as a game. About 580 engine source
files were ported. Around sixty bugs were found and fixed that EA shipped in 2003 and nobody noticed
for twenty-two years. And the computer opponent builds a base now, which it never could.

No unit or weapon is rebalanced. You need your own copy of Zero Hour, because no game data ships
here.

## Play it

1. Have Zero Hour installed from Steam or the EA app.
2. Download `CnCGeneralsZHReforged-Setup-<version>.exe` from the
   [latest release](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/latest) and run
   it. The installer is not code signed, so Windows SmartScreen asks once before it starts.
3. The launcher finds your Zero Hour folder and lists every file it is about to write before it
   writes any. Each file it replaces is backed up first. Press Install, then Play.

The launcher carries no game files of its own. It downloads the build, checks every file against a
signed list, and keeps both the game and itself up to date from there. A Steam copy still starts
through Steam. Uninstall on the launcher's settings page puts back the files your game had before.
When the game crashes, the launcher sends the report without asking you for anything.

## Then and now

| | The source EA released | Reforged |
|:--|:--|:--|
| Frame rate | the picture waits on the game's clock | uncapped, and the rules keep their own |
| Worst logic turn | `2,976 ms` | `243 ms` |
| A 23-cell route | `55,000` cells searched in `256 ms` | `10,000` cells in `25 ms` |
| Route searching over a match | `11.8 s` | `1.6 s` |
| Skirmish opponent | urgent orders and one power plant | builds, scouts, expands and retreats |
| Base game textures | Zero Hour's downscaled copies | 481 originals at four times the resolution |
| Infantry shadows | a flat blob | cast from the pose |
| A screen of fire and smoke | 459 draw batches, `11.6 ms` a frame | 177 batches, `6.7 ms` |
| Language | English | English or Türkçe |
| A crash | silence | a report with file and line, sent by the launcher |

## What a match feels like now

### An opponent that plays fair and plays well

One wrong value kept the skirmish AI building only what its script marked urgent, plus a single power
plant. Every skirmish played against this code since 2003 was against an opponent that could not
build a base. It builds one now. It also scouts instead of reading your start position off the lobby,
walks a rifleman into oil derricks instead of shelling them, and on Hard it takes a losing team out
of a fight.

Easy, Medium and Hard differ in what the computer is allowed to decide. No level gets extra money,
cheaper units, faster building or longer sight. Measured over 32 headless matches with the seats swapped both ways,
Hard beats Easy 15-0.

### Orders that happen

Attack-move fights what it meets. Aircraft make their pass, fly home, rearm and pick your order back
up. Long moves stopped hitching once EA's coarse route search, which never ran, finally did its job.
A group sent across the map travels as a crowd and passes the slow vehicle in front, where it used to
queue behind it in one long column.

### Controls you pick

Options > Controls has Modern and Legacy. Modern is this game: left selects, right orders, and grid
hotkeys run the command bar. Legacy is the game as it shipped in 2003, letters on the buttons and
Ctrl held for force fire. Under either one, hold the button on the radar and drag, and the camera
follows the cursor across the map.

A single strip above the command bar carries everything your base is building, soonest first. Click
a picture and the camera goes there. Shift on a build button queues five, Ctrl twenty.

### Network games the host sets up

The lobby has a settings page. Peace time runs three to fifteen minutes. The unit limit shares 840
units between the players. Superweapons can be allowed, limited to one or banned, and Pro Rules takes
a fixed list of units and tricks out of every network match. Your ally's mouse shows on your map as a
pool of their colour with their name on it. Two copies of the game on one machine can play each other
over the LAN screen.

### A picture that holds up

The game draws through Direct3D 11 by default, with glow and edge smoothing over the battlefield.
`-d3d9` brings back the old renderer. Soldiers cast shadows from their pose and all 128 tree types
cast theirs. Eighty-nine kinds of explosion light the ground around them, and every smoke and fire
sprite is drawn: 101,000 of them at 8.3 ms a frame.

Wide screens get a command bar in three pieces instead of one stretched strip, and text that grows
with the monitor. On an ultrawide the view opens sideways, so a wider screen shows more battlefield.

### Türkçe

Options > Gameplay > Language. Menus, the command bar, briefings, tooltips and the credits come to
3,853 lines of Turkish, written against one glossary of more than 1,400 terms. Voices and videos stay
as your install has them.

> [!NOTE]
> [CHANGELOG.md](CHANGELOG.md) is the whole record: every change with the numbers behind it, and the
> work that was tried and taken back out.

## How it was tested

There is no CI and no debugger on the machine this is built on. The game tests itself instead.

- 38 automated test suites run on every build.
- Headless, a 23-minute skirmish plays out in 38 seconds, the same way on every run.
- An AI change is argued with 20 headless matches on the same seeds, win rate and match length
  before and after.
- A graphics change is argued in pixels: the same frame of the same match from two builds, and the
  pixels between them counted.
- Two copies over one real connection caught every multiplayer replay falsely reporting a desync
  since 2003.
- Every fix was proved by putting the bug back and watching its test fail.

What did not work stays written down. The first group movement rework and tree shadows cast from
stencil volumes were both built and measured, then reverted, and CHANGELOG says why.

---

## Build it

Win32 x86 only. The code is full of 32-bit inline assembly, and an x64 configure is refused on
purpose. Visual Studio 2022 with the Desktop C++ workload is enough.

```console
cmake -S GeneralsMD/Code -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`build.example.bat` wraps all three. Copy it to `build.bat` and fill in the paths for your machine.

The build copies `generals.exe` into `GeneralsMD/Run/`, which is the only place the game starts. Put
your Zero Hour `*.big` files next to it and the base game's `*.big` files in `Run/ZH_Generals/`,
since Zero Hour is not standalone and mounts both.

> [!IMPORTANT]
> EA stripped some third-party sources, and they are not committed here either. Drop in zlib 1.1.4,
> LZH-Light 1.0, a minimal DirectX 8 SDK and the GameSpy SDK before the first build. STLport, the
> 3ds Max 4 SDK, NVASM, the Miles and Bink SDKs and SafeDisc are not needed: sound and video run
> through the `mss32.dll` and `binkw32.dll` your own install already has. The stub DLLs of the same
> names in `build/Release/` are link libraries only, and copying one into `Run/` silences the game.

The launcher is a separate Electron project,
[olcayseygan/zhr-launcher](https://github.com/olcayseygan/zhr-launcher).

<details>
<summary>Settings the options screen does not show</summary>

<br>

Almost everything is on the six pages of the options screen now. These still live only in
`Options.ini`, in your Zero Hour Data folder.

| Line | What it does |
|:--|:--|
| `ShowAllyCursors = no` | stops sending your mouse position to allies and stops drawing theirs |
| `EdgeScrollInWindowedMode = yes` | scrolls at the screen edge in a window too |

On the command line, `-d3d9` starts the old renderer and `-dx11post off` gives the Direct3D 11 picture
without its finishing passes.

</details>

## Not there yet

- LAN and online play between separate machines have not been tested. Two copies on one machine play
  each other fine.
- A Direct3D 11 frame still costs more than the old renderer's, 13.6 ms against 8.6 ms on a screen
  full of Inferno Cannon fire. `-d3d9` is the way back.
- Random maps generate and play, and they are out of the skirmish list until the generator stops
  dealing seeds that should not be played. `-randommap <seed>` starts one.

---

<details>
<summary>EA's original README</summary>

<br>

This repository includes source code for Command & Conquer Generals, and its expansion pack Zero Hour. This release provides support to the Steam Workshop for both games ([C&C Generals](https://steamcommunity.com/workshop/browse/?appid=2229870) and [C&C Generals - Zero Hour](https://steamcommunity.com/workshop/browse/?appid=2732960)).

### Dependencies

If you wish to rebuild the source code and tools successfully you will need to find or write new replacements (or remove the code using them entirely) for the following libraries;

- DirectX SDK (Version 9.0 or higher) (expected path `\Code\Libraries\DirectX\`)
- STLport (4.5.3) - (expected path `\Code\Libraries\STLport-4.5.3`)
- 3DSMax 4 SDK - (expected path `\Code\Libraries\Max4SDK\`)
- NVASM - (expected path `\Code\Tools\NVASM\`)
- BYTEmark - (expected path `\Code\Libraries\Source\Benchmark`)
- RAD Miles Sound System SDK - (expected path `\Code\Libraries\Source\WWVegas\Miles6\`)
- RAD Bink SDK - (expected path `\Code\GameEngineDevice\Include\VideoDevice\Bink`)
- SafeDisk API - (expected path `\Code\GameEngine\Include\Common\SafeDisk` and `\Code\Tools\Launcher\SafeDisk\`)
- Miles Sound System "Asimp3" - (expected path `\Code\Libraries\WPAudio\Asimp3`)
- GameSpy SDK - (expected path `\Code\Libraries\Source\GameSpy\`)
- ZLib (1.1.4) - (expected path `\Code\Libraries\Source\Compression\ZLib\`)
- LZH-Light (1.0) - (expected path `\Code\Libraries\Source\Compression\LZHCompress\CompLibSource` and `CompLibHeader`)

### Compiling (Win32 Only)

To use the compiled binaries, you must own the game. The C&C Ultimate Collection is available for purchase on [EA App](https://www.ea.com/en-gb/games/command-and-conquer/command-and-conquer-the-ultimate-collection/buy/pc) or [Steam](https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/).

The quickest way to build all configurations in the project is to open `rts.dsw` in Microsoft Visual Studio C++ 6.0 (SP6 recommended for binary matching to Generals patch 1.08 and Zero Hour patch 1.04) and select Build -> Batch Build, then hit the “Rebuild All” button.

If you wish to compile the code under a modern version of Microsoft Visual Studio, you can convert the legacy project file to a modern MSVC solution by opening `rts.dsw` in Microsoft Visual Studio .NET 2003, and then opening the newly created project and solution file in MSVC 2015 or newer.

NOTE: As modern versions of MSVC enforce newer revisions of the C++ standard, you will need to make extensive changes to the codebase before it successfully compiles, even more so if you plan on compiling for the Win64 platform.

When the workspace has finished building, the compiled binaries will be copied to the folder called `/Run/` found in the root of each games directory.

### Known Issues

Windows has a policy where executables that contain words “version”, “update” or “install” in their filename will require UAC Elevation to run. This will affect “versionUpdate” and “buildVersionUpdate” projects from running as post-build events. Renaming the output binary name for these projects to not include these words should resolve the issue for you.

### STLport

STLport will require changes to successfully compile this source code. The file [stlport.diff](stlport.diff) has been provided for you so you can review and apply these changes. Please make sure you are using STLport 4.5.3 before attempting to apply the patch.

### Contributing

This repository will not be accepting contributions (pull requests, issues, etc). If you wish to create changes to the source code and encourage collaboration, please create a fork of the repository under your GitHub user/organization space.

### Support

This repository is for preservation purposes only and is archived without support.

</details>

<div align="center">
<sub>

GPL v3 with additional terms, see [LICENSE.md](LICENSE.md).
Preservation release © Electronic Arts.

</sub>
</div>

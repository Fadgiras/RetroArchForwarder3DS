# RetroArch Forwarder Generator

Turns a ROM on your SD card into a HOME menu icon that launches it straight in
**your installed RetroArch**, with your own settings, overrides and saves.
The game stays on the SD card; the forwarder is a small CIA (about 430 KB)
that only starts it.

There are two ways to build one. Both write the same kind of CIA, from the same
template and the same patching code:

| | On the console: `fwdgen.3dsx` | On Windows: RetroArch Forwarder PC |
| --- | --- | --- |
| Where | Homebrew Launcher | PowerShell GUI, no install |
| Result | CIA built **and installed** | CIA saved to the SD, install it with FBI |
| Artwork | libretro thumbnails, SteamGridDB logo | SteamGridDB logos, heroes, grids and icons, local images, libretro title screens |
| Preview | no | banner and icon, live |
| Transparent banner | no | yes |

Both read the titles installed on the SD card and the same
`sdmc:/forwarders/registry.txt`, so they never hand out a title id that is
already taken. See [Safety](#safety).

## Requirements

- **Luma3DS**, and the **Homebrew Launcher title installed** (`hblauncher_loader`,
  title id `000400000D921E00`). The forwarder jumps to it, see below.
- The RetroArch core as a **`.3dsx`**, not as a CIA — for example
  `sdmc:/retroarch/cores/pcsx_rearmed_libretro.3dsx`. Both that directory and
  `sdmc:/3ds/` are scanned. The cores ship inside `RetroArch_3dsx.7z`, one
  archive per release on the
  [libretro buildbot](https://buildbot.libretro.com/stable/).
  Take the archive matching the RetroArch you normally run, rather than the
  nightly cores: mixing a recent core with older assets leaves the bottom
  screen mislabelled. RetroArch states its own version in
  `sdmc:/retroarch/logs/`.
- The game on the SD card. For multi-disc games pick the **`.m3u`**: discs
  can be swapped from RetroArch's *Disc Control* and they share one memory
  card.
- Wi-Fi on the console, if you want the artwork fetched there.

## Usage on the console

Drop `fwdgen.3dsx` into `sdmc:/3ds/` and run it from the Homebrew Launcher.

1. Pick the ROM
2. Pick the core — skipped when the card holds only one
3. The CIA is built and installed; the game appears on the HOME menu

| Key | Action |
| --- | --- |
| D-pad | move |
| A | open folder / select |
| B | go up |
| L / R | previous / next page |
| Y | back to root |
| START | quit |

With a SteamGridDB API key in `sdmc:/forwarders/steamgriddb.txt`, the banner
gets the game's logo, see [SteamGridDB logos](#steamgriddb-logos-optional).

## Usage on Windows

**RetroArch Forwarder PC** is for preparing everything on the PC, choosing
each picture by eye, and only installing the finished CIA. Its window follows
*PS1 Forwarder Creator Plus*: previews on the left, SteamGridDB on the right.

Put these in one folder and double-click `RetroArch Forwarder PC.bat`:

- `RetroArch Forwarder PC.bat`, `RetroArchForwarderPC.ps1` and `LEEME.txt`, from `pc/`
- `fwdgen-pc.exe`, see [Getting fwdgen-pc.exe](#getting-fwdgen-pcexe)
- `template.cia`, from `romfs/`

Then, with the console's SD card in the PC:

1. **SD**: detected by its `Nintendo 3DS` folder. The cores found on it fill
   the *Core* list.
2. **Elegir juego de la SD...**: the game must be on that card, since the
   forwarder opens it from there. Its name fills *Nombre*, and the title id
   it will get is shown with the reason: free, an update of this game's
   forwarder, or moved because another title holds the hashed id.
3. **Artwork**, in order of priority:
   - local files: `logos\<game>.png`, `covers\<game>.png|jpg` (background),
     `icons\<game>.png`;
   - SteamGridDB: with an API key, the game is searched and its *Logos*,
     *Fondos (Heroes)*, *Portadas (Grids)* and *Iconos* appear as thumbnails;
     click one to use it. The first of each can be picked automatically;
   - the libretro title screen, as the background, when nothing else is set.
4. **Banner**: the logo over the background, blurred and dimmed, or the
   background alone. **Banner transparente** keeps only the logo, so the
   HOME menu shows through. The icon always has a background: icons carry
   no alpha.
5. **Crear CIA** saves it, by default to `<SD>\cias\`. Install it with FBI.

Rebuilding a game keeps its title id and raises the title version, so the
HOME menu refreshes the icon when the new CIA is installed over the old one.
The API key is kept in `settings.json`, next to the script, in plain text;
on first run it is taken from a neighbouring *PS1 Forwarder Creator Plus*
folder if there is one. Problems are logged to `log.txt`.

### Getting fwdgen-pc.exe

`fwdgen-pc.exe` is the command line the GUI drives. It is built from the same
`source/` files as the console app, so the CIA is the same byte for byte
given the same pictures. Build it with [zig](https://ziglang.org)
(`pip install ziglang`):

```
sh pc/build.sh
```

or take the `fwdgen-pc` artifact of the *build* workflow, which also bundles
the GUI, `LEEME.txt` and `template.cia`.

It can be used on its own:

```
fwdgen-pc tid   --sd E: --rom "sdmc:/roms/psx/Game.chd"
fwdgen-pc build --sd E: --template template.cia --rom "sdmc:/roms/psx/Game.chd" ^
                --core "sdmc:/retroarch/cores/pcsx_rearmed_libretro.3dsx" ^
                --name "Game" --tid 000400000FF1C100 --icon icon.png ^
                --banner banner.png [--transparent] --out Game.cia
```

`tid` prints the id the game would get; `build` refuses an id that belongs
to another title, then records it in the registry.

## How the forwarder launches the game

**Not** through the APT deliver argument. RetroArch does receive it but never
acts on it — its own *Restart RetroArch* loses the loaded content the same way,
on 1.9.14 as on 1.22.2. That is a RetroArch defect, not something a forwarder
can work around.

So the forwarder calls Luma3DS's **`hb:ldr`**, the very service that makes the
Homebrew Launcher CIA work. It sets the target (the core `.3dsx`) and the
arguments (`argv[0]` the core, `argv[1]` the ROM), then chainloads the Homebrew
Launcher title `000400000D921E00`. Luma's loader recognises that title id and
loads the `.3dsx` with those arguments — the 3dsx loader argv channel, which
does work.

## How the CIA is built

Every forwarder starts from the template embedded in romfs, and only fixed-size
fields are rewritten, so nothing downstream in the file ever moves:

| Field | Marker / location |
| --- | --- |
| ROM path | `FWD0ROM!` in `.code`, 256 reserved bytes |
| Core path | `FWD0CRE!` in `.code`, 256 reserved bytes |
| SMDH titles and icon | `icon` section, always 14016 bytes |
| Banner | `banner` section, oversized to 256 KB on purpose |
| Title id and version | NCCH header, ExHeader, TMD and ticket |

The whole hash chain is then recomputed, leaf to root:

```
sha256(.code)            -> ExeFS header, slots stored in reverse order
sha256(ExeFS header)     -> NCCH 0x1C0
sha256(ExHeader)         -> NCCH 0x160
sha256(NCCH content)     -> TMD content chunk record
sha256(chunk record)     -> TMD info record
sha256(64 info records)  -> TMD 0xA4
```

The RSA signature is not recomputed: custom firmware does not check it.

`.code` is left uncompressed (`EnableCompress: false` in the RSF) so the path
fields can be rewritten in place.

## Artwork

Looked up in this order, for both the icon and the banner:

1. already in `sdmc:/retroarch/thumbnails/<system>/<kind>/<name>.png`
2. otherwise **downloaded** from `thumbnails.libretro.com` and stored there
3. failing that, an image sitting next to the ROM

They are kept where RetroArch expects them, so it benefits too. Without Wi-Fi
the app simply falls back to the card, then to the template.

Two details worth knowing, both learned the hard way:

- The repository answers over **plain HTTP**, so no TLS. The root certificates
  baked into the 3DS are too old for today's sites, and this avoids having to
  disable certificate verification the way many homebrew apps do.
- It **replaces `& * / : ` " < > ? \ |` with an underscore** in file names.
  Checked against its 9339 PlayStation entries: not one contains any of them.
  Without that substitution, *Command & Conquer* is never found.

The system is derived from the core file name, via the `SYSTEMS` table in
`source/fetch.c`, which covers 74 cores. Every system name in it was checked
against the server. Cores carrying their own game — *Anarch*, *Tyrquake*,
*xrick* and the like — are deliberately absent: there is no repository to look
them up in, and the template artwork is kept instead.

A core often plays more than one machine, and the repository keeps each in its
own place — *Tetris* is filed under `Nintendo - Game Boy`, *Pokémon Crystal*
under `Nintendo - Game Boy Color`, never both. Since nothing in a ROM says
which, a core is listed once per system it covers, likeliest first, and each is
tried in turn. Twenty cores have several, `genesis_plus_gx` the most with five.
The system that answered is then reused for the banner, so it never looks
somewhere the icon did not.

### SteamGridDB logos (optional)

With an API key, the banner shows the game's **logo from
[SteamGridDB](https://www.steamgriddb.com)** (section *Logos*: transparent
PNGs) over its title screen, blurred and dimmed. Without a title screen the
logo sits on a dark gradient.

1. Get a key at <https://www.steamgriddb.com/profile/preferences/api>.
2. Put it, alone, in `sdmc:/forwarders/steamgriddb.txt`.

The game is searched by its name without region or disc tags. An exact title
match is taken directly; otherwise a list appears on the bottom screen
(A pick, X search again, B skip). Logos are cached in
`sdmc:/forwarders/logos/`: delete one to fetch it again, or drop your own
PNG there under the game's name to use it offline.

SteamGridDB only accepts modern TLS (ECDHE ciphers, ECDSA certificates), which
the console's SSL service does not offer, so this part uses libcurl and
mbedTLS from the devkitPro portlibs. Certificates are verified against the
Mozilla bundle shipped in `romfs/cacert.pem`
([curl.se](https://curl.se/docs/caextract.html)). The key is sent only to the
API, never to the image CDN. Anything failing falls back to the libretro
artwork.

### Identifying the game

Matching on the file name only goes so far: a ROM called `FF7.pbp` matches
nothing, and no fuzzy matching bridges the gap between that and
`Final Fantasy VII (France) (Disc 1)`. So the disc is asked what it is, in this
order:

1. **its own serial**, mapped to a canonical name through the system index
2. **the file name**, confirmed by an actual fetch rather than assumed
3. **the system keyboard**, filtering that same index

| Format | Where the serial lives | Readable |
| --- | --- | --- |
| pbp | `PARAM.SFO`, first kilobyte | yes |
| bin / cue / iso | `SYSTEM.CNF`, a `BOOT=cdrom:` line in track 1 | yes |
| chd | nowhere, the data is LZMA compressed | no, keyboard |

Neither case needs an ISO9660 parser: a plain string scan finds the boot line.

The name is settled once per game, before the icon and the banner, so the
keyboard never opens twice. When it does open on a pbp it starts pre-filled
with the title the file declares for itself.

A pbp also carries its own `ICON0.PNG`, used as a last resort when the network
is unreachable.

### Adding a system

Indexes live in `romfs/db/<system>.txt`, one line per record:

```
SERIAL|Game Name (Region)
```

The serial is empty for platforms that have none. That still matters: the
keyboard searches the names, so an index without a single serial makes manual
picking work for cartridge systems too.

Only `Sony - PlayStation` ships with the app, since that is what a 3DS runs
best. Add another with:

```bash
python tools/make_index.py "Sega - Mega-CD - Sega CD"
python tools/make_index.py "NEC - PC Engine CD - TurboGrafx-CD"
python tools/make_index.py "Nintendo - Game Boy Advance" --source no-intro
```

`redump` covers disc systems and carries serials, `no-intro` covers cartridge
systems and gives names only. Both come from
[libretro-database](https://github.com/libretro/libretro-database), the same
source RetroArch scans with, so the names line up with the thumbnail
repository. The system name must match a `.dat` there exactly, and the same
string the `SYSTEMS` table in `source/fetch.c` maps your core to.

Each index costs romfs space: PlayStation is 0.5 MB for 10318 serials and
9992 titles. Without an index a system still works, it just falls back to the
file name.

### Banner format

A banner is a **CBMD**: a header pointing at an LZ11-compressed CGFX (offset at
`0x08`) and at the audio (offset at `0x84`). The CGFX ends with the texture:
**256x128 in RGBA4444**, tiled in 8x8 blocks whose pixels follow a Morton
order — the same tiling as icons, which are RGB565.

The app inflates the CGFX, swaps the texture, recompresses and re-appends the
audio with its offset fixed. The LZ11 encoder emits literals only, never back
references: eight times simpler to implement, at the cost of 12.5% in size.
That is why the banner section is reserved at 256 KB, and why each forwarder
weighs about 430 KB.

### Title version

The HOME menu caches icons and only reloads them when the title version
changes. The app therefore reads the installed version with `AM_GetTitleInfo`
and writes the next one, so every rebuild refreshes the display without having
to uninstall first.

## Safety

The `0xFF000-0xFFFFF` range is not reserved for this app: NDS forwarders and
homebrew built with a default RSF install titles there too. So the hashed id is
only a starting point:

- Before choosing, the app lists every title installed on the SD card. If that
  list cannot be read, it stops rather than guess.
- An id that holds a title this app did not build is **never** reused or
  deleted. The game moves on to the next free id, and the screen says so.
  A title counts as ours only when it carries the template's product code.
- `sdmc:/forwarders/registry.txt` records the id each ROM received, one
  `TITLEID|path` line per forwarder. Rebuilding a game updates its own
  forwarder, and an id once given to a game is never handed to another, even
  after that forwarder is uninstalled.
- Forwarders built before the registry existed are recognised by their product
  code and adopted on their next rebuild.
- On Windows the same rules apply, read from the SD card: the installed
  titles are the folders under `Nintendo 3DS\...\title\00040000`. The PC
  cannot read a title's product code (that copy is encrypted), so an
  installed title the registry does not give to this ROM always counts as
  someone else's. `fwdgen-pc build` checks again before writing, and stops
  when no title is found at all: that is the wrong drive.
- Nothing outside the range is ever touched.
- The CIA is written to `sdmc:/forwarders/` **before** being installed, so a
  failed install still leaves a file you can inspect.

## Building

The console app needs devkitPro with devkitARM, libctru and the 3ds portlibs
(curl, mbedtls, jansson, zlib). For the Windows tool, see
[Getting fwdgen-pc.exe](#getting-fwdgen-pcexe).

```
make
```

The template in `romfs/template.cia` is produced by the Python tooling, which
is not part of this repository.

## Tests

Every patching routine is free of 3DS dependencies and compiles on a PC, where
its output is compared byte for byte against a Python reference. That is how
the whole thing was validated before it ever ran on hardware.

```
gcc -O2 -o host/test_filter host/test_filter.c source/romfilter.c
./host/test_filter

gcc -O2 -o host/test_sha host/test_sha.c source/sha256.c
./host/test_sha

gcc -O2 -o host/test_idalloc host/test_idalloc.c source/idalloc.c source/sha256.c
./host/test_idalloc

gcc -O2 -o host/test_icon host/test_icon.c source/icongen.c -lm
gcc -O2 -o host/test_banner host/test_banner.c source/bannergen.c source/icongen.c -lm
gcc -O2 -o host/test_patch host/test_patch.c source/cia_patch.c source/sha256.c

# reads a ROM's serial and resolves it; point INDEX_PATH at the index
gcc -O2 -DINDEX_DIR='"romfs/db"' -o host/test_meta host/test_meta.c source/metadata.c
./host/test_meta "Sony - PlayStation" "some game.pbp"
```

## Limitations

- The banner reservation makes every forwarder about 430 KB.
- Names are reduced to ASCII in the SMDH titles.
- ROM and core paths are limited to 255 bytes each.
- The forwarder targets a **path**, so changing core version means replacing
  that file — nothing to regenerate.

## License

MIT, see [LICENSE](LICENSE).

`source/stb_image.h` is vendored third party code, released by its author into
the public domain (or under MIT, at your option) — either way compatible.

## Credits

Image decoding uses [stb_image](https://github.com/nothings/stb). Artwork comes
from the [libretro thumbnail repository](https://github.com/libretro-thumbnails).

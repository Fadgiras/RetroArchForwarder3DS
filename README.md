# RetroArch Forwarder Generator

A Nintendo 3DS homebrew that turns a ROM on your SD card into a HOME menu icon
launching it straight in RetroArch — box art, banner and all, built and
installed on the console itself.

No PC needed once it is installed.

## Requirements

- **Luma3DS**, and the **Homebrew Launcher title installed** (`hblauncher_loader`,
  title id `000400000D921E00`). The forwarder jumps to it, see below.
- The RetroArch core as a **`.3dsx`**, not as a CIA — for example
  `sdmc:/3ds/pcsx_rearmed_libretro.3dsx`, from `RetroArch_3dsx.7z` on the
  [libretro buildbot](https://buildbot.libretro.com/nightly/nintendo/3ds/).
  Use the **same version** as the RetroArch you normally run: mixing versions
  leaves the bottom screen mislabelled.
- Wi-Fi, if you want the artwork fetched automatically.

## Usage

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
`source/fetch.c` — extend it as you add cores.

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

- Only title ids in the `0xFF000-0xFFFFF` range are ever deleted before
  installing. Nothing outside that range is touched.
- The CIA is written to `sdmc:/forwarders/` **before** being installed, so a
  failed install still leaves a file you can inspect.

## Building

Needs devkitPro with devkitARM and libctru.

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

gcc -O2 -o host/test_icon host/test_icon.c source/icongen.c -lm
gcc -O2 -o host/test_banner host/test_banner.c source/bannergen.c source/icongen.c -lm
gcc -O2 -o host/test_patch host/test_patch.c source/cia_patch.c source/sha256.c
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

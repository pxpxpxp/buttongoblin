# Button Goblin · Stream Deck Mini · v1.1.0

<img src="../docs/assets/button-goblin.webp" width="220" height="220" alt="Button Goblin">

[Download Mini ZIP](https://github.com/pxpxpxp/buttongoblin/releases/download/mini-v1.1.0/ButtonGoblin_Mini_v1_1_0.zip) · [Animated Mini guide](https://buttongoblin.monster/mini.html) · [15-key MK.2 build](../README.md)

A six-key port of the existing native MiSTer launcher. **Update All is on the first page, physical key 4.** The Mini uses its own USB protocol and prepared 80×80 BMP animations. No image decoding happens on MiSTer.

## Mini v1.1.0 · Y / N page

Page 6 adds animated Y and N keys, Back to Home, and Next. Letters turn green while held and release safely when leaving the page. Copy the new `buttons-confirm.conf` together with the updated binary and artwork. These keys target the active MiSTer screen; the background Update All process has separate non-interactive input.

## Run both decks together

Use the **[Dual v1.1.0 bundle and setup instructions](DUAL_README.md)** to install the Mini and 15-key MK.2 together. Run `ButtonGoblin_Both_Enable.sh` after copying the complete bundle.

Mini v1.1.0 uses `buttongoblin-mini`, `ButtonGoblin_Mini_Enable.sh` and `/tmp/buttongoblin_mini.*`.

## Home · Page 1

| 1 · NES | 2 · SNES | 3 · Genesis |
| :---: | :---: | :---: |
| **4 · Update All** | **5 · Brightness** | **6 · Next** |

- Tap Update All to run `/media/fat/Scripts/update_all.sh`. Its running, failed, and finished feedback stays associated with Home even while you browse other pages.
- Tap Brightness for the existing five ascending levels. Hold it for one second to jump to Navigation without changing brightness.
- Press and release Next to cycle through the six pages. Release a held button before using it on the new page.

## Remaining pages

| Page | Top row · 1 / 2 / 3 | Bottom row · 4 / 5 / 6 |
| --- | --- | --- |
| 2 · Consoles | TurboGrafx-16 / PlayStation / Saturn | Nintendo 64 / Mega CD / Next |
| 3 · Consoles | Game Boy Advance / Game Boy / Master System | Neo Geo / The Simpsons / Next |
| 4 · Navigation | Up / Left / Down | Right / Enter / Next |
| 5 · Shortcuts | Menu / Escape / Space | Back to Home / Brightness / Next |
| 6 · Y / N | Y / N / — | Back to Home / — / Next |

The navigation shortcuts have matching green artwork while held. Back retains the fifth-cycle Stay Puft morph and returns Home on release. Next also shows green while pressed. The original startup mosaic is recomposed for the Mini’s 3×2 layout and fades in and out.

## Install

1. Download and extract **ButtonGoblin_Mini_v1_1_0.zip**.
2. Copy the package’s complete **Scripts contents**, including hidden **.config**, into `/media/fat/Scripts/`.
3. Connect your Elgato Stream Deck Mini to MiSTer’s USB host hub with a data-capable connection.
4. Run **ButtonGoblin_Mini_Enable.sh** from MiSTer’s Scripts menu. The service starts immediately and at boot.

**Update All itself is installed separately.** This package installs its first-page button and launcher integration, and expects the updater at `Scripts/update_all.sh`. It does not download or replace your updater. Cores, games, favorites, and BIOS files are also installed separately.

The runtime remains under `/media/fat/Scripts/.config/buttongoblin-mini/`. The Mini has its own services, boot block and logs, so it can run alongside a 15-key MK.2. For both devices, use the [combined dual-deck package](DUAL_README.md), which also coordinates Update All between both launchers.

## Configuration and diagnostics

The six files are `buttons.conf`, `buttons-page2.conf`, `buttons-page3.conf`, `buttons-navigation.conf`, `buttons-shortcuts.conf`, and `buttons-confirm.conf`. Within each page, `key.0` is physical button 1 and `key.5` is button 6. The letter actions are `y` and `n`; they send keyboard presses/releases to the active MiSTer screen and do not supply stdin to background Update All. The additional actions are `next` (cycle pages) and `back` (Home).

**Do not export Mini configurations from the 15-key web editor.** It produces MK.2 layouts and JPEG animations. Edit the Mini configuration files directly for this release; keep a Next or Back action available.

Prepared `.sma` files contain Mini BMP frames. They are not compatible with MK.2 `.sda` files. Original 72×72 character art is centered inside an 80×80 canvas, preserving the existing pixel geometry. Playback uses at least 350 ms per frame to limit USB traffic; slow uploads naturally reduce the frame rate without accumulating frames.

Run `extras/ButtonGoblin_Mini_Diagnostics.sh` over SSH for connection, worker, and updater logs. A missing updater or bad game path is reported through the existing failure feedback. The service retains supervised restart, USB reconnection, held-key rearming, queue clearing on page changes, and bounded image caches.

All six page configurations must load successfully. A failed reload keeps the complete previous configuration set. Update All runs once at a time; Home shows its completion when you return.

## Build and verification

GitHub Actions restores the existing v1 build inputs, generates the current character/control artwork, converts it to Mini format, runs the Mini tests, cross-compiles a static Cortex-A9 ARM binary with Zig 0.13.0, and publishes the ZIP and SHA-256 checksum under the **mini-v1.1.0** release.

The standalone native source is [buttongoblin-mini.c](buttongoblin-mini.c). `prepare_mini.py` prepares the package; `test_mini.c` and `test_package.py` check the protocol, paging, feedback, cache validation, and first-page updater mapping. The packaged source can be rebuilt with `sh source/build.sh` after installing Zig on a computer.

Supported Mini USB product IDs: `0063`, `0090`, `00B3`, and `00B8`, vendor `0FD9`. The binary rejects unrelated Stream Deck models.

**Hardware validation:** native button input has been user-confirmed on a physical Mini connected to MiSTer. Host regression and sanitizer tests also pass. Orientation, shared-hub operation and complete updater runs require checks on your hardware.

## Performance and shared USB hubs

Changed button states take priority over routine animation frames; an in-flight BMP finishes first because the Mini shares its image buffer. Consecutive identical frames are merged without changing animation timing, saving 115,572 bytes in this build. Script completion uses SIGCHLD plus a 100 ms fallback, rather than checking once per USB packet.

Only USB devices with vendor `0FD9` and a supported Mini product ID receive write access. Other HID devices are probed read-only for identity; the launcher never resets the hub, detaches drivers, or grabs other input devices. A per-device advisory lock prevents cooperating launcher instances from writing to the same Mini. Other applications must also honor the lock to cooperate.

Only the visible page animates. Each image uses 20 reports of 1,024 bytes, sent one report at a time with at least 1 ms between successful writes, with input handled between writes. Frame delays are at least 350 ms after an upload completes. USB topology, power, kernel messages, and image-transfer counters are available in Diagnostics. These bounds reduce avoidable traffic; they cannot guarantee shared-hub performance or adequate power on your hardware.

## Stability and performance fixes

- Prevent idle CPU spinning with custom hold actions or a page transition.
- Suppress simultaneous held keys across page changes and clear stale green shortcut feedback.
- Keep script feedback on its owning page, including custom mappings; retain updater results across USB reconnection.
- Validate all six pages when using a relative `buttons.conf` path.
- Retry virtual keyboard creation after a failed connection.
- Rebuild the Mini when shared artwork preparation inputs change.

## Protocol and credits

- [Elgato Mini HID protocol](https://docs.elgato.com/streamdeck/hid/mini/)
- [Upstream python-elgato-streamdeck Mini implementation](https://github.com/abcminiuser/python-elgato-streamdeck/blob/master/src/StreamDeck/Devices/StreamDeckMini.py) and [PIL image transform](https://github.com/abcminiuser/python-elgato-streamdeck/blob/master/src/StreamDeck/ImageHelpers/PILHelper.py), used to cross-check report framing and native BMP orientation.
- Existing launcher, character artwork, and attribution are carried forward from the v1 package. See the included **LICENSE.txt** and **ARTWORK_CREDITS.txt**.

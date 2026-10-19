# Button Goblin · Dual v1.0.3

<img src="../docs/assets/button-goblin.webp" width="220" height="220" alt="Button Goblin">

Run a six-key Stream Deck Mini and a 15-key Stream Deck MK.2 together on one MiSTer. Both stay active, with independent pages, artwork and brightness. Update All is on the Mini's first page, physical key 4, and on the MK.2's first page, physical key 14.

**[Download the combined ZIP](https://github.com/pxpxpxp/MiSTer_StreamDeck/releases/download/dual-v1.0.3/MiSTer_StreamDeck_Dual_v1_0_3.zip)** · [Mini guide](https://pxpxpxp.github.io/MiSTer_StreamDeck/mini.html) · [15-key guide](https://pxpxpxp.github.io/MiSTer_StreamDeck/)

## v1.0.3 · Mini Y / N page

Mini v1.0.5 adds a sixth page with animated Y/N keyboard keys, Back to Home and Next. Held keys release on page changes. The MK.2 layout is unchanged. Y/N target the active MiSTer screen, not background Update All stdin.

## Previous v1.0.2 input fix

Mini v1.0.4 fixes a reproducible input-blocking loop during slow animated page changes and reads the seven-byte Mini button prefix. Both runtimes finish the initial page paint before resuming animations. Mini diagnostics include input counters, input-blocking state, and a bounded sample of presses and malformed reports.

## Previous simplification changes

- Direct, reviewable native sources for both devices; no generated C patches in the Dual build.
- Unified hold rules fix custom hold conflicts and unnecessary scheduler wakeups.
- Script results update their owning page directly; held shortcut feedback clears on page changes.
- Both-deck enable/disable scripts attempt both operations and report failures.
- Shared release definitions keep package folders, ZIP names, guide links and Actions outputs consistent.

## Install both

1. Wait for any Update All job to finish. Run the installed **StreamDeck_Disable.sh** before replacing files. If a separate Mini service is installed, disable that too. Back up custom configurations.
2. Extract **MiSTer_StreamDeck_Dual_v1_0_3.zip**. Copy its **Scripts contents**, including hidden **.config**, into `/media/fat/Scripts/`. Replace each binary, configuration and artwork set together.
3. Plug both decks into MiSTer's USB host hub.
4. Run **StreamDeck_Both_Enable.sh** from the Scripts menu. Both services start now and at boot.

The combined ZIP includes both native ARM binaries. The old Mini v1.0.0/v1.0.1 used the same directory as the MK.2; disabling that old service before copying is essential. Its directory becomes the MK.2 installation again. Restore custom Mini mappings only into `streamdeck-mini`, checking the six-key page layout.

| Device | Installed folder | Enable / disable | Runtime and logs |
| --- | --- | --- | --- |
| 15-key MK.2 | `Scripts/.config/streamdeck/` | `StreamDeck_Enable.sh` / `StreamDeck_Disable.sh` | `/tmp/mister_streamdeck.*` |
| Six-key Mini | `Scripts/.config/streamdeck-mini/` | `StreamDeck_Mini_Enable.sh` / `StreamDeck_Mini_Disable.sh` | `/tmp/mister_streamdeck_mini.*` |

**StreamDeck_Both_Disable.sh** stops both and removes both boot entries. Either device can also be enabled, disabled or reloaded independently. Enabling or disabling the Mini preserves the MK.2's files and boot entry. The Mini extras use `StreamDeck_Mini_` names within its own `extras` folder.

## Using both

- Each launcher accepts only its own supported USB device family. Neither grabs other input devices or resets the USB bus. Per-device advisory locks prevent cooperating launcher instances from sharing a deck.
- Pages and brightness remain independent. Both decks control the same MiSTer: a core launch from either deck changes the running core; keyboard shortcuts from either affect the current menu.
- Both included binaries share an Update All lock. If Update All is running on one deck, another updater request is rejected and logged; retry after it finishes. Feedback remains on the deck that started the job. This lock coordinates these two launchers; a manually started updater does not participate.
- Install Update All separately at `/media/fat/Scripts/update_all.sh`. Game, core and BIOS files also remain separate installs.
- Each visible page animates. Two decks add USB traffic and power demand; physical coexistence, hub power and controller responsiveness still need checking on your setup.

The web editor targets the MK.2 only. Edit Mini configurations in its separate folder. The Mini uses BMP animation frames, while the MK.2 uses JPEG frames; do not mix exports.

## Checks and diagnostics

The Actions build tests both launchers, runs them concurrently on a host, verifies independent shutdown and boot blocks, tests Update All exclusion in both directions, and builds both static Cortex-A9 ARM binaries. Host checks do not replace testing with both physical devices attached.

For SSH diagnostics run `Scripts/.config/streamdeck/extras/StreamDeck_Diagnostics.sh` for MK.2 and `Scripts/.config/streamdeck-mini/extras/StreamDeck_Mini_Diagnostics.sh` for Mini. The Mini report includes USB topology, power descriptors, kernel messages and image-transfer counters.

Original attribution is included in **LICENSE.txt** and **ARTWORK_CREDITS.txt**.

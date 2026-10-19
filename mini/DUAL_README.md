# Button Goblin · Dual v2.0.0

<img src="../docs/assets/button-goblin.webp" width="220" height="220" alt="Button Goblin">

Run a six-key Stream Deck Mini and a 15-key Stream Deck MK.2 together on one MiSTer. Both stay active, with independent pages, artwork and brightness. Update All is on the Mini's first page, physical key 4, and on the MK.2's first page, physical key 14.

**[Download the combined ZIP](https://github.com/pxpxpxp/buttongoblin/releases/download/dual-v2.0.0/ButtonGoblin_Dual_v2_0_0.zip)** · [Mini guide](https://buttongoblin.monster/mini.html) · [15-key guide](https://buttongoblin.monster/)

## Install both

1. Download and extract **ButtonGoblin_Dual_v2_0_0.zip**.
2. Copy its complete **Scripts contents**, including hidden **.config**, into `/media/fat/Scripts/`.
3. Connect both decks to MiSTer’s USB host hub.
4. Run **ButtonGoblin_Both_Enable.sh** from MiSTer’s Scripts menu. Both services start immediately and at boot.

The combined ZIP includes both Button Goblin native ARM binaries and independent service controls.

| Device | Installed folder | Enable / disable | Runtime and logs |
| --- | --- | --- | --- |
| 15-key MK.2 | `Scripts/.config/buttongoblin/` | `ButtonGoblin_Enable.sh` / `ButtonGoblin_Disable.sh` | `/tmp/buttongoblin.*` |
| Six-key Mini | `Scripts/.config/buttongoblin-mini/` | `ButtonGoblin_Mini_Enable.sh` / `ButtonGoblin_Mini_Disable.sh` | `/tmp/buttongoblin_mini.*` |

**ButtonGoblin_Both_Disable.sh** stops both and removes both boot entries. Either device can also be enabled, disabled or reloaded independently. Enabling or disabling the Mini preserves the MK.2's files and boot entry. The Mini extras use `ButtonGoblin_Mini_` names within its own `extras` folder.

## Using both

- Each launcher accepts only its own supported USB device family. Neither grabs other input devices or resets the USB bus. Per-device advisory locks prevent cooperating launcher instances from sharing a deck.
- Pages and brightness remain independent. Both decks control the same MiSTer: a core launch from either deck changes the running core; keyboard shortcuts from either affect the current menu.
- Both included binaries share an Update All lock. If Update All is running on one deck, another updater request is rejected and logged; retry after it finishes. Feedback remains on the deck that started the job. This lock coordinates these two launchers; a manually started updater does not participate.
- Install Update All separately at `/media/fat/Scripts/update_all.sh`. Game, core and BIOS files also remain separate installs.
- Each visible page animates. Two decks add USB traffic and power demand; physical coexistence, hub power and controller responsiveness still need checking on your setup.

Use the [MK.2 editor](https://buttongoblin.monster/editor/) or the [Mini editor](https://buttongoblin.monster/editor/mini.html) for the matching device. Both support saved projects, animated artwork and configuration exports. The Mini uses BMP animation frames, while the MK.2 uses JPEG frames; do not mix exports.

## Checks and diagnostics

The Actions build tests both launchers, runs them concurrently on a host, verifies independent shutdown and boot blocks, tests Update All exclusion in both directions, and builds both static Cortex-A9 ARM binaries. Host checks do not replace testing with both physical devices attached.

For SSH diagnostics run `Scripts/.config/buttongoblin/extras/ButtonGoblin_Diagnostics.sh` for MK.2 and `Scripts/.config/buttongoblin-mini/extras/ButtonGoblin_Mini_Diagnostics.sh` for Mini. The Mini report includes USB topology, power descriptors, kernel messages and image-transfer counters.

Original attribution is included in **LICENSE.txt** and **ARTWORK_CREDITS.txt**.

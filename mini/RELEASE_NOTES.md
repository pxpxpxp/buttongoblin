Mini v1.0.5 — animated Y / N page

- Add page 6: Y / N / blank, Back to Home / blank / Next.
- Y and N send native keyboard press/release events, animate, and turn green while held.
- Release held keys on page changes; Next wraps through all six pages.
- Keep Update All on Home/key 4 and preserve the v1.0.4 button fixes.
- Update the Mini guide, page validation and input regression tests.

The Dual v1.0.3 bundle includes Mini v1.0.5. The MK.2 layout is unchanged.

Y/N keys type into the active MiSTer screen; they do not feed the background Update All process, which runs with non-interactive input. This release does not diagnose or fix the separately reported Update All failure.

Disable the service and back up custom settings before replacing all Scripts contents, including hidden .config. Copy the new buttons-confirm.conf together with the new binary and artwork, then enable the service. New page hardware behavior needs confirmation; the previous v1.0.4 button response was user-confirmed on 2026-10-17.

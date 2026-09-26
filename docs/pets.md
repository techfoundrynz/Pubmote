# Pets

Menu → Pets opens the pet picker. Enable/disable the installed pet offline, or
browse the live public catalog from [codex-pets.net](https://codex-pets.net).
The gallery shows one large animated pet at a time. Swipe horizontally (or use
the chevrons at the screen edges) to browse pets, tap the preview to cycle through its nine
animations, then Apply to enable it. Browsing never changes the installed pet.
Swipes and chevrons remain responsive while loading: the page counter advances immediately,
and rapid taps are coalesced after 150 ms of inactivity. Only the latest destination
is loaded; superseded downloads stop between reads and cannot replace the current
preview or show stale errors. A pending network operation may finish before the
worker can cancel it. Navigation is disabled while Apply writes the selected pet.
The gallery footer has Exit and Apply buttons; Retry appears only when loading fails.
The preview uses the freed header and status space once loaded. The stats pet is
centered at 55% of the display's shorter edge, behind all text and dials at 40%
opacity to keep readings clear. It reuses the same atlas without a larger image allocation.
Between pets, the gallery shows a loading state. The installed pet is only shown
before entering online browsing. Exit returns to the menu offline, or restarts
the remote after browsing to restore board communications.
Navigation and status stay fixed on screen. The gallery and main-screen pet animate
in Slint. The main-screen pet uses idle frames and the running-right row when moving. Pocket mode
pauses animation, and a board warning hides the overlay.

Browsing asks the rider to disconnect board communications and use configured
Wi-Fi. Leaving browsing restarts the remote to restore communications, matching
the updater's existing lifecycle. Merely opening Pets or toggling an installed
pet does not disconnect the board. The ESP-NOW handoff retains the Wi-Fi driver
and its internal SRAM buffers, avoiding a large reallocation on a fragmented
heap. BLE is fully deinitialized before Wi-Fi starts.

## Deployment

The shared `API_BASE_URL` in `firmware/src/config.h` defaults to
`"https://api.pubmote.com"`. For local development, override it with your server
origin, for example `"http://192.168.31.248:8791"`. Keep this local override out of
commits. Do not include a trailing slash; the pet client appends `/pets/`.
Start the local adapter with:

```sh
pnpm --filter @pubmote/api exec wrangler dev --local --ip 127.0.0.1 --port 8792
```

In a second terminal, expose the pet routes on LAN port 8791:

```sh
node api/scripts/pets-lan-proxy.mjs
```

This proxy avoids an unresponsive Wrangler wildcard listener observed on the
Windows development host. The firmware URL and firewall port remain 8791.

The remote still needs its saved Wi-Fi credentials and LAN access to that
computer. Update the URL if the computer's LAN IP changes. HTTP is supported for
this local endpoint; HTTPS endpoints continue to verify certificates. Set
`API_BASE_URL` to `"https://api.pubmote.com"` and rebuild to use production again.

If Windows blocks LAN access, run this once in an administrator PowerShell
while the server is running (adjust `LocalAddress` if the computer's IP changes):

```powershell
New-NetFirewallRule -Name PubRemote-Pets-Dev-8791 -DisplayName 'PubRemote Pets development' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8791 -LocalAddress 192.168.31.248 -RemoteAddress LocalSubnet -Profile Any
```

Remove the temporary exception after development:

```powershell
Remove-NetFirewallRule -Name PubRemote-Pets-Dev-8791
```

Deploy the API worker before using downloads on the remote:

```sh
pnpm --filter @pubmote/api deploy
```

This adds `/pets/catalog?page=1&pageSize=1` and `/pets/<id>.pet` to `api.pubmote.com`.
The gallery loads one full atlas into RAM so every animation can be previewed.
Apply saves those same validated bytes without a second network request; flash
writes happen only after Apply. Each page releases the previous preview before
loading its replacement. The legacy four-item catalog and compact `.preview`
endpoint remain available for older firmware. One persistent download task
reuses its stack across requests.
The adapter accesses only public codex-pets.net metadata and sprite assets; no
account or API key is required. It uses Photon WASM to decode and resize the
original sheets, and caches successful responses. Conversion requires more CPU
than the [Workers Free plan's 10 ms request allowance](https://developers.cloudflare.com/workers/platform/limits/); use a Worker plan/CPU limit
that accommodates conversion. Local conversions were tested, but production
CPU usage must be checked after deployment. Upstream API changes can require an
adapter update.

## Storage and format

Both 1536×1872 v1 and 1536×2288 v2 input sheets produce the same 384×468
straight-alpha RGBA atlas. The nine common rows are retained; the extra two v2
look-around rows are omitted. Cells are 48×52 pixels, eight columns per row.
The gallery cycles through idle, running-right, running-left, wave, jump, failed,
waiting, working and review at 260 ms per frame. The main screen plays idle
(six frames) and running-right (eight frames). No WebP decoder or custom drawing is needed in firmware.

The download contains a 16-byte header followed by 718,848 RGBA bytes:

| Offset | Value |
| --- | --- |
| 0 | Eight bytes `PMPET01\0` |
| 8 | Little-endian uint16 cell width, 48 |
| 10 | Little-endian uint16 cell height, 52 |
| 12 | Little-endian uint16 row count, 9 |
| 14 | Little-endian uint16 interval in milliseconds, 260 |

The existing 2 MB LittleFS partition holds alternating slots. Downloads stream
into a fixed-size RAM buffer. Apply writes a temporary file, then replaces the
inactive slot. The NVS selection changes only after validation and a successful
file write.
Interrupted or failed downloads preserve the active slot. Only a completely
erased partition is initialized automatically; an unmountable existing
filesystem is left intact. Each loaded atlas needs approximately 702 KiB of
heap, typically PSRAM. Browsing also holds the validated 702 KiB download buffer
and, if installed, the current main-screen atlas. Applying reuses the preview image.

The MCU font embeds a limited character set, so names requiring other glyphs
use the pet's catalog ID. Originals and creator information remain on the source
site.

## Validation

```sh
pnpm --filter @pubmote/api test:pets
pnpm --filter @pubmote/api typecheck
pnpm --filter @pubmote/api lint
pnpm --filter @pubmote/api exec wrangler deploy --dry-run
pio run -e pingumote_esp32s3_touch_amoled_132
```

API tests require Node 24 and cover both sheet versions, frame boundaries,
alpha, invalid dimensions/routes, pagination, upstream failures and allowed
sprite origins. Real Pingu v1 and Thrall v2 downloads were also exercised through
the local Worker. On-device checks still needed: visual placement on round and
rectangular panels, Wi-Fi handoff/restart, enable/disable persistence, and power
loss during a replacement download.

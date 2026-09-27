# Pubmote Firmware Tool

This tool is for end-user debugging and flashing of the Pubmote firmware, hosted on GitHub pages

## Getting-started guide

Edit `docs/quick-start.md` in the repository root. Vite renders it to
`dist/getting-started/index.html` and copies its screenshots into the Pages build.
The same page is served by `pnpm dev` at `/getting-started/`.

`/getting-started/?tag=v1.2.3` loads the Markdown and relative screenshots from
that Git tag through `raw.githubusercontent.com`. Release tags must be retained
and not moved. No archive rebuild is needed after a release. Without a tag, the
static page contains the current guide and works without JavaScript. A missing
tag/file or network error shows an explicit error with links to GitHub and the
current guide; it never silently substitutes current instructions.

The firmware release workflow sets `PUBMOTE_GUIDE_TAG` to the actual release tag.
Other builds use the Git commit SHA, avoiding the mutable `nightly` tag. A build
from a source archive without Git falls back to the unversioned URL; set
`PUBMOTE_GUIDE_TAG` explicitly to pin it. Unpublished commits cannot be fetched.
Existing firmware released before this feature cannot acquire the new QR link.

`prebuild_hook.py` installs the pinned, host-only `qrcode==8.2` encoder into
`.pio/guide-deps/` on first use and writes a packed matrix to the board build
directory. Slint receives a shared pixel buffer at startup and scales it with
nearest-neighbor sampling at integer module sizes. No QR library runs on-device.

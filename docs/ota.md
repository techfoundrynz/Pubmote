# OTA release discovery

Firmware requests `GET https://api.pubmote.com/ota/v1/releases?board=<HW_TYPE>`.
The v1 response contains `stable`, `prerelease`, and `nightly`, each either
`null` or `{ "tag": "...", "url": "https://..." }`. Downloads still use public
GitHub release assets directly. The Worker uses GitHub's `@octokit/graphql`
client for request serialization and HTTP/GraphQL error handling. Board names match the exact asset prefix
`<board>-`, and only `.bin` assets qualify.

Release selection is unchanged: GitHub's latest release is stable; the two
most recently created releases are searched for the first matching non-nightly
release and the mutable `nightly` tag. Nightly publishing and firmware version
comparison are unchanged. The Worker caches GitHub metadata for 60 seconds
across all boards, so new releases may take up to a minute to appear.

## Rollout

1. The `Cloudflare Worker` GitHub Actions workflow copies
   the repository's `RELEASES_AUTH_TOKEN` secret into the Worker before deploying.
   It uses the existing `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` secrets.
   Checks run on relevant pull requests and pushes to `master`, or manually.
   Deployment runs only from `master`, after API and test type checks, lint,
   formatting, Vitest tests, and a Worker bundle check. PR checks need no secrets.
   Deployments are serialized. The credential is
   a Worker secret, never a firmware build flag or bundled variable.
2. Push the changes to `master` or run that workflow, then check the manifest for a supported
   board before distributing the new firmware. A missing secret returns 503;
   upstream failures return 502; missing assets are represented by null channels.
3. Build/distribute firmware. After allowing devices to migrate, replace the
   repository secret with a new GitHub token with the minimum required read
   permissions and rerun the Worker workflow before revoking the embedded token.
   Old firmware depends on that token for discovery;
   revoking it earlier requires users to update through the USB firmware tool.

For local/manual deployment, configure the Worker secret from `api` using
`pnpm exec wrangler secret put RELEASES_AUTH_TOKEN`, then `pnpm run deploy`.

Run `pnpm test` and `pnpm run typecheck` in `api` to validate the service.
Tests are TypeScript Vitest tests; `pnpm run test:watch` runs them during development.

## Memory and certificates

The manifest response buffer is bounded at 2 KiB (previously 32 KiB), prefers
PSRAM, and rejects overflow. HTTP/TLS is cleaned up before JSON parsing.
Manifest HTTP RX/TX buffers are 1 KiB/512 bytes; binary OTA RX/TX buffers are
4 KiB/1 KiB (previously 8 KiB/4 KiB). Failed OTA sessions are aborted to release
their allocations. These are allocation changes, not measured peak heap savings.

TLS still verifies certificates using the ESP certificate bundle, including
GitHub's download redirects. Reducing the bundle chiefly saves flash; pinning
a server leaf certificate would introduce certificate-renewal failures.
Partial OTA downloads plus a smaller mbedTLS receive buffer can save additional
RAM, but require testing the GitHub range responses and every other HTTPS client
before changing the shared TLS configuration. No TLS settings are changed here.

## Public endpoint protections

The manifest remains public, matching the public GitHub binaries. Cloudflare's
`OTA_RATE_LIMITER` binding permits 60 checks per public IP per minute at each
Cloudflare location. Exceeding it returns 429 with `Retry-After: 60`, before
cache/GitHub access. This is an approximate local abuse limit, not a global
quota or device authentication. A limiter failure returns an error.

Only one `board` query parameter is accepted. Asset URLs must be canonical
HTTPS GitHub URLs in this repository, have no credentials/query/fragment, and
match the selected asset filename. The serialized manifest must fit the
firmware's 2 KiB buffer. Firmware also checks the initial download URL against
the repository and `.bin` extension before opening an OTA session. GitHub
redirects continue to use TLS certificate verification.

## Signed firmware rollout (not enabled)

Firmware signing remains disabled. HTTPS and URL validation do not replace
cryptographic firmware authentication. ESP-IDF supports signed OTA verification
without hardware Secure Boot, which is the recommended next step for this fleet:
<https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/security/secure-boot-v2.html#signed-app-verification-without-hardware-secure-boot>

Before enforcement, provision and back up a persistent private signing key in
the release pipeline, sign stable and nightly images, and test a migration
release on hardware. Verify that valid signed updates succeed, modified and
unsigned images fail, and USB recovery still works. Existing unsigned releases
will no longer be installable by a device enforcing signatures. Keep hardware
Secure Boot/eFuse changes out of this OTA migration. No signing keys were
generated and no signature-enforcement settings were changed in this update.

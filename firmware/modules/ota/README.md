# Firmware update client

Ownership: firmware release and connectivity maintainers.

Public API: `ota/update_client.h`. Owns release fetching, URL/image validation,
download, flash writes, and update cancellation checks. It does not own UI,
radio-session acquisition, user confirmation, or reboot policy.

Runtime: operations block and must run on a maintenance worker with an internal
stack suitable for flash operations. The caller owns request/cancellation state
for the entire call. Progress callbacks execute on that worker and must not
touch Slint. The application owns cancellation/commit arbitration and restoration
of the board transport. Retain image verification and HTTPS downgrade rejection.

Validation: OTA download and commit/cancellation host tests, firmware build, and
cancelled/failed/successful update recovery on hardware when changing behaviour.

# Board protocol

Ownership: connectivity maintainers, with receiver/tool compatibility review for
command or wire-format changes.

Owns command identifiers, packet sizes, endian conversion and packet codecs.
It has no device, settings, transport, clock, UI or scheduler dependency.
Callers supply an expected pairing secret and publish decoded data only after
successful validation. Output arguments remain unchanged when decoding fails.
Encoders return the written length, or zero for missing arguments/insufficient
capacity without changing the output. Buffers and decoded values belong to the
caller; the codec retains nothing and may run concurrently on any task.

TX retains the ESP receiver ABI: little endian secret and IEEE float input axes,
three one-byte flags and the trailing reserved byte. Board telemetry and pairing
secrets are big endian; trip distance is an IEEE float in little endian. Command
IDs and legacy receiver-version length handling are retained. Host golden-byte
tests cover these layouts; application tests cover publish/timer side effects.

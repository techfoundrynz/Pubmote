# Game storage

Ownership: game distribution and storage maintainers.

Public API: `games/game_store.h`. Owns the LittleFS mount, installed game files,
metadata validation and game-upload console commands. It does not own game
execution, Lua, Slint, or input routing.

Runtime: filesystem access is serialized by the store's mutex. File operations
can block. `game_store_read` returns an allocated source buffer that the caller
must free. Do not format an existing filesystem when mounting fails.

Validation: game/upload tests, firmware build, and install/list/read checks on
hardware for filesystem changes.

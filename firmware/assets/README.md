# Firmware assets

The firmware and simulator use `Saira-SemiBold.ttf`. Its digits have been adjusted
to a common advance width for numeric displays.

`Saira-SemiBold.original.ttf` is the preserved input to `modify_font.py`, not a
build artifact. Run `python firmware/assets/modify_font.py` with `fonttools`
installed to regenerate the runtime font. The script always reads the original
and writes `Saira-SemiBold.ttf`; keep the original unchanged.

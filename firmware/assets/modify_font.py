import os
import shutil
from fontTools.ttLib import TTFont

# Get script's directory
script_dir = os.path.dirname(os.path.abspath(__file__))
font_path = os.path.join(script_dir, "Saira-SemiBold.ttf")
backup_path = os.path.join(script_dir, "Saira-SemiBold.ttf.bak")

# Create a backup of the original font if not already backed up
if not os.path.exists(backup_path):
    shutil.copyfile(font_path, backup_path)
    print("Created font backup at:", backup_path)
else:
    print("Backup already exists.")

# Load font
font = TTFont(backup_path)
hmtx = font['hmtx']
glyf = font['glyf']

digits = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine']
W_target = 682

print("Converting digits to tabular-nums...")
for d in digits:
    if d in hmtx.metrics and d in glyf:
        glyph = glyf[d]
        
        # Ensure bounds are calculated
        if not hasattr(glyph, 'xMin') or glyph.xMin is None:
            glyph.recalcBounds(glyf)
            
        xMin = getattr(glyph, 'xMin', 0)
        xMax = getattr(glyph, 'xMax', 0)
        outline_width = xMax - xMin
        
        # Calculate new centered lsb
        lsb_target = int((W_target - outline_width) / 2)
        shift_x = lsb_target - xMin
        
        print(f"Glyph '{d}': shifting by {shift_x} units, new lsb={lsb_target}")
        
        # Shift outline
        if hasattr(glyph, 'coordinates'):
            glyph.coordinates.translate((shift_x, 0))
        elif hasattr(glyph, 'components'):
            for comp in glyph.components:
                comp.x += shift_x
        
        # Recalculate bounds after shifting
        glyph.recalcBounds(glyf)
        
        # Update hmtx metrics
        hmtx.metrics[d] = (W_target, lsb_target)

# Save the modified font to the original path
font.save(font_path)
print("Font saved successfully to:", font_path)

"""Regenerate Whack's lossless package textures from the original SVGs.

Development-only dependencies: Slint viewer 1.18.1 and Pillow.
Set SLINT_VIEWER to override the viewer executable path.
No SVG decoder is needed on the remote.
"""
import base64
from pathlib import Path
import zlib
import subprocess
import os

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
START = '-- BEGIN GENERATED ORIGINAL ART'
END = '-- END GENERATED ORIGINAL ART'


def main():
    # Capture opaque scenery patches. This preserves Slint's SVG rasterization
    # and compositing exactly without storing a whole-screen framebuffer.
    patches = ((70,8,6,6),(5,21,18,44),(77,38,17,13),(83.5,18.6,9,17),
               (11.5,64.6,9,17),(79,58,13,21),(24,85,3.5,3),(70,83,3.5,3),
               (23,19,3.5,3),(76,78,3.5,3),(6,64,3.5,3),(9,64,4,5),
               (20,80,4,5),(65,86,4,5),(81,80,4,5))
    display_heights = {240:240,280:456,410:502,466:466}
    viewer = Path(os.environ.get('SLINT_VIEWER', ROOT/'.pio/game-test-tools/slint-viewer/slint-viewer.exe'))
    work = ROOT/'.pio/game-assets';work.mkdir(parents=True,exist_ok=True)
    # Original scenery geometry remains in Git, not in the firmware source tree.
    revision = 'b6074accb7b432f6a82711d7bfc8f5389e855d76'
    result = subprocess.run(['git', 'show', f'{revision}:firmware/src/slint/ui/whack.slint'],
                            cwd=ROOT, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'Fetch Git history containing {revision} to regenerate artwork')
    original = result.stdout.decode('utf-8')
    start = original.index('component WhackScenery')
    end = original.index('export component WhackScreen', start)
    scenery_source = 'export ' + original[start:end]
    scenery_source = scenery_source.replace('../assets/whack/', (ROOT/'games/assets/whack').as_posix()+'/')
    source = work/'original-scenery.slint'
    source.write_text(scenery_source)
    lines = [START, 'local function load_art()', ' local width=game.screen()']
    for display in (240,280,410,466):
        lines.append((' if' if display==240 else ' elseif')+f' width=={display} then' if display!=466 else ' else')
        height=display_heights[display]
        scene=work/'scenery.slint';png=work/'scenery.png'
        scene.write_text(f'import {{ WhackScenery }} from "{source.as_posix()}"; export component Asset inherits Window {{width:{display}px;height:{height}px;background:#16301c;WhackScenery {{width:100%;height:100%;}}}}')
        subprocess.run([str(viewer),'--screenshot',str(png),str(scene)],check=True,env={**os.environ,'SLINT_BACKEND':'software','SLINT_SCALE_FACTOR':'1'})
        scenery=Image.open(png).convert('RGBA')
        total=0
        for index,(x,y,w,h) in enumerate(patches):
            x,y,w,h=int(x*display/100+.5),int(y*height/100+.5),int(w*display/100+.5),int(h*height/100+.5)
            pixels=scenery.crop((x,y,x+w,y+h));total+=w*h*4
            data = base64.b64encode(zlib.compress(pixels.tobytes(), 9)).decode()
            lines.append(f' game.texture({index},{w},{h},"{data}")')
        assert total<=256*1024
    lines.append(' end')
    lines.extend(['end', END])
    path = ROOT / 'games/whack.lua'
    source = path.read_text()
    if START in source:
        source = source[:source.index(START)] + '\n'.join(lines) + source[source.index(END) + len(END):]
    else:
        header, body = source.split('\n', 1)
        source = header + '\n' + '\n'.join(lines) + '\n' + body
    path.write_text(source, newline='\n')


if __name__ == '__main__':
    main()

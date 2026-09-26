"""Play the installable Lua games on a PC. Requires lupa and Python's tkinter."""
import argparse
import base64
import json
import math
import zlib
from functools import lru_cache
from collections import deque
from pathlib import Path
import time

from lupa.lua54 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


class GameHost:
    """The firmware's small drawing/input API, with a bounded Lua VM."""

    def __init__(self, path, best=0, width=466, height=466, square=False):
        self.commands = []
        self.rng = 42
        self.screen = (width, height, square, True, True)
        self.textures = {}
        self.texture_bytes = 0
        self.effects = deque(maxlen=256)
        self.best = best
        self.feedback = ""
        self.repeat_delay, self.repeat_interval = 0, 0
        self.lua = LuaRuntime(unpack_returned_tuples=True, max_memory=256 * 1024,
                              register_eval=False, register_builtins=False)
        # Install the hook before removing debug and Python access from packages.
        self.lua.execute('local d=debug; arm_budget=function() d.sethook(function() error("instruction budget") end, "", 200000) end')
        self.arm = self.lua.globals().arm_budget
        self.lua.execute('arm_budget=nil; io=nil; os=nil; debug=nil; package=nil; require=nil; python=nil; dofile=nil; loadfile=nil; load=nil; pcall=nil; xpcall=nil; collectgarbage=nil; setmetatable=nil; getmetatable=nil; string.dump=nil')
        # Lua closures keep Python callables out of the package's object model.
        wrap = self.lua.eval('function(f) return function(...) return f(...) end end')
        api = self.lua.table()
        for name, callback in {"rect": self.rect, "text": self.text,
                               "tone": self.tone, "haptic": self.haptic,
                               "save_score": self.save_score,
                               "repeat_input": self.repeat_input, "random": self.random,
                               "screen": lambda: self.screen, "texture": self.texture,
                               "sprite": self.sprite, "sequence": self.sequence, "column": self.column, "exit": lambda *a: self.append("exit", a)}.items():
            api[name] = wrap(callback)
        self.lua.globals().game = api
        data = Path(path).read_bytes()
        if not 0 < len(data) <= 131072 or b'\0' in data:
            raise ValueError("Invalid package size or embedded NUL")
        prefix = '-- pubmote-game '
        header = data.decode().splitlines()[0]
        if not header.startswith(prefix):
            raise ValueError("Missing package header")
        self.metadata = json.loads(header[len(prefix):])
        if self.metadata['api'] != 1:
            raise ValueError("Unsupported game API")
        self.arm()
        self.lua.execute(data.decode())
        self.call('init', best)

    def call(self, name, *args):
        self.arm()
        callback = self.lua.globals()[name]
        if callback is None:
            raise ValueError(f"Missing callback: {name}")
        return callback(*args)

    def append(self, kind, args):
        if len(self.commands) >= 512:
            raise ValueError("Drawing budget exceeded")
        coords = args[1:5] if kind == 'sprite' else args[:4] if kind == 'rect' else args[:3]
        if not all(math.isfinite(v) and -200 <= v <= 300 for v in coords):
            raise ValueError("Invalid drawing coordinates")
        self.commands.append((kind, args))

    def rect(self, x, y, w, h, color, radius=0, border=0, edge=0, alpha=255):
        if min(w, h, radius) < 0 or not 0 <= color <= 0xffffff:
            raise ValueError("Invalid rectangle")
        self.append('rect', (x, y, w, h, color, radius, border, edge, alpha))

    def text(self, x, y, w, text, color, font, height=8, mono=False, bold=False):
        if len(text.encode()) > 96 or '\0' in text or not 8 <= font <= 28 or not 0 <= color <= 0xffffff:
            raise ValueError("Invalid text")
        self.append('text', (x, y, w, text, color, font, height, mono, bold))

    def texture(self, index, width, height, data):
        from PIL import Image
        size = width * height * 4
        if not 0 <= index <= 15 or not 1 <= width <= 256 or not 1 <= height <= 256 or len(data) > 32768 or self.texture_bytes + size > 256 * 1024:
            raise ValueError("Texture budget exceeded")
        decoder = zlib.decompressobj()
        pixels = decoder.decompress(base64.b64decode(data, validate=True), size + 1)
        if len(pixels) != size or not decoder.eof:
            raise ValueError("Invalid texture")
        self.textures[index] = Image.frombytes('RGBA', (width, height), pixels)
        self.texture_bytes += size

    def sprite(self, index, x, y, w, h, tint=None):
        self.append('sprite', (index, x, y, w, h, tint))

    def column(self, x, y, w, h, spacing, padding, rows):
        rows = [tuple(row[i] for i in range(1, 10)) for row in rows.values()]
        self.append("column", (x, y, w, h, spacing, padding, rows))

    def sequence(self, notes, repeat=False):
        self.effects.append(['sequence', [[n[1],n[2]] for n in notes.values()], bool(repeat)])
        self.feedback = f"Sequence: {len(notes)} notes"

    def tone(self, hz, ms):
        self.effects.append(["stop"] if hz == 0 and ms == 0 else ["tone",hz,ms])
        self.feedback = f"Tone: {hz} Hz / {ms} ms"

    def haptic(self, pattern):
        self.effects.append(["haptic",pattern])
        self.feedback = f"Haptic: {pattern}"

    def random(self, limit):
        self.rng = (self.rng * 1664525 + 1013904223) & 0xffffffff
        return self.rng % limit

    def repeat_input(self, delay, interval):
        self.repeat_delay, self.repeat_interval = delay / 1000, interval / 1000

    def save_score(self, score):
        self.best = max(self.best, score)

    def draw(self):
        self.commands.clear()
        self.call('draw')
        return self.commands


@lru_cache(maxsize=64)
def font_for(size, mono):
    from PIL import ImageFont
    path = ROOT / 'firmware/assets' / ('JetBrainsMono-Medium.ttf' if mono else 'Saira-SemiBold.ttf')
    return ImageFont.truetype(str(path), size)


def render_frame(host, size=466):
    from PIL import Image, ImageDraw
    output = Image.new('RGB', (size, size), '#071a2b')
    draw = ImageDraw.Draw(output, 'RGBA')
    scale = size / 100
    def color(rgb, alpha=255):
        return ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, alpha)
    for kind, args in host.draw():
        if kind == 'sprite':
            index,x,y,w,h,tint = args
            sprite = host.textures[index]
            if tint is not None:
                layer = Image.new('RGBA', sprite.size, color(tint))
                layer.putalpha(sprite.getchannel('A')); sprite = layer
            sprite = sprite.resize((max(1,round(w*scale)),max(1,round(h*scale))), Image.Resampling.BILINEAR)
            output.paste(sprite,(round(x*scale),round(y*scale)),sprite)
        elif kind == 'column':
            x,y,w,h,spacing,padding,rows = args
            prepared = []
            for text,font,rgb,mono,bold,height,cells,cell,wrap in rows:
                face = font_for(round(font*size/240),bool(mono))
                lines = [text]
                if wrap and face.getlength(text) > w*scale:
                    lines = ['']
                    for word in text.split():
                        candidate = (lines[-1]+' '+word).strip()
                        if face.getlength(candidate)>w*scale and lines[-1]: lines.append(word)
                        else: lines[-1]=candidate
                height_px = height*scale if height is not None and height>=0 else sum(face.getmetrics())*len(lines)
                prepared.append((lines,face,color(rgb),height_px,cells,cell))
            total = sum(row[3] for row in prepared)+spacing*scale*max(0,len(rows)-1)
            top = y*scale+((h-padding)*scale-total)/2
            for lines,face,ink,height_px,cells,cell in prepared:
                for i,line in enumerate(lines):
                    draw.text(((x+w/2)*scale,top+(i+.5)*height_px/len(lines)),line,anchor='mm',font=face,fill=ink)
                if cells is not None and cell:
                    for i,c in enumerate(cells.values()):
                        if c:
                            xx=(x+w/2-cell*2+(i%4)*cell)*scale;yy=top+(i//4)*cell*scale
                            draw.rectangle((xx,yy,xx+cell*scale-1,yy+cell*scale-1),fill=color(c))
                top += height_px+spacing*scale
        elif kind == 'exit':
            x,y,w,h=args
            draw.rounded_rectangle((x*scale,y*scale,(x+w)*scale,(y+h)*scale),radius=8*size/240,fill='#414141')
            draw.text(((x+w/2)*scale,(y+h/2)*scale),'Exit',anchor='mm',font=font_for(round(10*size/240),False),fill='white')
        elif kind == 'rect':
            x,y,w,h,rgb,radius,border,edge,alpha = args
            if w <= 0 or h <= 0: continue
            box=(x*scale,y*scale,(x+w)*scale,(y+h)*scale)
            draw.rounded_rectangle(box, radius=radius*scale, fill=color(rgb,alpha),
                                   outline=color(edge) if border else None, width=max(1,round(border*scale)))
        else:
            x,y,w,text,rgb,font,h,mono,bold = args
            if w == 0: w = font_for(round(font*size/240),mono).getlength(text) / scale
            if h == 0: h = sum(font_for(round(font*size/240),mono).getmetrics()) / scale
            draw.text(((x+w/2)*scale,(y+h/2)*scale),text,anchor='mm',font=font_for(round(font*size/240),mono),fill=color(rgb))
    return output


class Preview:
    def __init__(self, root, path, size):
        import tkinter as tk
        from tkinter import ttk

        self.root, self.path, self.size = root, path, size
        self.host = None
        self.stamp = None
        self.keys = {}
        self.last = time.monotonic()
        self.paused = False
        self.touching = False
        root.title(f"PubRemote — {path.stem}")
        self.canvas = tk.Canvas(root, width=size, height=size, bg='#071a2b', highlightthickness=0)
        self.canvas.pack()
        ttk.Label(root, text="Enter/Space: action • Arrows: stick • Mouse: touch/swipe\nR: restart • P: pause • Save file: reload • Esc: close").pack()
        self.joystick = tk.BooleanVar(value=False)
        ttk.Checkbutton(root, text="Analog stick (move slider to aim in Whack)", variable=self.joystick).pack()
        self.stick = tk.DoubleVar(value=0)
        ttk.Scale(root, from_=1, to=-1, variable=self.stick, orient='horizontal').pack(fill='x')
        self.status = tk.StringVar()
        ttk.Label(root, textvariable=self.status, wraplength=size).pack()
        root.bind('<KeyPress>', self.press)
        root.bind('<KeyRelease>', lambda e: self.keys.pop(e.keysym, None))
        root.bind('<FocusOut>', lambda e: self.keys.clear())
        self.canvas.bind('<ButtonPress-1>', self.touch_down)
        self.canvas.bind('<ButtonRelease-1>', self.touch_up)
        self.canvas.bind('<B1-Motion>', lambda e: self.event(7, e.x/self.size*100, e.y/self.size*100))
        self.reload()
        self.tick()

    def reload(self):
        try:
            self.stamp = self.path.stat().st_mtime_ns
            best = self.host.best if self.host else 0
            self.host = GameHost(self.path, best, self.size, self.size)
            self.keys.clear()
            self.paused = False
            self.status.set(f"{self.host.metadata['title']} — reloaded")
        except Exception as error:
            self.fail(error)

    def fail(self, error):
        self.paused = True
        self.status.set(f"Stopped: {error}")

    def event(self, kind, x=0, y=0):
        if self.host and not self.paused:
            try:
                if self.host.call('event', kind, x, y) is not False:
                    self.paint()
            except Exception as error:
                self.fail(error)

    def press(self, event):
        key = event.keysym
        if key in self.keys:
            return
        self.keys[key] = time.monotonic() + (self.host.repeat_delay if self.host else 0)
        if key == 'Escape': self.root.destroy()
        elif key.lower() == 'r': self.reload()
        elif key.lower() == 'p': self.paused = not self.paused
        else:
            kind = {'Return': 0, 'space': 0, 'Up': 1, 'Down': 2, 'Left': 3, 'Right': 4}.get(key)
            if kind is not None: self.event(kind)

    def touch_down(self, event):
        self.canvas.focus_set()
        x,y=event.x / self.size * 100,event.y / self.size * 100
        if self.host:
            for kind,args in self.host.commands:
                if kind=='exit':
                    bx,by,w,h=args
                    if bx <= x <= bx+w and by <= y <= by+h:
                        self.root.destroy()
                        return
        self.touching = 0 <= x <= 100 and 0 <= y <= 100
        if self.touching: self.event(5, event.x / self.size * 100, event.y / self.size * 100)

    def touch_up(self, event):
        if self.touching: self.event(6, event.x / self.size * 100, event.y / self.size * 100)
        self.touching = False

    def paint(self):
        from PIL import ImageTk
        self.photo = ImageTk.PhotoImage(render_frame(self.host, self.size))
        self.canvas.delete('all')
        self.canvas.create_image(0, 0, image=self.photo, anchor='nw')

    def tick(self):
        now = time.monotonic()
        dt, self.last = int((now - self.last)*1000)/1000, now
        if dt < 0 or dt > .250: dt=.033
        try:
            if self.path.stat().st_mtime_ns != self.stamp:
                self.reload()
            if self.host and not self.paused:
                for key, deadline in list(self.keys.items()):
                    if self.host.repeat_interval and key in ('Down', 'Left', 'Right') and now >= deadline:
                        self.event({'Up': 1, 'Down': 2, 'Left': 3, 'Right': 4}[key])
                        self.keys[key] = now + self.host.repeat_interval
                if self.host.call('update', dt, 0, self.stick.get(), self.joystick.get()) is not False:
                    self.paint()
                self.status.set(f"Lua {self.host.lua.get_memory_used() // 1024}/256 KiB · {len(self.host.commands)}/512 draws · {self.host.feedback}")
        except Exception as error:
            self.fail(error)
        self.root.after(33, self.tick)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', help='Game name (tetris, flappy, whack) or Lua file path')
    parser.add_argument('--size', type=int, default=466)
    args = parser.parse_args()
    path = Path(args.game)
    if not path.is_file():
        path = ROOT / 'games' / f'{args.game}.lua'
    if not path.is_file():
        parser.error(f'Game not found: {path}')
    if not 240 <= args.size <= 1200:
        parser.error('Size must be between 240 and 1200')
    import tkinter as tk
    root = tk.Tk()
    Preview(root, path, args.size)
    root.mainloop()


if __name__ == '__main__':
    main()

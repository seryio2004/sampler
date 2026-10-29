"""Matching painters for the actual Canvas UI and portable PNG previews."""
from .view import BG, TEXT


class TkPainter:
    def __init__(self, canvas):
        self.canvas = canvas
        width, height = canvas.winfo_width(), canvas.winfo_height()
        self.scale = min(max(1, width)/1280, max(1, height)/800)
        self.dx, self.dy = (width-1280*self.scale)/2, (height-800*self.scale)/2
        canvas.delete('all')
        canvas.configure(bg=BG)

    def xy(self, x, y):
        return self.dx+x*self.scale, self.dy+y*self.scale

    def rect(self, x0, y0, x1, y1, fill, outline=None):
        self.canvas.create_rectangle(*self.xy(x0, y0), *self.xy(x1, y1), fill=fill,
                                     outline=outline or fill, width=max(1, self.scale))

    def oval(self, x0, y0, x1, y1, fill):
        self.canvas.create_oval(*self.xy(x0, y0), *self.xy(x1, y1), fill=fill, outline='')

    def line(self, points, color, width=1):
        coords = []
        for i in range(0, len(points), 2):
            coords.extend(self.xy(points[i], points[i+1]))
        self.canvas.create_line(*coords, fill=color, width=max(1, width*self.scale))

    def text(self, x, y, text, size=11, color=TEXT, bold=False, anchor='nw', family='sans'):
        self.canvas.create_text(*self.xy(x, y), text=str(text), fill=color, anchor=anchor,
                                font=('DejaVu Sans Mono' if family=='mono' else 'DejaVu Sans', -max(8, round(size*self.scale)), 'bold' if bold else 'normal'))


class ImagePainter:
    """Offscreen render of the same layout, without a display server or audio."""
    def __init__(self, width=1280, height=800):
        from PIL import Image, ImageDraw
        self.image = Image.new('RGB', (width, height), BG)
        self.draw = ImageDraw.Draw(self.image)
        self.scale = min(width/1280, height/800)
        self.dx, self.dy = (width-1280*self.scale)/2, (height-800*self.scale)/2
        self.fonts = {}

    def xy(self, x, y):
        return self.dx+x*self.scale, self.dy+y*self.scale

    def rect(self, x0, y0, x1, y1, fill, outline=None):
        self.draw.rectangle((*self.xy(x0, y0), *self.xy(x1, y1)), fill=fill, outline=outline)

    def oval(self, x0, y0, x1, y1, fill):
        self.draw.ellipse((*self.xy(x0, y0), *self.xy(x1, y1)), fill=fill)

    def line(self, points, color, width=1):
        coords = [self.xy(points[i], points[i+1]) for i in range(0, len(points), 2)]
        self.draw.line(coords, fill=color, width=max(1, round(width*self.scale)))

    def text(self, x, y, text, size=11, color=TEXT, bold=False, anchor='nw', family='sans'):
        from PIL import ImageFont
        from pathlib import Path
        key = (size, bold, family)
        if key not in self.fonts:
            font = Path('/usr/share/fonts/truetype/dejavu/'+('DejaVuSansMono' if family=='mono' else 'DejaVuSans')+('-Bold' if bold else '')+'.ttf')
            try:
                self.fonts[key] = ImageFont.truetype(str(font), max(8, round(size*self.scale)))
            except OSError:
                self.fonts[key] = ImageFont.load_default()
        font = self.fonts[key]
        box = self.draw.textbbox((0, 0), str(text), font=font)
        width, height = box[2]-box[0], box[3]-box[1]
        px, py = self.xy(x, y)
        if anchor in ('center', 'n', 's'):
            px -= width/2
        elif anchor in ('ne', 'e', 'se'):
            px -= width
        if anchor in ('center', 'w', 'e'):
            py -= height/2
        elif anchor in ('s', 'sw', 'se'):
            py -= height
        self.draw.text((px-box[0], py-box[1]), str(text), fill=color, font=font)

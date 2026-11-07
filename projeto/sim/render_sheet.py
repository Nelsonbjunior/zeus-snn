"""Junta as cenas .pbm do simulador numa folha PNG ampliada (pixels do OLED visiveis).

Uso:  py -3.13 sim\\render_sheet.py [pasta_das_cenas] [saida.png]
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SCALE = 4
YELLOW = (255, 200, 40)   # linhas 0-15 do OLED bicolor
BLUE = (80, 170, 255)


def load_pbm(path):
    lines = [ln for ln in path.read_text().split("\n") if ln and not ln.startswith("#")]
    w, h = map(int, lines[1].split())
    rows = lines[2:2 + h]
    img = Image.new("RGB", (w, h), (0, 0, 0))
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:w]):
            if ch == "1":
                px[x, y] = YELLOW if y < 16 else BLUE
    return img.resize((w * SCALE, h * SCALE), Image.NEAREST)


def main():
    here = Path(__file__).resolve().parent
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "out"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "out" / "folha.png"
    files = sorted(src.glob("*.pbm"))
    if not files:
        sys.exit(f"nenhuma cena em {src}: rode o simulador antes")
    cols = 3
    cw, ch = 128 * SCALE + 16, 64 * SCALE + 30
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw, rows * ch), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(files):
        x, y = (i % cols) * cw + 8, (i // cols) * ch + 4
        d.text((x, y), f.stem, fill=(220, 220, 220))
        sheet.paste(load_pbm(f), (x, y + 18))
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()

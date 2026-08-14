from pathlib import Path

from reportlab.lib.pagesizes import landscape
from reportlab.pdfgen import canvas
from PIL import Image

root = Path("/Users/kenneth/Documents/Codex/2026-08-13/referenced-chatgpt-conversation-this-is-an")
render_dir = root / "work/ui-design-system-v2/pptx-render-final"
output = root / "outputs/JusPrin-UI-Design-System.pdf"
pages = sorted(render_dir.glob("slide-*.png"), key=lambda p: int(p.stem.split("-")[-1]))
if len(pages) != 13:
    raise RuntimeError(f"Expected 13 rendered slides, found {len(pages)}")

with Image.open(pages[0]) as first:
    page_size = landscape((first.height, first.width))

pdf = canvas.Canvas(str(output), pagesize=page_size, pageCompression=1)
for page in pages:
    pdf.drawImage(str(page), 0, 0, width=page_size[0], height=page_size[1], preserveAspectRatio=True)
    pdf.showPage()
pdf.save()
print(output)

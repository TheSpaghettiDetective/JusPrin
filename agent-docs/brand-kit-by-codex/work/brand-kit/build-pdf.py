from pathlib import Path

from reportlab.lib.utils import ImageReader
from reportlab.pdfgen import canvas


ROOT = Path("/Users/kenneth/Documents/Codex/2026-08-13/referenced-chatgpt-conversation-this-is-an")
SLIDES = ROOT / "work/brand-kit/hires-renders"
OUTPUT = ROOT / "outputs/JusPrin-Visual-Brand-Kit.pdf"
PAGE = (960, 540)


files = sorted(SLIDES.glob("slide-*.png"), key=lambda p: int(p.stem.split("-")[-1]))
if len(files) != 12:
    raise RuntimeError(f"Expected 12 rendered pages, found {len(files)}")

pdf = canvas.Canvas(str(OUTPUT), pagesize=PAGE, pageCompression=1)
pdf.setTitle("JusPrin Visual Brand Kit")
pdf.setAuthor("JusPrin")
pdf.setSubject("Visual identity guidelines and applications")
for slide in files:
    pdf.drawImage(ImageReader(str(slide)), 0, 0, width=PAGE[0], height=PAGE[1], preserveAspectRatio=True, mask="auto")
    pdf.showPage()
pdf.save()

print(f"Created {OUTPUT} from {len(files)} verified slide renders")

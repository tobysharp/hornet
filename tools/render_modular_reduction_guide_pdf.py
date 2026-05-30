from __future__ import annotations

import html
import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import LETTER
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import Paragraph, Preformatted, SimpleDocTemplate, Spacer, Table, TableStyle


def make_styles():
    styles = getSampleStyleSheet()
    body = ParagraphStyle(
        "Body",
        parent=styles["BodyText"],
        fontName="Times-Roman",
        fontSize=10.5,
        leading=14,
        alignment=TA_JUSTIFY,
        spaceAfter=8,
    )
    title = ParagraphStyle(
        "TitleGuide",
        parent=styles["Title"],
        fontName="Helvetica-Bold",
        fontSize=22,
        leading=28,
        alignment=TA_CENTER,
        spaceAfter=18,
    )
    h1 = ParagraphStyle(
        "H1",
        parent=styles["Heading1"],
        fontName="Helvetica-Bold",
        fontSize=16,
        leading=20,
        spaceBefore=12,
        spaceAfter=8,
    )
    h2 = ParagraphStyle(
        "H2",
        parent=styles["Heading2"],
        fontName="Helvetica-Bold",
        fontSize=13,
        leading=17,
        spaceBefore=10,
        spaceAfter=6,
    )
    h3 = ParagraphStyle(
        "H3",
        parent=styles["Heading3"],
        fontName="Helvetica-BoldOblique",
        fontSize=11,
        leading=14,
        spaceBefore=8,
        spaceAfter=4,
    )
    bullet = ParagraphStyle(
        "BulletGuide",
        parent=body,
        leftIndent=18,
        firstLineIndent=-12,
        spaceAfter=4,
        alignment=TA_LEFT,
    )
    code = ParagraphStyle(
        "CodeLabel",
        parent=body,
        fontName="Helvetica-Oblique",
        fontSize=8,
        textColor=colors.HexColor("#555555"),
        spaceAfter=4,
        alignment=TA_LEFT,
    )
    return {"body": body, "title": title, "h1": h1, "h2": h2, "h3": h3, "bullet": bullet, "code": code}


def inline_markup(text: str) -> str:
    escaped = html.escape(text)
    return re.sub(r"`([^`]+)`", r"<font name='Courier'>\1</font>", escaped)


def block_table(flowable, background):
    table = Table([[flowable]], colWidths=[6.6 * inch])
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), background),
                ("BOX", (0, 0), (-1, -1), 0.5, colors.HexColor("#C7CCD1")),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def render_markdown_to_story(text: str):
    styles = make_styles()
    story = []
    lines = text.splitlines()
    i = 0
    paragraph = []

    def flush_paragraph():
        nonlocal paragraph
        if paragraph:
            story.append(Paragraph(inline_markup(" ".join(paragraph).strip()), styles["body"]))
            paragraph = []

    while i < len(lines):
        line = lines[i]

        if line.startswith("```"):
            flush_paragraph()
            block_type = line[3:].strip() or "code"
            i += 1
            block_lines = []
            while i < len(lines) and not lines[i].startswith("```"):
                block_lines.append(lines[i])
                i += 1
            label = "Math" if block_type == "math" else "Code"
            story.append(Paragraph(label, styles["code"]))
            pre = Preformatted(
                "\n".join(block_lines),
                ParagraphStyle(
                    "Block",
                    fontName="Courier",
                    fontSize=9,
                    leading=11,
                    alignment=TA_LEFT,
                ),
            )
            background = colors.HexColor("#F7F8FA") if block_type == "code" else colors.HexColor("#F3F7FB")
            story.append(block_table(pre, background))
            story.append(Spacer(1, 0.10 * inch))
        elif line.startswith("# "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(line[2:].strip()), styles["title"]))
        elif line.startswith("## "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(line[3:].strip()), styles["h1"]))
        elif line.startswith("### "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(line[4:].strip()), styles["h2"]))
        elif line.startswith("#### "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(line[5:].strip()), styles["h3"]))
        elif re.match(r"^\d+\. ", line) or line.startswith("- "):
            flush_paragraph()
            bullet_text = line
            story.append(Paragraph(inline_markup(bullet_text), styles["bullet"]))
        elif not line.strip():
            flush_paragraph()
        else:
            paragraph.append(line.strip())
        i += 1

    flush_paragraph()
    return story


def build_pdf(input_path: Path, output_path: Path):
    text = input_path.read_text(encoding="utf-8")
    story = render_markdown_to_story(text)

    doc = SimpleDocTemplate(
        str(output_path),
        pagesize=LETTER,
        leftMargin=0.8 * inch,
        rightMargin=0.8 * inch,
        topMargin=0.7 * inch,
        bottomMargin=0.7 * inch,
        title="Optimizing 512-to-256 Bit Modular Reduction For secp256k1 Field Arithmetic",
        author="GitHub Copilot",
    )

    def add_page_number(canvas, _doc):
        canvas.setFont("Helvetica", 9)
        canvas.setFillColor(colors.HexColor("#666666"))
        canvas.drawRightString(7.35 * inch, 0.45 * inch, f"Page {canvas.getPageNumber()}")

    doc.build(story, onFirstPage=add_page_number, onLaterPages=add_page_number)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: render_modular_reduction_guide_pdf.py INPUT.md OUTPUT.pdf", file=sys.stderr)
        return 2
    build_pdf(Path(argv[1]), Path(argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
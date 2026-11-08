"""Render docs/MANUAL.md to a printable PDF (docs/BasementalFG250-Manual.pdf).

Needs: python3 -m pip install markdown, and Node with Playwright's Chromium.
Usage:  python3 docs/make_manual_pdf.py
"""
import pathlib
import subprocess
import sys

import markdown

here = pathlib.Path(__file__).resolve().parent
md = (here / "MANUAL.md").read_text(encoding="utf-8")
body = markdown.markdown(md, extensions=["tables", "fenced_code"])

css = """
@page { size: A4; margin: 16mm 15mm 18mm 15mm; }
body { font-family: "Helvetica Neue", Arial, sans-serif; font-size: 10.5pt; line-height: 1.45; color: #1d1f22; }
h1 { font-size: 22pt; margin: 0 0 4pt; letter-spacing: -0.3pt; }
h2 { font-size: 15pt; margin: 18pt 0 6pt; padding-bottom: 3pt; border-bottom: 1.5px solid #6c51b8; break-after: avoid; }
h3 { font-size: 12pt; margin: 12pt 0 4pt; break-after: avoid; }
img { width: 100%; border-radius: 6px; margin: 6pt 0; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 10pt; font-size: 9.5pt; break-inside: auto; }
th { background: #2b2d31; color: #fff; text-align: left; padding: 4pt 6pt; }
td { border-bottom: 1px solid #d9dbe0; padding: 4pt 6pt; vertical-align: top; }
tr { break-inside: avoid; }
code, pre { font-family: Menlo, Consolas, monospace; font-size: 8.8pt; }
pre { background: #f2f1f7; border-left: 3px solid #6c51b8; padding: 6pt 8pt; white-space: pre-wrap; }
blockquote { margin: 6pt 0; padding: 4pt 10pt; background: #f6f3ff; border-left: 3px solid #9b7fd4; }
hr { border: 0; border-top: 1px solid #ddd; margin: 12pt 0; }
a { color: #4b3596; }
em { color: #555; }
"""
html = f"<!doctype html><html><head><meta charset='utf-8'><title>Basemental FG 250 Manual</title><style>{css}</style></head><body>{body}</body></html>"
html_path = here / "_manual.html"
html_path.write_text(html, encoding="utf-8")
out = here / "BasementalFG250-Manual.pdf"

js = f"""
const {{ chromium }} = require('playwright');
(async () => {{
  const b = await chromium.launch();
  const p = await b.newPage();
  await p.goto('file://{html_path}');
  await p.pdf({{ path: '{out}', format: 'A4', printBackground: true,
                displayHeaderFooter: true, headerTemplate: '<span></span>',
                footerTemplate: '<div style="font-size:8px;width:100%;text-align:center;color:#888">Basemental FG 250 manual · page <span class="pageNumber"></span> of <span class="totalPages"></span></div>',
                margin: {{ top: '16mm', bottom: '18mm', left: '15mm', right: '15mm' }} }});
  await b.close();
}})();
"""
subprocess.run(["node", "-e", js], check=True)
html_path.unlink()
print(f"wrote {out}")
sys.exit(0)

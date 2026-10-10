"""Make a print PDF from native SVG exports (omit editor grid background)."""
from pathlib import Path
import sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.append(str(ROOT.parents[1]/'.local/pcb-python'))
import pymupdf
export=ROOT/'exports';source=export/'schematic';pdf=pymupdf.open()
for i,p in enumerate(sorted(source.glob('SCH_*.svg'))):
    tree=ET.fromstring(p.read_text(encoding='utf-8'))
    for a in list(tree):
        if a.get('id')=='gridBg':tree.remove(a)
        elif a.get('id')=='fillBg':a.set('fill','#ffffff')
    for a in tree.iter():
        if a.text and a.text.startswith('DRAFT - native ERC'):
            a.text='Rev A prototype design. GPIO identities retained. See VALIDATION.md for check status.'
    data=ET.tostring(tree,encoding='utf-8',xml_declaration=True)
    target=source/f'print-sheet-{i+1:02}.svg';target.write_bytes(data)
    doc=pymupdf.open(stream=data,filetype='svg');converted=pymupdf.open(stream=doc.convert_to_pdf(),filetype='pdf')
    pdf.insert_pdf(converted)
    converted[0].get_pixmap(matrix=pymupdf.Matrix(1.3,1.3)).save(source/f'sheet-{i+1:02}.png')
pdf.save(export/'ESP-HI-C3-RevA-schematic.pdf')
print('Rendered schematic pages:',len(pdf))

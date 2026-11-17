#!/usr/bin/env python3
"""BDF (6x12) -> C array for Phosphor_DE's UI font (128 ASCII glyphs x 12 rows, 6 px MSB first).
   python bdf2font.py font.bdf out.h "credit line"   """
import sys
def main(src, dst, credit):
    asc = None; glyphs = {}; cur = None; bm = None
    for ln in open(src, encoding='latin-1'):
        p = ln.split()
        if not p: continue
        if p[0] == 'FONT_ASCENT': asc = int(p[1])
        elif p[0] == 'ENCODING': cur = {'enc': int(p[1])}
        elif p[0] == 'BBX' and cur is not None: cur['bbx'] = list(map(int, p[1:5]))
        elif p[0] == 'BITMAP': bm = []
        elif p[0] == 'ENDCHAR':
            cur['rows'] = bm; glyphs[cur['enc']] = cur; cur = None; bm = None
        elif bm is not None: bm.append(int(p[0], 16) << (8 * (1 + (len(p[0]) - 1) // 2 * 0)) >> (4 * len(p[0])) if False else (int(p[0], 16), len(p[0])))
    out = []
    for ch in range(128):
        cell = [0] * 12
        g = glyphs.get(ch)
        if g and ch >= 32:
            w, h, xo, yo = g['bbx']
            top = asc - (yo + h)
            for i, (v, nd) in enumerate(g['rows']):
                bits = v << (8 - 4 * nd) if nd <= 2 else v >> (4 * nd - 8)   # to 8-bit MSB-first
                bits = (bits >> xo) & 0xFF if xo >= 0 else (bits << -xo) & 0xFF
                r = top + i
                if 0 <= r < 12: cell[r] = bits & 0xFC
        out += cell
    with open(dst, 'w') as f:
        f.write('// %s\n// converted with tools/bdf2font.py\n\nconst unsigned char NC_FONT[] = {\n' % credit)
        for i in range(0, len(out), 12):
            f.write('\t' + ', '.join('0x%02X' % b for b in out[i:i + 12]) + ',\t// %r\n' % chr(i // 12) if 32 <= i // 12 < 127 else '\t' + ', '.join('0x%02X' % b for b in out[i:i + 12]) + ',\n')
        f.write('};\n')
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])

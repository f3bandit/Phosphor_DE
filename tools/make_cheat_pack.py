#!/usr/bin/env python3
"""Phosphor_DE cheat pack builder - by f3bandit

Takes the libretro cheat database (cht/Nintendo - Game Boy Advance/*.cht, CC BY-SA 4.0)
and the No-Intro GBA dat (metadat/no-intro/Nintendo - Game Boy Advance.dat, for game codes),
and writes one file per game code: SYSTEM/CHEATS/<CODE>.cht (RetroArch layout, as is).
When several files map to one code (regions, revisions) their cheats are merged.

  python make_cheat_pack.py <libretro-database folder> <output folder>
"""
import os, re, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbacodes

def main(db, out):
    dat = open(os.path.join(db, 'metadat/no-intro/Nintendo - Game Boy Advance.dat'), encoding='utf-8', errors='replace').read()
    serial = {}
    for m in re.finditer(r'game \(\s*name "([^"]+)"(.*?)\n\)', dat, re.S):
        s = re.search(r'serial "([0-9A-Z]{4})"', m.group(2))
        if s:
            serial[m.group(1)] = s.group(1)
    def base(n):  # name without trailing tags like (Code Breaker), (Rev 1) kept for matching
        return re.sub(r'\s*\((Code Breaker|Game ?Shark|Action Replay|GameShark SP|CodeBreaker)\)\s*$', '', n, flags=re.I)
    # older cheat files use names like "(USA, Europe)" where the dat now lists each region:
    # match on the bare title and the regions the cheat file names
    def split(n):
        title = re.sub(r'\s*\(.*$', '', n).strip().lower()
        tags = re.findall(r'\(([^)]*)\)', n)
        regions = set()
        for t in tags:
            for r in t.split(','):
                r = r.strip()
                if r in ('USA', 'Europe', 'Japan', 'World', 'Australia', 'Germany', 'France', 'Spain', 'Italy', 'Korea'):
                    regions.add(r)
        other = [t for t in tags if not any(x.strip() in ('USA', 'Europe', 'Japan', 'World', 'Australia', 'Germany', 'France', 'Spain', 'Italy', 'Korea') for x in t.split(',')) and not re.fullmatch(r'(En|Fr|De|Es|It|Nl|Sv|No|Da|Fi|Ja|Pt)(,(En|Fr|De|Es|It|Nl|Sv|No|Da|Fi|Ja|Pt))*', t)]
        return title, regions, set(o.lower() for o in other)
    by_title = {}
    for n, c in serial.items():
        t, r, o = split(n)
        by_title.setdefault(t, []).append((r, o, c))
    def match_title(n):
        t, r, o = split(n)
        out = []
        for r2, o2, c in by_title.get(t, []):
            if o2 != o:            # (Rev 1), (Demo), (Beta) must agree
                continue
            if r & r2 or 'World' in r or 'World' in r2:
                out.append(c)
        return sorted(set(out))
    by_code, missing = {}, []
    report, stats = [], {}
    for f in sorted(glob.glob(os.path.join(db, 'cht/Nintendo - Game Boy Advance/*.cht'))):
        name = base(os.path.basename(f)[:-4])
        name = re.sub(r'\s*\(diff\)', '', name).replace(' _ ', ' & ')
        name = base(name)
        code = serial.get(name)
        codes = [code] if code else match_title(name)
        if not codes:
            missing.append(os.path.basename(f)); continue
        t = open(f, encoding='utf-8', errors='replace').read()
        cheats = []
        conv = gbacodes.Converter()      # decryption state runs through the file in order
        entries = sorted(((int(i), d) for i, d in re.findall(r'cheat(\d+)_desc\s*=\s*"([^"]*)"', t)))
        for i, d in entries:
            c = re.search(r'cheat%d_code\s*=\s*"([^"]*)"' % i, t)
            if not (c and c.group(1).strip()):
                continue
            try:
                lines, how = conv.convert(c.group(1).strip())
            except gbacodes.Bad as e:
                report.append('%s | %s | dropped: %s' % (os.path.basename(f), d.strip(), e))
                continue
            stats[how] = stats.get(how, 0) + 1
            if not lines:            # master code / game ID: nothing for Phosphor_DE to run
                continue
            cheats.append((d.strip(), '+'.join(l.replace(' ', '+') for l in lines)))
        if not cheats:
            continue
        for code in codes:
            lst = by_code.setdefault(code, [])
            seen = {d.lower() for d, _ in lst}
            lst += [(d, c) for d, c in cheats if d.lower() not in seen]
    dst = os.path.join(out, 'SYSTEM', 'CHEATS')
    os.makedirs(dst, exist_ok=True)
    total = 0
    for code, cheats in sorted(by_code.items()):
        lines = ['cheats = %d' % len(cheats), '']
        for i, (d, c) in enumerate(cheats):
            lines += ['cheat%d_desc = "%s"' % (i, d.replace('"', "'")), 'cheat%d_code = "%s"' % (i, c), 'cheat%d_enable = false' % i, '']
        open(os.path.join(dst, code + '.cht'), 'w', encoding='utf-8', newline='\r\n').write('\n'.join(lines))
        total += len(cheats)
    open(os.path.join(out, 'CHEATS_SOURCE.txt'), 'w', encoding='utf-8', newline='\r\n').write(
        'Cheats in SYSTEM/CHEATS come from the libretro cheat database\n'
        'https://github.com/libretro/libretro-database (CC BY-SA 4.0), renamed by game code.\n\n'
        'Codes are written as unencrypted Code Breaker. Action Replay v3 and GameShark codes were decrypted\n'
        'and converted (ROM patches such as Anti-DMA included). Converted: %s\n\n'
        'Files with no game code match (not included):\n' % stats + '\n'.join(missing) + '\n\n'
        'Cheats left out (could not be converted):\n' + '\n'.join(report) + '\n')
    print('%d games, %d cheats, %d files unmatched, %d cheats dropped' % (len(by_code), total, len(missing), len(report)), stats)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    main(sys.argv[1], sys.argv[2])

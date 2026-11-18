#!/usr/bin/env python3
"""Phosphor_DE cheat pack audit - by f3bandit

  python audit_pack.py <folder with SYSTEM/CHEATS> <libretro-database folder> [report.txt]

Reads every cheat the way the cart's engine does and lists the ones that can't do what their name says,
or that do harm. Nothing is changed; make_gh_pack.py's generic checks (audit_cheat) drop the "drop"
findings when the pack is built.
"""
import os, re, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# engine table room: the in-game patch is copied to VRAM 0x06012C00 and the cheat table follows it at
# CHEAT - RTS_ReplaceIRQ_start = 0xE48, up to 0x06018000
ENGINE_ROOM = (0x06018000 - 0x06012C00 - 0xE48) // 8

def rom_sizes(db):
    dat = open(os.path.join(db, 'metadat/no-intro/Nintendo - Game Boy Advance.dat'), encoding='utf-8', errors='replace').read()
    sz = {}
    for size, serials in re.findall(r'rom \( name "[^"]*" size (\d+) [^)]*?serial "([^"]*)"', dat):
        for serial in re.findall(r'\b[0-9A-Z]{4}\b', serials):
            sz[serial] = max(sz.get(serial, 0), int(size))
    return sz

def read_cht(path):
    t = open(path, encoding='utf-8', errors='replace').read()
    d = dict(re.findall(r'cheat(\d+)_desc\s*=\s*"([^"]*)"', t))
    c = dict(re.findall(r'cheat(\d+)_code\s*=\s*"([^"]*)"', t))
    return [(d[i], c.get(i, '')) for i in sorted(d, key=int)]

def lines_of(code):
    t = code.split('+')
    return [(int(t[k], 16), int(t[k + 1], 16)) for k in range(0, len(t) - 1, 2)]

def walk(lines):
    """yield (kind, type, addr, value) the way the engine compiles it; kinds: w (write), c (condition),
    rom, fillp (fill params), list (list header), data (list data), hook"""
    fill = False; left = 0
    for op1, op2 in lines:
        t, a = op1 >> 28, op1 & 0x0FFFFFFF
        if fill: yield ('fillp', t, op1, op2); fill = False; continue
        if left: n = min(3, left); left -= n; yield ('data', t, op1, op2); continue
        if t in (0, 1): yield ('hook', t, a, op2)
        elif t == 4: yield ('w', t, a, op2); fill = True
        elif t == 5: yield ('list', t, a, op2); left = op2
        elif t == 8 and 0x08000000 <= a < 0x0A000000: yield ('rom', t, a, op2)
        elif t in (7, 0xA, 0xB, 0xC, 0xF, 0xD): yield ('c', t, a, op2)
        else: yield ('w', t, a, op2)

def entries(lines):
    n, fill, left = 0, False, 0
    for k, t, a, v in walk(lines):
        if k == 'fillp': n += 1
        elif k == 'list': pass
        elif k == 'data': n += 3
        elif k in ('w', 'c'): n += 1
    return n

KEYMASK = 0x03FF

def audit_cheat(code, name, cbcode, romsize):
    """(severity, why) findings for one cheat: 'drop' = harmful or can't work, 'check' = worth a look"""
    out = []
    try:
        L = lines_of(cbcode)
    except ValueError:
        return [('drop', 'unreadable code')]
    items = list(walk(L))
    last_cond = None
    for k, t, a, v in items:
        if k == 'w':
            if 0x03007F00 <= a <= 0x03007FFF:
                out.append(('drop', 'writes the BIOS area at the top of IWRAM (stacks / interrupt vector / cheat switch)'))
            elif 0x04000000 <= a < 0x05000000:
                reg = a & 0x3FF
                if 0xB0 <= reg < 0xE0 or 0x100 <= reg < 0x110 or reg in (0x200, 0x202, 0x208) or reg == 0x130:
                    out.append(('drop', 'writes a hardware register (DMA / timer / interrupt / keys) every frame'))
                else:
                    out.append(('check', 'writes a video or sound register every frame'))
            elif 0x05000000 <= a < 0x08000000:
                out.append(('check', 'writes palette / VRAM / OAM every frame'))
        if k == 'rom':
            if a & 1:
                out.append(('drop', 'ROM patch at an odd address (lands one byte off)'))
            if romsize and (a - 0x08000000) >= romsize:
                out.append(('drop', 'ROM patch past the end of the game (%d MB)' % (romsize >> 20)))
        if k == 'c' and (a & 0x0FFFFFFF) == 0x04000130 and t in (7, 0xA):
            if v & ~KEYMASK & 0xFFFF:
                out.append(('drop', 'button check that can never be true'))
            elif t == 7 and v == 0:
                out.append(('drop', 'only runs with all 10 buttons held'))
        if k == 'c' and t == 0xD and (v & KEYMASK) == KEYMASK:
            out.append(('drop', 'only runs with all 10 buttons held'))
        last_cond = k == 'c'
    if items and last_cond:
        out.append(('drop', 'ends with a condition (nothing to run after it)'))
    if entries(L) > ENGINE_ROOM:
        out.append(('drop', 'too big for the engine (%d entries, room for %d)' % (entries(L), ENGINE_ROOM)))
    elif entries(L) > 600:
        out.append(('check', 'very big (%d engine entries)' % entries(L)))
    if re.search(r'&amp;|&quot;|&#\d+;|\\[nrt]', name):
        out.append(('fix', 'HTML / escape codes in the name'))
    if any(ord(ch) > 126 for ch in name):
        out.append(('fix', 'non-ASCII characters in the name'))
    if re.search(r'\b(modifier|digit|enter value|value code|xxxx|\?\?)\b', name, re.I):
        out.append(('check', 'name suggests a value to choose'))
    return out

def main(pack, db, rep=None):
    sz = rom_sizes(db)
    d = os.path.join(pack, 'SYSTEM', 'CHEATS')
    find = collections.defaultdict(list)
    sev = collections.Counter()
    games = cheats = 0
    for f in sorted(os.listdir(d)):
        if not f.endswith('.cht'): continue
        code = f[:-4]; games += 1
        lst = read_cht(os.path.join(d, f))
        names = collections.Counter(n for n, _ in lst)
        for n, c in lst:
            cheats += 1
            for s, why in audit_cheat(code, n, c, sz.get(code)):
                find[(s, why)].append((code, n)); sev[s] += 1
            if names[n] > 1:
                find[('fix', 'same name twice in one list')].append((code, n)); sev['fix'] += 1
        if code not in sz:
            find[('check', 'game code not in the No-Intro GBA list')].append((code, '')); sev['check'] += 1
    out = ['%d games, %d cheats; engine room %d entries' % (games, cheats, ENGINE_ROOM), '']
    for (s, why), lst in sorted(find.items(), key=lambda x: ('drop', 'fix', 'check').index(x[0][0])):
        out.append('[%s] %s: %d' % (s, why, len(lst)))
    out.append('')
    for (s, why), lst in sorted(find.items(), key=lambda x: ('drop', 'fix', 'check').index(x[0][0])):
        out.append('== [%s] %s' % (s, why))
        out += ['  %s | %s' % x for x in lst]
    txt = '\n'.join(out) + '\n'
    if rep: open(rep, 'w', encoding='utf-8').write(txt)
    print('\n'.join(out[:2 + len(find)]))

if __name__ == '__main__':
    main(*sys.argv[1:])

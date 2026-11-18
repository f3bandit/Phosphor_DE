#!/usr/bin/env python3
"""Phosphor_DE cheat pack builder (GameHacking.org + libretro) - by f3bandit

  python make_gh_pack.py <gamehacking zip or folder> <libretro pack folder> <libretro-database folder> <out folder>

<gamehacking zip>   what the GBA Cheat Downloader extension saved: one LibRetro .cht per game + index.csv
<libretro pack>     the pack built by make_cheat_pack.py (its SYSTEM/CHEATS files get merged in)
<libretro-database> for the No-Intro GBA dat (game codes for GameHacking entries without a serial)

Rules (ready-to-play codes only, nothing for the user to edit):
  - codes with a value left to fill in (XXXX, ??) are dropped, and so are GameHacking's spelled-out
    versions of them (one code repeated with a list of values, each under its own name)
  - Action Replay / GameShark codes are taken raw (GameHacking stores them decrypted) or decrypted
  - master / "must be on" codes are dropped (nothing to run); Anti-DMA ROM patches are kept
  - every file is compiled with the Phosphor_DE cheat code itself (cht_check); anything it can't run goes
  - at most 448 cheats per game (the cart's list size)
Writes SYSTEM/CHEATS/<CODE>.cht, CHEATS_SOURCE.txt and CHEATS_REPORT.txt (every dropped code and why).
"""
import os, re, sys, csv, io, zipfile, subprocess, tempfile, shutil, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbacodes as g

CH_MAX = 448
HERE = os.path.dirname(os.path.abspath(__file__))
DROP = {('MSSE', '(M) - Must Be On'): 'suspicious master code (GBA Video)',
        ('AFXE', 'Infinite Items (All Slots)'): 'also overwrites the byte next to each item count'}
FIX = {('AFXE', 'Infinite MP No 4'): '022003B4 000003E7',      # encrypted junk on the site, rebuilt from
       ('AFXE', 'Max Level No 24'): '0020190E 00000063'}       # the pattern of the other characters

# Checked by hand (and on the cart), applied last to every cheat whatever its source (GameHacking or
# libretro): POST_DROP removes, POST_CODE replaces the code (Code Breaker, '+' between words),
# RENAME fixes the name (button combos read from the code's key checks).
_EMERALD_HEAP = 'writes into the game\'s heap (0x02000000-0x0201BFFF) every frame: overworld graphics glitches'
POST_DROP = {}
for _c in ('BPEE', 'BPEP'):
    POST_DROP[(_c, 'Max Money')] = 'only changes the money shown in the bag; ' + _EMERALD_HEAP
    POST_DROP[(_c, 'Max Coins')] = _EMERALD_HEAP + ' (Max Coins From Pokies only writes in the slot machine)'
    POST_DROP[(_c, 'Wild Pokemon Catching Spree')] = _EMERALD_HEAP
POST_DROP[('BPEE', 'Complete Pokedex')] = 'only fills entries 1-8 (Pokedex Complete is the full one)'
POST_CODE = {
    # libretro's Anti DMA (SuperCheats' B2809E31 3CEF5320 1C7B3231 B494738C) patches 0x08076BEE and the
    # save data still moves (Infinite Money did nothing on the cart); the Action Replay money code's own
    # Anti DMA (A57E2EDE A5AFF3E4, game ID BPEE) patches 0x08076BEA
    # + MoveSaveBlocks_ResetHeap's new money/coins/item-count key ("adds r4, r4, r0" -> "movs r4, #0"): the
    # game re-keys at every battle and map load (converting what's stored), so from the first one the key
    # is 0 and plain money/coin/item writes are real amounts (Infinite Money gave the bag 999999 XOR key)
    ('BPEE', 'Anti DMA'): '88076BEA+2400+88076CD6+2400',
}
for _c in ('BPEE', 'BPEP', 'BPEF'):     # SaveBlock2 play time: hours (0x0E) were left alone
    POST_CODE[(_c, 'Low Game Time')] = '82024A62+0000+82024A64+0001+82024A66+0000'
_EMERALD_NAMES = (
    ('Access Fly Map [Press Select]', 'Access Fly Map [Press A+Select]'),
    ('Have Flash [Press L+Select]', 'Have Flash [Press Select+Up]'),
    ('1-Hit Kill', '1-Hit Kill [Hold L]'),
    ('1-Hit Double Battle Kill (Instant Win)', '1-Hit Double Battle Kill [Hold L]'),
    ('1-Hit Win', '1-Hit Win [Hold R]'),
    ('100% catch Rate', '100% Catch Rate'),
    ('Gain 5000 Exp', 'Gain 5000 Exp [Press A+R]'),
    ('Receive Starter', 'Receive Starter [Press L+R+Down]'),
    ('Receive Symbol', 'Receive Symbol [Hold Select]'),
    ('Recieve More Exp', 'Receive More Exp'),
    ('The Human Bike', 'The Human Bike [B+Up On, B+Down Off]'),
    ('Berry Blender Re-Battle Activator', 'Berry Blender Re-Battle [Hold Select]'),
    ('Receive Balance Badge', 'Receive Balance Badge [Press Select+L+Up]'),
    ('Receive Dynamo Badge', 'Receive Dynamo Badge [Press Select+Down]'),
    ('Receive Feather Badge', 'Receive Feather Badge [Press Select+L+Right]'),
    ('Receive Heat Badge', 'Receive Heat Badge [Press Select+Left]'),
    ('Receive Knuckle Badge', 'Receive Knuckle Badge [Press Select+Right]'),
    ('Receive Mind Badge', 'Receive Mind Badge [Press Select+L+Down]'),
    ('Receive Rain Badge', 'Receive Rain Badge [Press Select+L+Left]'),
    ('Receive Stone Badge', 'Receive Stone Badge [Press Select+Up]'),
    ('Mystic Ticket', 'Mystic Ticket (one at a time)'),
    ('Aurora Ticket', 'Aurora Ticket (one at a time)'),
    ('Have Old Sea Map', 'Have Old Sea Map (one at a time)'),
    ('Shiny Groudon', 'Shiny Groudon (replaces PC Box 14, slot 1)'))
RENAME = {(c, a): b for c in ('BPEE', 'BPEP') for a, b in _EMERALD_NAMES}

# FireRed / LeafGreen (US): 1.0 (Rev 0) and 1.1 (Rev 1) share the game code and all their RAM, not their
# code. Checked against pret's byte-exact builds of all four ROMs: each ROM patch below fits one revision
# (on the other it lands inside unrelated code), so it's tagged "(Rev N)" - the cart leaves out cheats for
# the other revision (source/pde_cheats.c drop_other_revs) - and the other revision gets its own copy,
# found by the same original instructions (+0x14, or +0x78 in the Fly check). Anti DMA: SetSaveBlocksPointers'
# "ands r1, r0" (the random offset) becomes "movs r1, #0".
_FR_REV1 = ('No Random Battle', 'Skip Trainer Battles', 'Faster Text Display', "Opponent Can't Attack")
_FR_REV0 = ('Can Use Fly in Buildings, Caves, etc.', 'View Secret ID', "Catch Trainer's Pokemon", 'EXP Multiplier')
_EXP_REV1 = ('8203FF00+4803+8203FF02+B401+8203FF04+464A+8203FF06+8810+8203FF08+0040+8203FF0A+6008+8203FF0C+4689+'
             '8203FF0E+BD00+8203FF10+1D99+8203FF12+0802+88021D90+4800+88021D92+4700+88021D94+FF01+88021D96+0203')
RENAME[('BPRE', 'Anti DMA')] = 'Anti DMA (Rev 1)'
POST_CODE[('BPRE', 'Anti DMA')] = '8804C078+2100+8804C150+2400'   # + the money key, as Emerald's
for _n in _FR_REV1:
    RENAME[('BPRE', _n)] = _n + ' (Rev 1)'
for _n in _FR_REV0:
    RENAME[('BPRE', _n)] = _n + ' (Rev 0)'
RENAME[('BPRE', "Catch Trainer's Pokemon")] = "Catch Trainer's Pokemon [Hold L+R] (Rev 0)"
for _n in ('Can Use Fly in Buildings, Caves, etc.', 'View Secret Trainer ID', 'EXP Multiplier'):
    RENAME[('BPGE', _n)] = _n + ' (Rev 0)'
POST_CODE[('BPGE', 'View Secret Trainer ID')] = '880897D8+7B41+880897DC+7B00'   # was 0808981C: a typo, over a str
for _i in range(1, 31):     # GameHacking's "put item X in slot N" with the item lost: B+Up empties the slot
    POST_DROP[('BPGE', 'Slot %d' % _i)] = 'the item to put in the slot was lost (an XXXX code): B+Up empties that bag slot'
POST_DROP[('BPRE', 'Careful')] = 'its ROM patch lands in the wireless link code on both revisions'
POST_DROP[('BPRE', 'Naughty')] = 'its ROM patch lands in the movement code on both revisions'
# Ruby / Sapphire (US): writes the contest data in gSharedMem (0x02000000 + 0x192B6, sContestantStatus) every
# frame - other screens use that memory outside a contest
for _c in ('AXVE', 'AXPE'):
    RENAME[(_c, 'Max Hearts In Contests')] = 'Max Hearts In Contests (on only during a contest)'
# Code Breaker's way to stop the save data moving: freeze the random seed (gRngValue, 0x03005000) at a value
# that makes Random() return 0. It runs with every cheat and also freezes encounters, damage, catch odds and
# shininess; the Anti DMA ROM patches above stop the move by themselves
_RNG = 'auto code that freezes the random seed (gRngValue) every frame; Anti DMA (Rev 0/1) does its job'
POST_DROP[('BPRE', 'Disable DMA (Must Be On)')] = _RNG
POST_DROP[('BPGE', 'Pokemon Fire Red-Enable Code (Must Be On)')] = _RNG
POST_DROP[('BPGE', 'Pokemon Leaf Green-Enable Code (Must Be On)')] = _RNG
# Final Fantasy, Fire Emblem (checked Nov 2026): GameHacking value lists whose value was lost (the code
# writes 0, or item 1, into every slot every frame), and encrypted item lists decoded with the wrong key
# (one random value at a random ROM address each, some past the end of the ROM)
_LOST = 'the value to set was lost (an XXXX list): writes the same placeholder into that slot every frame'
_GARBLED = 'encrypted code decoded wrong: one random value at a random address (mostly ROM)'
POST_DROP_RE = [
    ('AE7E', r'^\d+(st|nd|rd|th) Position: Item Modifier Slot #\d+$', _LOST),
    ('AE7E', r'^Character \d+$', _LOST + ' (removes the unit)'),
    ('AE7E', r'^Slot \d+$', _LOST + ' (the unit\'s class)'),
    ('BFFE', r'^Item Modifier Slot \d+$', _LOST),
    ('BZ4J', r'^Item Slot Modifier \d+$', _LOST),
    ('BZ5E', r'^Item Slot \d+$', _LOST),
    ('BZ4E', r'^[0-9A-F]{4} - ', _GARBLED),
    ('BE8E', r'^(Brave Lance|Chest Key \(Five Uses\)|Demon Surge|Demonb Light|Juna Fruit|Killer Bow|Killing Edge|'
             r'Lightning|Neutral Phase Repeats Without Going to Other Phases.*|Ocean Seal|Orion\'s Belt|Purge|'
             r'Siegmund|Slim Sword|Stone Shard|Vidofnir|Warp)$', _GARBLED),
]
POST_DROP[('BZ6E', 'Map modifier')] = _LOST + ' (R warps to map 0)'
POST_DROP[('BZ6J', 'Map Modifier')] = _LOST + ' (R warps to map 0)'
POST_DROP[('BZ6E', 'Auction House Modifier')] = 'only runs with all 10 buttons held (the key value was lost)'
RENAME[('AFXE', 'Access Monster Bank [Press Select+Left]')] = 'Access Monster Bank [Press Select+Right]'
RENAME[('BZ4E', 'Access Armor Shop')] = 'Access Armor Shop [Press L+Select]'
RENAME[('BZ6J', 'Press L+Up To Instantly Win Battle')] = 'Press L+Select+Up To Instantly Win Battle'

POST_ADD = {
    'BPRE': [('Anti DMA (Rev 0)', '8804C064+2100+8804C13C+2400'),
             ('No Random Battle (Rev 0)', '880833B8+2000'),
             ('Skip Trainer Battles (Rev 0)', '88081BAE+E019'),
             ('Faster Text Display (Rev 0)', '880057F0+E01A'),
             ("Opponent Can't Attack (Rev 0)", '88015CD4+2801'),
             ('Can Use Fly in Buildings, Caves, etc. (Rev 1)', '88124B10+2001+880C514A+2001'),
             ('View Secret ID (Rev 1)', '88089818+7B41+8808981C+7B00'),
             ("Catch Trainer's Pokemon [Hold L+R] (Rev 1)", '8803DB00+2000+74000130+00FF+82022B4C+0004+74000130+00FF+82022B4E+0000'),
             ('EXP Multiplier (Rev 1)', _EXP_REV1)],
    'BPGE': [('Anti DMA (Rev 0)', '8804C064+2100+8804C13C+2400'),
             ('Anti DMA (Rev 1)', '8804C078+2100+8804C150+2400'),
             ('Can Use Fly in Buildings, Caves, etc. (Rev 1)', '88124AE8+2001+880C511E+2001'),
             ('View Secret Trainer ID (Rev 1)', '880897EC+7B41+880897F0+7B00'),
             ('EXP Multiplier (Rev 1)', _EXP_REV1)],
}

def post_fix(code, cheats, report):
    """the hand-checked fixes above, on one game's merged list"""
    # (also on a pack these fixes were already applied to: a renamed cheat still gets its corrected code,
    # and an added one is refreshed)
    old_name = {(k[0], v): k[1] for k, v in RENAME.items()}
    added = dict(POST_ADD.get(code, []))
    out = []
    for d, c in cheats:
        if (code, d) in POST_DROP:
            report.append((code, d, POST_DROP[(code, d)])); continue
        why = next((w for c2, rx, w in POST_DROP_RE if c2 == code and re.match(rx, d)), None)
        if why:
            report.append((code, d, why)); continue
        orig = old_name.get((code, d), d)
        out.append((RENAME.get((code, orig), d), added.get(d, POST_CODE.get((code, orig), c))))
    have = {n for n, _ in out}
    out += [(n, c) for n, c in POST_ADD.get(code, []) if n not in have]
    return out

# ---------------------------------------------------------------- reading
def read_cht(text):
    d = dict(re.findall(r'cheat(\d+)_desc\s*=\s*"([^"]*)"', text))
    c = dict(re.findall(r'cheat(\d+)_code\s*=\s*"([^"]*)"', text))
    return [(d[i].strip(), c.get(i, '').strip()) for i in sorted(d, key=int)]

def load_gh(src):
    files = {}
    if os.path.isdir(src):
        for n in os.listdir(src):
            files[n] = open(os.path.join(src, n), encoding='utf-8', errors='replace').read()
    else:
        z = zipfile.ZipFile(src)
        for n in z.namelist():
            files[os.path.basename(n)] = z.read(n).decode('utf-8', 'replace')
    rows = list(csv.DictReader(io.StringIO(files.pop('index.csv'))))
    return rows, files

# ---------------------------------------------------------------- game codes
def nointro(db):
    dat = open(os.path.join(db, 'metadat/no-intro/Nintendo - Game Boy Advance.dat'), encoding='utf-8', errors='replace').read()
    REG = ('USA', 'Europe', 'Japan', 'World', 'Australia', 'Germany', 'France', 'Spain', 'Italy', 'Korea', 'Netherlands', 'Sweden', 'China')
    def split(n):
        title = re.sub(r'\s*\(.*$', '', n).strip().lower()
        tags = re.findall(r'\(([^)]*)\)', n)
        regions = {r.strip() for t in tags for r in t.split(',') if r.strip() in REG}
        other = {t.lower() for t in tags if not any(x.strip() in REG for x in t.split(','))
                 and not re.fullmatch(r'[A-Z][a-z](\+[A-Z][a-z])?(,[A-Z][a-z](\+[A-Z][a-z])?)*', t)}
        return title, regions, other
    by_title = collections.defaultdict(list)
    for m in re.finditer(r'game \(\s*name "([^"]+)"(.*?)\n\)', dat, re.S):
        s = re.search(r'serial "([0-9A-Z]{4})"', m.group(2))
        if s:
            t, r, o = split(m.group(1)); by_title[t].append((r, o, s.group(1)))
    def match(name):
        # GoodTools-style names: (U) (E) (J) regions, [hI] (M4) (V1.0) tags
        name = re.sub(r'\[[^\]]*\]', '', name)
        name = re.sub(r'\((M\d+|V\d[.\d]*)\)', '', name)
        for a, b in (('(U)', '(USA)'), ('(E)', '(Europe)'), ('(J)', '(Japan)'), ('(UE)', '(USA, Europe)'), ('(F)', '(France)'), ('(G)', '(Germany)'), ('(S)', '(Spain)'), ('(I)', '(Italy)')):
            name = name.replace(a, b)
        name = re.sub(r'\s+', ' ', name).strip()
        name = name.replace("Disney's ", 'Disney ') if name.startswith("Disney's ") else name
        t, r, o = split(name)
        return sorted({c for r2, o2, c in by_title.get(t, []) if o2 == o and (r & r2 or 'World' in r | r2)})
    return match

# ---------------------------------------------------------------- code conversion
def ok_addr(t, a):
    if t == 0xD: return a == 0x20
    if t == 8 and 0x08000000 <= a < 0x0A000000: return True
    r = a >> 24
    return (r == 2 and a < 0x02100000) or (r == 3 and (a & 0xFFFFFF) < 0x8000) or (r == 4 and (a & 0xFFFFFF) < 0x400)

def plausible(lines):
    real, skip = 0, False
    for l in lines:
        if skip: skip = False; continue
        t = int(l[0], 16); a = int(l[:8], 16) & 0x0FFFFFFF
        if t in (0, 1): continue
        if t == 5: return real >= 0           # list code: data lines follow
        if t == 9 or not ok_addr(t, a): return False
        real += 1; skip = t == 4
    return real > 0

def mirror(lines):
    """EWRAM / IWRAM mirror addresses -> the real address"""
    out, skip = [], False
    for l in lines:
        if skip: out.append(l); skip = False; continue
        t = int(l[0], 16); a = int(l[:8], 16) & 0x0FFFFFFF
        if a >> 24 == 2: a = 0x02000000 | (a & 0x3FFFF)
        elif a >> 24 == 3: a = 0x03000000 | (a & 0x7FFF)
        out.append('%X%07X %s' % (t, a, l[9:])); skip = t == 4
    return out

def par_raw(pairs):
    """raw PAR v3, with fills done as Code Breaker slides / byte writes"""
    out, k, run = [], 0, []
    def flush():
        if run: out.extend(g.par_lines(run)); run.clear()
    while k < len(pairs):
        o1, o2 = pairs[k]
        if o1 == 0 and o2 >> 24 in (0x80, 0x82):
            flush()
            if k + 1 >= len(pairs): raise g.Bad('fill at end')
            v, p = pairs[k + 1]; k += 2
            a = g.par_addr(o2); rep = (p >> 16) & 0xFF; vs = p >> 24; w = 1 if o2 >> 24 == 0x80 else 2; ast = (p & 0xFFFF) * w
            if w == 2:
                out += ['4%07X %04X' % (a, v & 0xFFFF), '%08X %04X' % (rep | vs << 16, ast & 0xFFFF)]
            elif vs == 0 and ast == 1:
                b, e = a, a + rep
                if b & 1: out.append('3%07X %04X' % (b, v & 0xFF)); b += 1
                if e & 1: out.append('3%07X %04X' % (e - 1, v & 0xFF)); e -= 1
                if e > b: out += ['4%07X %04X' % (b, (v & 0xFF) * 0x101), '%08X %04X' % ((e - b) // 2, 2)]
            else:
                if rep > 256: raise g.Bad('fill too long')
                out += ['3%07X %04X' % (a + i * ast, (v + i * vs) & 0xFF) for i in range(rep)]
            continue
        run.append(pairs[k]); k += 1
    flush()
    return out

def convert(code, conv):
    toks = [t for t in re.split(r'[+\s]+', code) if t]
    if not toks: raise g.Bad('empty')
    if not all(re.fullmatch(r'[0-9A-Fa-f]+', t) for t in toks): raise g.Bad('needs a value typed in (XXXX)')
    toks = [t.upper() for t in toks]
    if len(toks) % 2 == 0 and all(len(toks[k]) == 8 and len(toks[k + 1]) == 4 for k in range(0, len(toks), 2)):
        lines = ['%s %s' % (toks[k], toks[k + 1]) for k in range(0, len(toks), 2)]
        if plausible(lines): return mirror(lines), 'cb'
        hook = all(l[0] in '01' for l in lines)
        if hook: return [], 'hook'
        raise g.Bad('Code Breaker code that is encrypted or broken')
    if len(toks) % 2 or not all(len(t) == 8 for t in toks): raise g.Bad('unknown code format')
    pairs = [(int(toks[k], 16), int(toks[k + 1], 16)) for k in range(0, len(toks), 2)]
    # raw PAR addresses past the 256 KB of EWRAM are mirrors: fold them back before decoding
    mir = lambda o: o & ~0x000C0000 if (o >> 20) & 0xF == 2 else o
    pmir = [(mir(a), b) if a else (a, (b & 0xFF000000) | mir(b & 0xFFFFFF) if b >> 24 in (0x80, 0x82, 0x84) else b) for a, b in pairs]
    tries = [('ar-raw', lambda: par_raw(pmir)), ('gs-raw', lambda: g.gsa_lines(pairs))]
    for name, fn in tries:
        try:
            lines = fn()
            if plausible(lines): return mirror(lines), name
        except (g.Bad, KeyError, IndexError):
            pass
    for which in ('par', 'gsa'):
        try:
            lines, (w, s) = conv._try_enc(pairs, which)
            if plausible(lines):
                setattr(conv, w, s); return mirror(lines), which
        except (g.Bad, KeyError, IndexError):
            pass
    # master codes: hooks only, nothing to run
    try:
        if all(l[0] in '01' for l in g.par_lines(pairs)): return [], 'hook'
    except (g.Bad, KeyError, IndexError):
        pass
    raise g.Bad('Action Replay / GameShark code that won\'t decode')

# ---------------------------------------------------------------- per game
def gh_game(code, entries, report):
    # GameHacking spells out XXXX codes: one code template repeated with different values,
    # each under its own name (or the same name many times). Those all go.
    def tmpl(c):
        t = [x for x in re.split(r'[+\s]+', c.upper()) if x]
        # values: every 2nd token, and any 8-digit token below 0x10000 (the value word of a PAR ROM patch)
        return ' '.join('*' if k % 2 or (len(x) == 8 and re.fullmatch(r'[0-9A-F]+', x) and int(x, 16) < 0x10000) else x
                        for k, x in enumerate(t))
    names_by_tmpl = collections.defaultdict(set)
    codes_by_name = collections.defaultdict(set)
    for d, c in entries:
        names_by_tmpl[tmpl(c)].add(d); codes_by_name[d].add(c)
    conv = g.Converter()
    out, seen = [], set()
    for d, c in entries:
        c = FIX.get((code, d), c)
        if (code, d) in DROP:
            report.append((code, d, DROP[(code, d)])); seen.add(key(d)); continue
        if len(codes_by_name[d]) > 1 and (code, d) not in FIX:
            if key(d) not in seen: report.append((code, d, 'value list from an XXXX code')); seen.add(key(d))
            continue
        if len(names_by_tmpl[tmpl(c)]) >= 3:
            report.append((code, d, 'value list from an XXXX code')); continue
        if key(d) in seen: continue
        try:
            lines, how = convert(c, conv)
        except g.Bad as e:
            report.append((code, d, str(e))); continue
        if not lines or all(l[0] in '01' for l in lines):
            report.append((code, d, 'master / must-be-on code, not needed')); continue
        cc = '+'.join(l.replace(' ', '+') for l in lines)
        if cc in [x for _, x in out]:
            report.append((code, d, 'same code as another cheat')); continue
        out.append((d, cc)); seen.add(key(d))
    return out

# The cart switches a cheat on by itself ("auto") when its name contains one of these
# (source/pde_cheats.c is_auto). Auto cheats go to the top of every list.
AUTO_TRIGGERS = ('Anti DMA', 'Anti-DMA', 'Must Be On', 'must be on')
REAL_AUTO = re.compile(r'^\W*(\(m\)|\[m\])?\W*(must be on|anti[- ]?dma|disable dma|enable code|[^()]*enable code)\b|\(must be on\)\s*$|^\W*must be on', re.I)

def fix_auto(code, cheats, report):
    """auto cheats first; names that only mention a trigger get reworded so the cart doesn't
    switch them on by itself; auto cheats may only do plain writes and ROM patches, since they
    run whenever any other cheat is on"""
    auto, rest = [], []
    for d, c in cheats:
        if not any(t in d for t in AUTO_TRIGGERS):
            rest.append((d, c)); continue
        if not REAL_AUTO.search(d):
            nd = d.replace('Anti-DMA', 'AntiDMA').replace('Anti DMA', 'AntiDMA').replace('Must Be On', 'Needs To Be On').replace('must be on', 'needs to be on')
            rest.append((nd, c)); continue
        lines = c.split('+'); types = {lines[k][0] for k in range(0, len(lines), 2)}
        if not types <= set('0138') or (code, d) in DROP:
            report.append((code, d, DROP.get((code, d), 'auto code that does more than plain writes / ROM patches'))); continue
        auto.append((d, c))
    return auto + rest

def key(name):
    return re.sub(r'[^a-z0-9]', '', name.lower())

def write_cht(path, cheats):
    L = ['cheats = %d' % len(cheats), '']
    for i, (d, c) in enumerate(cheats):
        L += ['cheat%d_desc = "%s"' % (i, d.replace('"', "'")), 'cheat%d_code = "%s"' % (i, c), 'cheat%d_enable = false' % i, '']
    open(path, 'w', encoding='utf-8', newline='\r\n').write('\n'.join(L))

def engine_check(dst, code, cheats, report, checker, tmp):
    """compile with the real Phosphor_DE cheat code; drop what it marks n/a"""
    d = os.path.join(tmp, 'SYSTEM', 'CHEATS'); os.makedirs(d, exist_ok=True)
    write_cht(os.path.join(d, code + '.cht'), cheats)
    r = subprocess.run([checker, code], capture_output=True, text=True, env=dict(os.environ, CHROOT=tmp), timeout=60)
    lines = r.stdout.splitlines()[1:1 + len(cheats)]
    keep = []
    for (dsc, c), l in zip(cheats, lines):
        if ' [n/a]' in l: report.append((code, dsc, 'Phosphor_DE cheat engine can\'t run it'))
        else: keep.append((dsc, c))
    if len(lines) != len(cheats): raise SystemExit('checker output mismatch for %s' % code)
    return keep

# ---------------------------------------------------------------- main
def main(src, lrpack, db, out):
    rows, files = load_gh(src)
    match = nointro(db)
    checker = os.path.join(tempfile.mkdtemp(), 'cht_check')
    subprocess.run(['gcc', '-O1', '-w', '-o', checker, os.path.join(HERE, 'cht_check.c')], check=True)
    report, nomatch = [], []
    by_code = collections.OrderedDict()
    # base versions before revisions, so a Rev's addresses don't win
    rows.sort(key=lambda r: ('rev' in r['region'].lower() or 'beta' in r['region'].lower() or 'proto' in r['region'].lower(), r['title'], r['region']))
    for r in rows:
        if r['file'] not in files: continue
        codes = sorted(set(re.findall(r'AGB-([0-9A-Z]{4})', r['serials']))) or match(r['title'] + ' ' + r['region'])
        if not codes:
            nomatch.append('%s %s [%s]' % (r['title'], r['region'], r['gamID'])); continue
        entries = [(d, c) for d, c in read_cht(files[r['file']]) if c]
        cheats = gh_game(codes[0], entries, report)
        for code in codes:
            lst = by_code.setdefault(code, [])
            names = {key(d) for d, _ in lst}; cs = {c for _, c in lst}
            lst += [(d, c) for d, c in cheats if key(d) not in names and c not in cs]
    # libretro extras
    lr_added = 0
    lrdir = os.path.join(lrpack, 'SYSTEM', 'CHEATS')
    for f in sorted(os.listdir(lrdir)):
        if not f.endswith('.cht'): continue
        code = f[:-4]
        lst = by_code.setdefault(code, [])
        names = {key(d) for d, _ in lst}; cs = {c for _, c in lst}
        for d, c in read_cht(open(os.path.join(lrdir, f), encoding='utf-8', errors='replace').read()):
            if c and key(d) not in names and c not in cs:
                lst.append((d, c)); names.add(key(d)); cs.add(c); lr_added += 1
    dst = os.path.join(out, 'SYSTEM', 'CHEATS')
    if os.path.isdir(dst): shutil.rmtree(dst)
    os.makedirs(dst)
    tmp = tempfile.mkdtemp()
    total = games = 0
    for code, cheats in by_code.items():
        if len(cheats) > CH_MAX:
            for d, _ in cheats[CH_MAX:]: report.append((code, d, 'over the 448-cheat list limit'))
            cheats = cheats[:CH_MAX]
        cheats = post_fix(code, cheats, report)
        cheats = fix_auto(code, cheats, report)
        cheats = engine_check(dst, code, cheats, report, checker, tmp)
        if not cheats: continue
        write_cht(os.path.join(dst, code + '.cht'), cheats)
        total += len(cheats); games += 1
    why = collections.Counter(w for _, _, w in report)
    open(os.path.join(out, 'CHEATS_SOURCE.txt'), 'w', encoding='utf-8', newline='\r\n').write(
        'Cheats in SYSTEM/CHEATS (one file per game code)\n\n'
        'GameHacking.org - https://gamehacking.org (GBA, LibRetro exports)\n'
        'libretro cheat database - https://github.com/libretro/libretro-database (CC BY-SA 4.0)\n\n'
        'Only ready-to-play codes are included: nothing has to be typed in or edited. Codes are stored as\n'
        'unencrypted Code Breaker; Action Replay / GameShark codes were converted (Anti-DMA patches kept).\n'
        'Every file was compiled with the Phosphor_DE cheat engine. Dropped codes: CHEATS_REPORT.txt\n')
    with open(os.path.join(out, 'CHEATS_REPORT.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('%d games, %d cheats\n\nDropped:\n' % (games, total))
        for w, n in why.most_common(): f.write('  %6d  %s\n' % (n, w))
        f.write('\nGameHacking games with no game code (not included):\n' + ''.join('  %s\n' % x for x in nomatch))
        f.write('\nEvery dropped code (game code | cheat | why):\n' + ''.join('%s | %s | %s\n' % x for x in report))
    print('%d games, %d cheats (%d from libretro), %d dropped, %d GH games unmatched' % (games, total, lr_added, len(report), len(nomatch)))
    for w, n in why.most_common(): print('  %6d  %s' % (n, w))

if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(__doc__); sys.exit(1)
    main(*sys.argv[1:])

"""GBA cheat code conversion for Phosphor_DE - by f3bandit

Turns Action Replay v3 (PAR v3 / AR MAX) and GameShark Advance (v1/v2) codes, encrypted or raw,
into the unencrypted Code Breaker lines the Phosphor_DE engine runs. ROM patches (Anti-DMA and
similar) become Code Breaker 16-bit writes to 0x08xxxxxx, which Phosphor_DE applies to the game
when it loads. Hooks / master codes / ID codes are dropped: the engine runs from the interrupt.

The decryption is the public GBA cheat-device algorithm (TEA with device seeds and reseeding),
the same one emulators use; the tables are the devices' standard reseed tables.
"""
import os
_here = os.path.dirname(os.path.abspath(__file__))
exec(open(os.path.join(_here, '_gsa_tables.py')).read())   # T1, T2
exec(open(os.path.join(_here, '_par_tables.py')).read())   # P1, P2

M = 0xFFFFFFFF
GSA_SEEDS = [0x09F4FBBD, 0x9681884A, 0x352027E9, 0xF3DEE5A7]
PAR_SEEDS = [0x7AA9648F, 0x7FAE6994, 0xC0EFAAD5, 0x42712C57]
MAX_EXPAND = 64      # most single-code repeats we expand into separate writes


def decrypt(o1, o2, s):
    sm = 0xC6EF3720
    for _ in range(32):
        o2 = (o2 - ((((o1 << 4) & M) + s[2]) ^ ((o1 + sm) & M) ^ ((o1 >> 5) + s[3]))) & M
        o1 = (o1 - ((((o2 << 4) & M) + s[0]) ^ ((o2 + sm) & M) ^ ((o2 >> 5) + s[1]))) & M
        sm = (sm - 0x9E3779B9) & M
    return o1, o2


def reseed(params, t1, t2):
    s = [0, 0, 0, 0]
    s0, s1 = params >> 8 & 0xFF, params & 0xFF
    for y in range(4):
        for x in range(4):
            z = (t1[(s0 + x) & 0xFF] + t2[(s1 + y) & 0xFF]) & 0xFF
            s[y] = ((s[y] << 8) | z) & M
    return s


class Bad(Exception):
    pass


def ram(a):
    r = a >> 24
    if r == 2 and a < 0x02040000: return True
    if r == 3 and (a & 0xFFFFFF) < 0x8000: return True
    if r == 4 and (a & 0xFFFFFF) < 0x400: return True
    return False


def w8(a, v):
    if not ram(a): raise Bad('address %08X' % a)
    return ['3%07X %04X' % (a & 0x0FFFFFFF, v & 0xFF)]


def w16(a, v):
    if not ram(a): raise Bad('address %08X' % a)
    return ['8%07X %04X' % (a & 0x0FFFFFFF, v & 0xFFFF)]


def w32(a, v):
    return w16(a, v) + w16(a + 2, v >> 16)


def wn(width, a, v):
    return {1: w8, 2: w16, 4: w32}[width](a, v)


def rom16(a, v):
    if not (0x08000000 <= a < 0x0A000000): raise Bad('rom %08X' % a)
    return ['8%07X %04X' % (a & 0x0FFFFFFF, v & 0xFFFF)]


def cond16(kind, a, v):
    if not ram(a): raise Bad('address %08X' % a)
    t = {'eq': '7', 'ne': 'A', 'gt': 'B', 'lt': 'C', 'and': 'F'}[kind]
    return '%s%07X %04X' % (t, a & 0x0FFFFFFF, v & 0xFFFF)


def guard(cond, codes):
    """Code Breaker conditions cover only the next line: repeat the condition before each line"""
    out = []
    for c in codes:
        if c.startswith('8') and int(c[1:8], 16) >= 0x08000000: raise Bad('conditional ROM patch')
        out += [cond, c]
    return out


# ------------------------------------------------------------------ PAR v3 (Action Replay MAX)
def par_addr(x):
    return (x & 0xFFFFF) | ((x << 4) & 0x0F000000)


def par_lines(pairs):
    """raw PAR v3 pairs -> Code Breaker lines"""
    out, i = [], 0
    block = None          # (cond, collected lines) for a block condition
    def emit(lines):
        if block is not None: block[1].extend(lines)
        else: out.extend(lines)
    while i < len(pairs):
        o1, o2 = pairs[i]; i += 1
        if o2 == 0x001DC0DE or o1 >> 24 == 0xC4: continue          # game ID / hook
        if o1 == 0:
            kind = o2 & 0xFF000000
            if o2 == 0: continue                                        # end of list
            if kind in (0x18000000, 0x1A000000, 0x1C000000, 0x1E000000):   # ROM patch, value on next line
                if i >= len(pairs): raise Bad('patch without value')
                v = pairs[i][0]; i += 1
                emit(rom16(0x08000000 | ((o2 & 0xFFFFFF) << 1), v))
                continue
            if kind == 0x40000000:                                      # endif
                if block is None: raise Bad('endif')
                out.extend(guard(block[0], block[1])); block = None
                continue
            if kind in (0x80000000, 0x82000000, 0x84000000):            # fill: next line = value, step/repeat
                if i >= len(pairs): raise Bad('fill')
                width = {0x80000000: 1, 0x82000000: 2, 0x84000000: 4}[kind]
                v, p = pairs[i]; i += 1
                a = par_addr(o2); rep = (p >> 16) & 0xFF; vstep = p >> 24; astep = (p & 0xFFFF) * width
                if rep > MAX_EXPAND: raise Bad('fill too long')
                for k in range(max(rep, 1)): emit(wn(width, a + k * astep, v + k * vstep))
                continue
            raise Bad('special %08X' % o2)                              # buttons, slowdown, else
        if o1 == 0xDEADFACE: raise Bad('reseed inside raw')
        if o1 & 0x38000000:                                             # condition
            width = 1 << ((o1 & 0x06000000) >> 25)
            if width != 2: raise Bad('%d-byte condition' % width)
            kind = {0x08000000: 'eq', 0x10000000: 'ne', 0x18000000: 'lt', 0x20000000: 'gt',
                    0x28000000: 'lt', 0x30000000: 'gt', 0x38000000: 'and'}[o1 & 0x38000000]
            c = cond16(kind, par_addr(o1), o2)
            act = o1 & 0xC0000000
            if act == 0xC0000000: raise Bad('disable condition')
            if act == 0x80000000:
                if block is not None: out.extend(guard(block[0], block[1]))
                block = (c, []); continue
            n = 1 if act == 0 else 2
            take = []
            for _ in range(n):
                if i >= len(pairs): raise Bad('condition at end')
                sub = par_lines([pairs[i]]); i += 1
                take += sub
            emit(guard(c, take)); continue
        width = 1 << ((o1 & 0x06000000) >> 25)
        base = o1 & 0xC0000000
        if o1 & 0x01000000 and (o1 & 0xFE000000) != 0xC6000000: raise Bad('type %08X' % o1)
        if base == 0x00000000:                                          # assign (with repeat for 1/2 bytes)
            if width == 8: raise Bad('width')
            rep = (o2 >> (width * 8)) + 1 if width < 4 else 1
            if rep > MAX_EXPAND: raise Bad('repeat too long')
            for k in range(rep): emit(wn(width, par_addr(o1) + k * width, o2))
        elif base == 0x80000000:                                        # add
            if width != 2: raise Bad('%d-byte add' % width)
            if not ram(par_addr(o1)): raise Bad('address')
            emit(['E%07X %04X' % (par_addr(o1) & 0x0FFFFFFF, o2 & 0xFFFF)])
        elif base == 0xC0000000:                                        # IO writes
            w = ((o1 >> 24) & 1) + 1
            emit(wn(w, 0x04000000 | (o1 & 0xFFFFFF), o2))
        else:
            raise Bad('pointer code')
    if block is not None: out.extend(guard(block[0], block[1]))
    return out


# ------------------------------------------------------------------ GameShark Advance v1/v2
def gsa_lines(pairs):
    out, i = [], 0
    while i < len(pairs):
        o1, o2 = pairs[i]; i += 1
        t = o1 >> 28; a = o1 & 0x0FFFFFFF
        if t == 0: out += w8(a, o2)
        elif t == 1: out += w16(a, o2)
        elif t == 2: out += w32(a, o2)
        elif t == 3:                                   # one 32-bit value to a list of addresses
            n = (o1 & 0xFFFF)
            addrs = [o2]
            while len(addrs) < n:
                if i >= len(pairs): raise Bad('list')
                addrs += [pairs[i][0], pairs[i][1]]; i += 1
            for ad in addrs[:n]: out += w32(ad, 0)  # value is in the header's high bits on real devices
            raise Bad('assign list')
        elif t == 6: out += rom16(0x08000000 | ((o1 & 0xFFFFFF) << 1), o2)
        elif t == 0xD:
            kind = {0: 'eq', 1: 'ne'}.get(o2 >> 20)
            if kind is None: raise Bad('if type')
            if i >= len(pairs): raise Bad('if at end')
            nxt = gsa_lines([pairs[i]]); i += 1
            out += guard(cond16(kind, a, o2), nxt)
        elif t == 0xE:
            n = (o1 >> 16) & 0xFF
            c = cond16('eq', o2 & 0x0FFFFFFF, o1)
            take = []
            for _ in range(n):
                if i >= len(pairs): raise Bad('range')
                take += gsa_lines([pairs[i]]); i += 1
            out += guard(c, take)
        elif t == 0xF: pass                            # hook
        else: raise Bad('GSA type %X' % t)
    return out


class Converter:
    """keeps the decryption state across the cheats of one game, in file order"""
    def __init__(self):
        self.par = PAR_SEEDS[:]
        self.gsa = GSA_SEEDS[:]

    def _dec(self, pairs, seeds, t1, t2):
        out, s = [], seeds[:]
        for o1, o2 in pairs:
            d1, d2 = decrypt(o1, o2, s)
            if d1 == 0xDEADFACE:
                s = reseed(d2 & 0xFFFF, t1, t2); continue
            out.append((d1, d2))
        return out, s

    def convert(self, code):
        """code text -> (list of Code Breaker lines, how) ; raises Bad"""
        toks = [t for t in code.replace(' ', '+').replace('\n', '+').split('+') if t]
        if not all(all(ch in '0123456789abcdefABCDEF' for ch in t) for t in toks):
            raise Bad('not hex (needs a value filled in)')
        if all(len(t) in (8, 4) for t in toks) and len(toks) % 2 == 0 and all(len(toks[k]) == 8 and len(toks[k + 1]) == 4 for k in range(0, len(toks), 2)):
            return ['%s %s' % (toks[k].upper(), toks[k + 1].upper()) for k in range(0, len(toks), 2)], 'cb'
        if len(toks) % 2 or not all(len(t) == 8 for t in toks):
            raise Bad('unknown format')
        pairs = [(int(toks[k], 16), int(toks[k + 1], 16)) for k in range(0, len(toks), 2)]
        tries = []
        # raw first (only if it makes sense), then encrypted with the running seeds
        # raw PAR ROM-patch lists ("00000000 18xxxxxx") are common in plain text; anything else is
        # tried encrypted first, since random encrypted data can pass for a raw code by chance
        raw_par_first = all(o1 == 0 or o1 < 0x10000 for o1, _ in pairs)
        order = [('par-raw', lambda: (par_lines(pairs), None))] if raw_par_first else []
        order += [('par', lambda: self._try_enc(pairs, 'par')),
                  ('gsa', lambda: self._try_enc(pairs, 'gsa'))]
        if not raw_par_first:
            order += [('par-raw', lambda: (par_lines(pairs), None))]
        order += [('gsa-raw', lambda: (gsa_lines(pairs), None))]
        for name, fn in order:
            try:
                lines, newseeds = fn()
            except (Bad, KeyError) as e:
                tries.append('%s: %s' % (name, e)); continue
            if newseeds:
                which, s = newseeds
                setattr(self, which, s)
            return lines, name
        raise Bad('; '.join(tries))

    def _try_enc(self, pairs, which):
        if which == 'par':
            dec, s = self._dec(pairs, self.par, P1, P2)
            lines = par_lines(dec)
            return lines, ('par', s)
        dec, s = self._dec(pairs, self.gsa, T1, T2)
        lines = gsa_lines(dec)
        return lines, ('gsa', s)

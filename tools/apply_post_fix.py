#!/usr/bin/env python3
"""Phosphor_DE - apply make_gh_pack.py's hand-checked fixes (POST_DROP, POST_CODE, RENAME, POST_ADD) to a pack
that's already built, without the GameHacking download - by f3bandit

  python apply_post_fix.py <folder with SYSTEM/CHEATS> [<CHEATS_REPORT.txt>]

Only the games named in those lists (and POST_ADD) are touched. Each is put through the same steps as a full build
(post_fix, fix_auto, the Phosphor_DE cheat engine check), and the report's totals and list are updated.
Running it twice changes nothing the second time.
"""
import os, re, sys, collections, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_gh_pack as p


def update_report(path, dropped, removed_cheats):
    t = open(path, encoding='utf-8').read().replace('\r\n', '\n')
    head, sep, lst = t.partition('\nEvery dropped code (game code | cheat | why):\n')
    if not sep:
        raise SystemExit('%s: no "Every dropped code" list' % path)
    # (some names in the list hold line breaks, so it isn't re-read: new entries are added, counts bumped)
    new = [x for x in dropped if '%s | %s | %s\n' % x not in lst]
    m = re.match(r'(\d+) games, (\d+) cheats\n\nDropped:\n((?: *\d+  .*\n)*)', head)
    if not m:
        raise SystemExit('%s: unexpected report header' % path)
    why = collections.Counter({w: int(n) for n, w in re.findall(r' *(\d+)  (.*)\n', m.group(3))})
    why.update(w for _, _, w in new)
    out = '%s games, %d cheats\n\nDropped:\n' % (m.group(1), int(m.group(2)) - removed_cheats)
    out += ''.join('%8d  %s\n' % (n, w) for w, n in why.most_common())
    out += head[m.end():] + sep + lst + ''.join('%s | %s | %s\n' % x for x in new)
    open(path, 'w', encoding='utf-8', newline='\r\n').write(out)


def main(root, report_path=None):
    dst = os.path.join(root, 'SYSTEM', 'CHEATS')
    codes = sorted({c for c, _ in list(p.POST_DROP) + list(p.POST_CODE) + list(p.RENAME)} | set(p.POST_ADD))
    checker = os.path.join(tempfile.mkdtemp(), 'cht_check')
    subprocess.run(['gcc', '-O1', '-w', '-o', checker, os.path.join(p.HERE, 'cht_check.c')], check=True)
    tmp = tempfile.mkdtemp()
    report, removed = [], 0
    for code in codes:
        f = os.path.join(dst, code + '.cht')
        if not os.path.isfile(f):
            continue
        before = p.read_cht(open(f, encoding='utf-8', errors='replace').read())
        cheats = p.post_fix(code, before, report)
        cheats = p.fix_auto(code, cheats, report)
        cheats = p.engine_check(dst, code, cheats, report, checker, tmp)
        p.write_cht(f, cheats)
        removed += len(before) - len(cheats)
        print('%s: %d -> %d cheats' % (code, len(before), len(cheats)))
    if report_path:
        update_report(report_path, report, removed)
    for x in report:
        print('  dropped: %s | %s | %s' % x)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    main(*sys.argv[1:])

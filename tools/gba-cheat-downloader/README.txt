GBA Cheat Downloader - browser extension - by f3bandit

Downloads the LibRetro cheat file of every GBA game on GameHacking.org (about 1,500 games)
and saves them in one zip, in a folder you choose. Works in Brave, Chrome and Edge.

INSTALL (one time)
1. Open brave://extensions   (Chrome: chrome://extensions, Edge: edge://extensions)
2. Turn ON "Developer mode" (top-right corner).
3. Click "Load unpacked" and select this folder (gba-cheat-downloader).
4. Click the puzzle-piece icon next to the address bar and pin "GBA Cheat Downloader".
   Leave this folder where it is; the browser loads the extension from it.

USE
1. Click the gold GBA button. It opens gamehacking.org in a new tab. If a Cloudflare check shows,
   pass it, wait for GameHacking to load, then click the GBA button again.
2. Click "Start" in the box at the bottom-right of the page.
3. Keep the tab open (20-30 minutes). "Stop" ends early and still saves what it has.
4. A Save window opens at the end: pick a folder and save gba_cheats_gamehacking.zip.
   Closed it by mistake? Click "Save zip again".

THE ZIP
- one .cht per game, e.g. "Final Fantasy Tactics Advance (USA) [4576].cht" ([4576] = GameHacking id)
- index.csv  - id, title, region, serials (AGB-AFXE-USA), number of codes, file
- failed.txt - only if some games couldn't be fetched; run it again later

Turn it into a Phosphor_DE cheat pack with tools/make_gh_pack.py (see the main README).

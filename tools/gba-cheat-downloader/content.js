// GBA Cheat Downloader - by f3bandit
// Runs inside gamehacking.org pages, so the requests go out with your normal browser session
// (GameHacking's Cloudflare check blocks anything else). Reads the full GBA game list, fetches
// the LibRetro export of every game, packs them with index.csv into one zip, then the extension
// opens a Save dialog so you choose where it goes.
let ghgRunning = false;
chrome.runtime.onMessage.addListener(m => { if (m.cmd === 'show') ghgShow(); });

function ghgShow() {
  if (document.getElementById('ghg_box')) return;
  const box = document.createElement('div');
  box.id = 'ghg_box';
  box.style.cssText = 'position:fixed;z-index:2147483647;right:12px;bottom:12px;width:380px;padding:12px;background:#16161a;color:#ddd;font:12px/1.5 monospace;border:1px solid #c8a040;border-radius:6px;box-shadow:0 4px 20px #000a';
  box.innerHTML = '<b style="color:#e0b050">GBA Cheat Downloader</b>' +
    '<div id="ghg_s" style="margin:8px 0;white-space:pre-wrap">Downloads the cheats for every GBA game on GameHacking.org (about 1,500 games, 20-30 minutes). ' +
    'Keep this tab open. At the end you choose where to save the zip.</div>' +
    '<button id="ghg_go">Start</button> <button id="ghg_x">Close</button>';
  // buttons styled outright so the page's own button styles can't wash them out
  const btn = 'font:bold 12px monospace;padding:4px 14px;margin-right:6px;border-radius:3px;cursor:pointer;';
  box.querySelector('#ghg_go').style.cssText = btn + 'background:#e0b050;color:#16161a;border:1px solid #e0b050';
  box.querySelector('#ghg_x').style.cssText = btn + 'background:#2a2a30;color:#eee;border:1px solid #777';
  document.body.appendChild(box);
  box.querySelector('#ghg_x').onclick = () => { if (!ghgRunning) box.remove(); };
  box.querySelector('#ghg_go').onclick = () => { box.querySelector('#ghg_go').remove(); ghgRun(box); };
}

async function ghgRun(box) {
  ghgRunning = true;
  let stop = false;
  const say = t => { box.querySelector('#ghg_s').textContent = t; };
  const x = box.querySelector('#ghg_x');
  x.textContent = 'Stop'; x.onclick = () => { stop = true; say('Stopping... the cheats so far still get saved.'); };
  const sleep = ms => new Promise(r => setTimeout(r, ms));

  // ---- zip writer (deflate, UTF-8 names)
  const parts = [], cd = []; let off = 0;
  const T = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = b => { let c = ~0; for (let i = 0; i < b.length; i++) c = T[(c ^ b[i]) & 255] ^ (c >>> 8); return ~c >>> 0; };
  const now = new Date(), dt = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1), dd = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
  const hdr = (sig, f) => { const b = new DataView(new ArrayBuffer(f.reduce((a, v) => a + v[1], 4))); b.setUint32(0, sig, true); let p = 4; for (const [v, n] of f) { n == 2 ? b.setUint16(p, v, true) : b.setUint32(p, v, true); p += n; } return new Uint8Array(b.buffer); };
  const add = async (name, text) => {
    const raw = new TextEncoder().encode(text), nm = new TextEncoder().encode(name);
    const z = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new CompressionStream('deflate-raw'))).arrayBuffer());
    const f = [[20, 2], [0x0800, 2], [8, 2], [dt, 2], [dd, 2], [crc(raw), 4], [z.length, 4], [raw.length, 4], [nm.length, 2], [0, 2]];
    const lh = hdr(0x04034b50, f);
    cd.push(hdr(0x02014b50, [[20, 2]].concat(f, [[0, 2], [0, 2], [0, 2], [0, 4], [off, 4]])), nm);
    parts.push(lh, nm, z); off += lh.length + nm.length + z.length;
  };
  const zipBlob = () => {
    const n = cd.length / 2, size = cd.reduce((a, v) => a + v.length, 0);
    return new Blob(parts.concat(cd, [hdr(0x06054b50, [[0, 2], [0, 2], [n, 2], [n, 2], [size, 4], [off, 4], [0, 2]])]), { type: 'application/zip' });
  };

  const clean = s => s.replace(/[<>:"\/\\|?*\x00-\x1f]/g, '_').replace(/\s+/g, ' ').trim().replace(/[. ]+$/, '').slice(0, 150);
  const get = async (url, opt) => {
    for (let k = 0; k < 4; k++) {
      try { const r = await fetch(url, Object.assign({ credentials: 'include' }, opt)); if (r.ok) return await r.text(); } catch (e) {}
      await sleep(3000 * (k + 1));
    }
    return null;
  };

  // ---- 1. game list
  const games = [], seen = new Set();
  for (let p = 0; p < 300 && !stop; p++) {
    say('Reading the game list, page ' + (p + 1) + '...  (' + games.length + ' games so far)');
    const t = await get('/system/gba/all/' + p);
    if (t == null) { say('Could not read the game list. Reload this page (pass the Cloudflare check if it shows one) and click the extension button again.'); ghgRunning = false; x.textContent = 'Close'; x.onclick = () => box.remove(); return; }
    const d = new DOMParser().parseFromString(t, 'text/html'), tb = d.querySelector('table');
    let title = '', added = 0;
    for (const r of tb ? tb.rows : []) {
      const th = r.querySelector('th[colspan]'); if (th) { title = th.textContent.trim(); continue; }
      const a = r.querySelector('a[href^="/game/"]'); if (!a) continue;
      const id = a.getAttribute('href').split('/')[2]; if (seen.has(id)) continue;
      const td = r.querySelectorAll('td');
      seen.add(id); added++;
      games.push({ id, title, region: a.textContent.trim(), serial: td[1] ? td[1].textContent.trim() : '', n: parseInt(td[4] ? td[4].textContent : '0') || 0 });
    }
    if (!added) break;
    await sleep(300);
  }
  const todo = games.filter(g => g.n > 0);

  // ---- 2. one LibRetro export per game
  let got = 0; const failed = [];
  for (let i = 0; i < todo.length && !stop; i++) {
    const g = todo[i];
    g.file = clean(g.title + ' ' + g.region) + ' [' + g.id + '].cht';
    say('Game ' + (i + 1) + ' of ' + todo.length + '\n' + g.title + ' ' + g.region + '\nsaved ' + got + ', failed ' + failed.length + '\n\nKeep this tab open.');
    const b = new URLSearchParams({ format: 'LibRetro', sysID: '5', gamID: g.id, codID: '', filename: 'x', download: 'true' });
    const t = await get('/inc/sub.exportCodes.php', { method: 'POST', body: b });
    if (t && /^\s*cheats\s*=/.test(t)) { await add(g.file, t); got++; }
    else { g.file = ''; failed.push(g.id + '\t' + g.title + ' ' + g.region); }
    await sleep(500);
  }

  // ---- 3. index + save
  const q = s => '"' + String(s).replace(/"/g, '""') + '"';
  await add('index.csv', 'gamID,title,region,serials,codes,file\r\n' +
    todo.filter(g => g.file).map(g => [g.id, q(g.title), q(g.region), q(g.serial), g.n, q(g.file)].join(',')).join('\r\n') + '\r\n');
  if (failed.length) await add('failed.txt', failed.join('\r\n') + '\r\n');
  const url = URL.createObjectURL(zipBlob()), name = 'gba_cheats_gamehacking.zip';
  const summary = (stop ? 'Stopped. ' : 'Done. ') + got + ' games saved' + (failed.length ? ', ' + failed.length + ' failed (listed in failed.txt inside the zip)' : '') + '.';
  say(summary + '\nChoose where to save the zip.');
  let r = null;
  try { r = await chrome.runtime.sendMessage({ cmd: 'save', url, name }); } catch (e) {}
  const link = document.createElement('a');
  link.href = url; link.download = name; link.textContent = 'Save zip again';
  link.style.cssText = 'display:inline-block;margin-right:6px;padding:4px 14px;font:bold 12px monospace;background:#e0b050;color:#16161a;border-radius:3px;text-decoration:none';
  x.before(link);
  if (!(r && r.ok)) link.click();
  say(summary + '\nIf you closed the Save dialog, click "Save zip again".');
  ghgRunning = false; x.textContent = 'Close'; x.onclick = () => box.remove();
}

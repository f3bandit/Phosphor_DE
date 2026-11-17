// GBA Cheat Downloader - by f3bandit
// Toolbar button: open (or reuse) a gamehacking.org tab and show the downloader box there.
const LIST = 'https://gamehacking.org/system/gba';
const show = tabId => chrome.tabs.sendMessage(tabId, { cmd: 'show' }).catch(() => {});

chrome.action.onClicked.addListener(async tab => {
  if (tab.url && tab.url.startsWith('https://gamehacking.org/')) { show(tab.id); return; }
  const t = await chrome.tabs.create({ url: LIST });
  const ready = (id, info) => { if (id === t.id && info.status === 'complete') { chrome.tabs.onUpdated.removeListener(ready); show(t.id); } };
  chrome.tabs.onUpdated.addListener(ready);
});

// the zip is built in the page; this opens the Save dialog for it
chrome.runtime.onMessage.addListener((m, sender, reply) => {
  if (m.cmd !== 'save') return;
  chrome.downloads.download({ url: m.url, filename: m.name, saveAs: true })
    .then(id => reply({ ok: true, id }), e => reply({ ok: false, err: String(e) }));
  return true;
});

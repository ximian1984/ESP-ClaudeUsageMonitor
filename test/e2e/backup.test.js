// Futtatas (a dongle-lal azonos halozaton, vagy SSH-alagutton: ssh -N -L 18080:<dongle-ip>:80 <gep>):
//   CMON_URL=http://127.0.0.1:18080 CMON_PW_FILE=/biztonsagos/hely/jelszo.txt node test/e2e/<fajl>.js
//   (PLAYWRIGHT_MODULE: ha a playwright globalisan van, pl. /opt/homebrew/lib/node_modules/playwright)
// A jelszo SOHA nincs a repoban: fajlbol olvasva, amit a futtato a hasznalat utan torol.
// Export/import E2E. Titkot NEM ir ki: a secrets-exportbol csak kulcs-jelenletet es hosszt nez, a fajlt torli.
// Import csak a TITOK NELKULI exporttal (a meglevo tokenek megmaradasat merjuk). A meglevo profilokhoz nem nyul.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('fs');
const B = process.env.CMON_URL || 'http://127.0.0.1:18080';
const PW = fs.readFileSync(process.env.CMON_PW_FILE, 'utf8');
const results = [];
const ok = (n, c, i = '') => results.push([c ? 'OK  ' : 'FAIL', n, i]);
const msg = (p) => p.locator('#msg').textContent();

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, acceptDownloads: true });
  const page = await ctx.newPage();
  const errs = [];
  page.on('pageerror', e => errs.push(String(e)));
  page.on('dialog', d => d.accept());
  await page.goto(B + '/');
  await page.waitForSelector('#f', { state: 'visible' });
  await page.fill('#p', PW);
  await page.click('#f button');
  await page.waitForFunction(() => document.querySelectorAll('#claudeList tr').length > 0, null, { timeout: 15000 });
  const token = await page.evaluate(() => sessionStorage.getItem('sid'));
  const api = async (path) => (await page.request.get(B + path, { headers: { 'X-CMon-Token': token } }));
  const before = await (await api('/api/config')).json();

  // 1. export titok nelkul
  let [dl] = await Promise.all([page.waitForEvent('download'), page.locator('button', { hasText: 'Export settings' }).click()]);
  const f1 = __dirname + '/exp-nosecret.json';
  await dl.saveAs(f1);
  const e1 = JSON.parse(fs.readFileSync(f1, 'utf8'));
  ok('export fajlnev', /^device-config-\d{4}-\d\d-\d\d\.json$/.test(dl.suggestedFilename()), dl.suggestedFilename());
  const txt1 = fs.readFileSync(f1, 'utf8');
  ok('export: nincs titok-kulcs', !/"password"|"auth"|"refresh"/.test(txt1) && e1.secrets === false);
  ok('export: tartalom', e1.wifi.length === before.wifi.length && e1.claude.length === before.claude.length,
    `wifi ${e1.wifi.length}, claude ${e1.claude.map(c => c.name)}, tz ${e1.tz}, rot ${e1.rotationSec}, refr ${e1.refreshSec}`);

  // 2. export titokkal (csak jelenlet + hossz; a fajl torolve)
  await page.locator('#expSecrets').check();
  [dl] = await Promise.all([page.waitForEvent('download'), page.locator('button', { hasText: 'Export settings' }).click()]);
  const f2 = __dirname + '/exp-secret.json';
  await dl.saveAs(f2);
  const e2 = JSON.parse(fs.readFileSync(f2, 'utf8'));
  fs.unlinkSync(f2);
  ok('secrets-export fajlnev', /-SECRETS\.json$/.test(dl.suggestedFilename()), dl.suggestedFilename());
  ok('secrets-export: jelszavak/tokenek benne', e2.secrets === true && e2.wifi.every(w => typeof w.password === 'string')
    && e2.claude.every(c => typeof c.auth === 'string' && typeof c.refresh === 'string'),
    'claude token hosszak: ' + e2.claude.map(c => `${c.name} auth=${c.auth.length} refresh=${c.refresh.length}`).join('; '));
  await page.locator('#expSecrets').uncheck();

  // 3. titok-export token nelkul -> 401
  const r401 = await page.request.get(B + '/api/export?secrets=1');
  ok('export token nelkul -> 401', r401.status() === 401, String(r401.status()));

  // 4. hibas fajl import -> hiba, semmi nem valtozik
  const bad = __dirname + '/bad.json';
  fs.writeFileSync(bad, JSON.stringify({ format: 'mas', version: 1 }));
  await page.setInputFiles('#impFile', bad);
  await page.locator('button', { hasText: 'Import settings' }).click();
  await page.waitForFunction(() => /format\/version|not a/.test(document.getElementById('msg').textContent), null, { timeout: 10000 });
  ok('rossz formatum -> hiba', true, await msg(page));

  // 5. titok nelkuli export visszatoltese -> minden marad, tokenek/jelszavak megmaradnak
  await page.setInputFiles('#impFile', f1);
  await page.locator('button', { hasText: 'Import settings' }).click();
  await page.waitForFunction(() => /Imported:/.test(document.getElementById('msg').textContent), null, { timeout: 15000 });
  ok('import', true, await msg(page));
  await new Promise(r => setTimeout(r, 2000));
  const after = await (await api('/api/config')).json();
  const strip = (c) => JSON.stringify({ w: c.wifi.map(w => [w.ssid, w.enabled, w.priority, w.hasPassword]),
    c: c.claude.map(x => [x.name, x.transport, x.enabled, x.hasAuth, x.hasRefresh]), tz: c.tz, r: c.rotationSec, f: c.refreshSec });
  ok('import utan konfig azonos (jelszo/token megmaradt)', strip(before) === strip(after), strip(after));

  ok('nincs JS-hiba', errs.length === 0, errs.join(' | '));
  fs.unlinkSync(bad);
  fs.unlinkSync(f1);
  await browser.close();
  for (const r of results) console.log(r[0], r[1], r[2] ? '— ' + r[2] : '');
  console.log('FAIL db:', results.filter(r => r[0] !== 'OK  ').length, '/', results.length);
})().catch(e => { for (const r of results) console.log(r[0], r[1], r[2] ? '— ' + r[2] : ''); console.log('MEGSZAKADT:', e.message.split('\n')[0]); process.exit(1); });

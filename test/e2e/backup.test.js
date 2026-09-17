// Futtatas (a dongle-lal azonos halozaton, vagy SSH-alagutton: ssh -N -L 18080:<dongle-ip>:80 <gep>):
//   CMON_URL=http://127.0.0.1:18080 CMON_PW_FILE=/biztonsagos/hely/jelszo.txt node test/e2e/backup.test.js
//   PLAYWRIGHT_MODULE: ha a playwright globalisan van (pl. /opt/homebrew/lib/node_modules/playwright)
//   CMON_PYTHON: python, amiben van "cryptography" -> a titkositott fajlt a dongle-tol FUGGETLENUL is visszafejti
// A jelszo SOHA nincs a repoban. Titkot nem ir ki: csak kulcs-jelenletet es hosszt; a letoltott fajlok az OS temp-
// konyvtarba kerulnek es a vegen torlodnek.
// Export/import E2E: olvashato export (titok nelkul), titkositott export (rossz/jo jelszo), fuggetlen visszafejtes,
// titkositott import rossz/jo jelszoval, titok nelkuli import. A konfiguracionak a vegen azonosnak kell lennie.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');
const B = process.env.CMON_URL || 'http://127.0.0.1:18080';
const PW_FILE = process.env.CMON_PW_FILE;
const PW = fs.readFileSync(PW_FILE, 'utf8');
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'cmon-e2e-'));
const results = [];
const ok = (n, c, i = '') => results.push([c ? 'OK  ' : 'FAIL', n, i]);
const msg = (p) => p.locator('#msg').textContent();
const waitMsg = (p, re, t = 30000) => p.waitForFunction((s) => new RegExp(s).test(document.getElementById('msg').textContent), re.source, { timeout: t });

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
  const cfgNow = async () => (await page.request.get(B + '/api/config', { headers: { 'X-CMon-Token': token } })).json();
  const strip = (c) => JSON.stringify({ w: c.wifi.map(w => [w.ssid, w.enabled, w.priority, w.hasPassword]),
    c: c.claude.map(x => [x.name, x.transport, x.enabled, x.hasAuth, x.hasRefresh]), tz: c.tz, r: c.rotationSec, f: c.refreshSec });
  const before = await cfgNow();
  const exportBtn = page.locator('button', { hasText: 'Export settings' });
  const importBtn = page.locator('button', { hasText: 'Import settings' });

  // 1. olvashato export, titok nelkul
  let [dl] = await Promise.all([page.waitForEvent('download'), exportBtn.click()]);
  const fPlain = path.join(TMP, 'plain.json');
  await dl.saveAs(fPlain);
  const plainTxt = fs.readFileSync(fPlain, 'utf8');
  const ePlain = JSON.parse(plainTxt);
  ok('olvashato export: fajlnev', /^device-config-\d{4}-\d\d-\d\d\.json$/.test(dl.suggestedFilename()), dl.suggestedFilename());
  ok('olvashato export: nincs titok', !/"password"|"auth"|"refresh"/.test(plainTxt) && ePlain.secrets === false,
    `wifi ${ePlain.wifi.length}, claude ${ePlain.claude.map(c => c.name)}`);
  const legacy = await page.request.get(B + '/api/export?secrets=1', { headers: { 'X-CMon-Token': token } });
  ok('titok olvashato formaban nem kerheto', legacy.status() === 400, String(legacy.status()));

  // 2. titkositott export: jelszo nelkul / rossz jelszoval / jo jelszoval
  await page.locator('#expSecrets').check();
  ok('jelszomezo megjelenik', await page.locator('#expPw').isVisible());
  await exportBtn.click();
  await waitMsg(page, /Enter the admin password/);
  ok('jelszo nelkul -> kerdezi', true, await msg(page));
  await page.fill('#expPw', 'rossz-jelszo-xyz');
  await exportBtn.click();
  await waitMsg(page, /wrong admin password/);
  ok('rossz admin-jelszo -> elutasitva', true, await msg(page));
  await page.fill('#expPw', PW);
  const t0 = Date.now();
  [dl] = await Promise.all([page.waitForEvent('download', { timeout: 60000 }), exportBtn.click()]);
  const encMs = Date.now() - t0;
  const fEnc = path.join(TMP, 'enc.json');
  await dl.saveAs(fEnc);
  const encTxt = fs.readFileSync(fEnc, 'utf8');
  const eEnc = JSON.parse(encTxt);
  ok('titkositott export: fajlnev', /-ENCRYPTED\.json$/.test(dl.suggestedFilename()), `${dl.suggestedFilename()}, ${encMs} ms`);
  const leak = [...before.wifi.map(w => w.ssid), ...before.claude.map(c => c.name), 'password', 'refresh', 'sk-ant']
    .filter(s => encTxt.includes(s));
  ok('titkositott fajlban nincs olvashato adat', eEnc.format === 'device-config-encrypted' && leak.length === 0,
    `kdf ${eEnc.kdf} ${eEnc.iterations} kor, ${eEnc.cipher}, data ${eEnc.data.length} b64 kar.` + (leak.length ? ' SZIVAROG: ' + leak : ''));
  ok('jelszomezo kiurult', (await page.locator('#expPw').inputValue()) === '');
  await page.locator('#expSecrets').uncheck();

  // 3. fuggetlen visszafejtes (Python cryptography), csak osszegzes
  if (process.env.CMON_PYTHON) {
    const tool = path.join(__dirname, '..', '..', 'tools', 'decrypt_backup.py');
    try {
      const out = execFileSync(process.env.CMON_PYTHON, [tool, fEnc, '--summary', '--password-file', PW_FILE], { encoding: 'utf8' });
      ok('fuggetlen visszafejtes (Python AES-GCM)', /format: device-config secrets: True/.test(out), out.trim().split('\n').slice(0, 3).join(' | '));
    } catch (e) {
      ok('fuggetlen visszafejtes (Python AES-GCM)', false, String(e.stderr || e.message));
    }
    let wrongOk = false;
    const wrongPw = path.join(TMP, 'wrong.txt');
    fs.writeFileSync(wrongPw, 'rossz-jelszo-xyz');
    try { execFileSync(process.env.CMON_PYTHON, [tool, fEnc, '--summary', '--password-file', wrongPw], { stdio: 'pipe' }); } catch (e) { wrongOk = /InvalidTag/.test(String(e.stderr)); }
    ok('fuggetlen: rossz jelszo -> InvalidTag', wrongOk);
  }

  // 4. titkositott import: jelszomezo megjelenik, rossz jelszo -> hiba, semmi nem valtozik
  await page.setInputFiles('#impFile', fEnc);
  await page.waitForFunction(() => document.getElementById('impPwRow').style.display === 'block', null, { timeout: 5000 });
  ok('titkositott fajl -> jelszomezo', true, await msg(page));
  await page.fill('#impPw', 'rossz-jelszo-xyz');
  await importBtn.click();
  await waitMsg(page, /wrong password or corrupted/);
  ok('import rossz jelszoval -> hiba', true, await msg(page));
  ok('rossz jelszo utan konfig valtozatlan', strip(await cfgNow()) === strip(before));

  // 5. titkositott import jo jelszoval
  await page.fill('#impPw', PW);
  await importBtn.click();
  await waitMsg(page, /Imported:/);
  ok('titkositott import', true, await msg(page));
  await new Promise(r => setTimeout(r, 2000));
  ok('titkositott import utan konfig azonos', strip(await cfgNow()) === strip(before), strip(await cfgNow()));

  // 6. olvashato (titok nelkuli) import: a meglevo titkok maradnak
  await page.setInputFiles('#impFile', fPlain);
  await importBtn.click();
  await waitMsg(page, /Imported:/);
  await new Promise(r => setTimeout(r, 2000));
  ok('titok nelkuli import utan konfig azonos', strip(await cfgNow()) === strip(before));

  ok('nincs JS-hiba', errs.length === 0, errs.join(' | '));
  await browser.close();
  fs.rmSync(TMP, { recursive: true, force: true });
  for (const r of results) console.log(r[0], r[1], r[2] ? '— ' + r[2] : '');
  console.log('FAIL db:', results.filter(r => r[0] !== 'OK  ').length, '/', results.length);
})().catch(e => {
  fs.rmSync(TMP, { recursive: true, force: true });
  for (const r of results) console.log(r[0], r[1], r[2] ? '— ' + r[2] : '');
  console.log('MEGSZAKADT:', e.message.split('\n')[0]);
  process.exit(1);
});

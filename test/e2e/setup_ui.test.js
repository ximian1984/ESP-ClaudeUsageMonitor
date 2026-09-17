// Futtatas (a dongle-lal azonos halozaton, vagy SSH-alagutton: ssh -N -L 18080:<dongle-ip>:80 <gep>):
//   CMON_URL=http://127.0.0.1:18080 CMON_PW_FILE=/biztonsagos/hely/jelszo.txt node test/e2e/<fajl>.js
//   (PLAYWRIGHT_MODULE: ha a playwright globalisan van, pl. /opt/homebrew/lib/node_modules/playwright)
// A jelszo SOHA nincs a repoban: fajlbol olvasva, amit a futtato a hasznalat utan torol.
// Setup-felulet E2E: login-hej, belepes, Claude/Wi-Fi profil CRUD (csak TESZT-adatokon), TZ, display, scan, logout.
// Csak TESZT-adatot hoz letre es torol; a meglevo profilokhoz nem nyul.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const B = process.env.CMON_URL || 'http://127.0.0.1:18080';
const PW = require('fs').readFileSync(process.env.CMON_PW_FILE, 'utf8');
const results = [];
const ok = (name, cond, info = '') => { results.push([cond ? 'OK  ' : 'FAIL', name, info]); };
const msg = (p) => p.locator('#msg').textContent().catch(() => '');
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 } });  // telefon-meret
  const page = await ctx.newPage();
  const consoleErrors = [];
  page.on('pageerror', e => consoleErrors.push(String(e)));

  // 1. login-hej
  await page.goto(B + '/');
  await page.waitForSelector('#f', { state: 'visible', timeout: 10000 });
  const shellHtml = await page.content();
  ok('login-hej: title Login', (await page.title()) === 'Login');
  ok('login-hej: nincs arulkodo szo', !/claude|monitor|lilygo|dongle|esp32|anthropic|oauth|firmware/i.test(shellHtml));

  // 2. rossz jelszo (1 proba, a 5-os zar alatt)
  await page.fill('#p', 'rossz-jelszo-123');
  await page.click('#f button');
  await page.waitForFunction(() => document.getElementById('m').textContent.length > 0, null, { timeout: 10000 });
  ok('rossz jelszo -> hiba', /wrong/i.test(await page.locator('#m').textContent()), await page.locator('#m').textContent());

  // 3. jo jelszo -> felulet
  await page.fill('#p', PW);
  await page.click('#f button');
  await page.waitForSelector('#claudeList', { timeout: 15000 });
  await page.waitForFunction(() => document.querySelectorAll('#claudeList tr').length > 0, null, { timeout: 15000 });
  ok('belepes -> felulet', true);
  const claudeRows = async () => page.$$eval('#claudeList tr', trs => trs.map(t => t.children[0].textContent));
  const wifiRows = async () => page.$$eval('#wifiList tr', trs => trs.map(t => t.children[0].textContent));
  const origClaude = await claudeRows();
  ok('Claude-profilok latszanak', origClaude.length > 0, JSON.stringify(origClaude));
  await page.waitForFunction(() => document.querySelectorAll('#dev tr').length > 3, null, { timeout: 10000 });
  const dev = await page.$$eval('#dev tr', trs => trs.map(t => t.children[0].textContent + '=' + t.children[1].textContent));
  ok('Device tabla', dev.length > 3, dev.filter(x => /IP|Firmware|Wi-Fi state/.test(x)).join(', '));
  ok('Wi-Fi lista', (await wifiRows()).length >= 1, JSON.stringify(await wifiRows()));

  // 4. Claude-profil nevvel, Save gombbal
  const cf = page.locator('#claudeForm');
  await cf.locator('input[name=name]').fill('TESZT-UI');
  await cf.locator('button[type=submit]').click();
  await page.waitForFunction(() => [...document.querySelectorAll('#claudeList tr')].some(t => t.children[0].textContent === 'TESZT-UI'), null, { timeout: 15000 });
  ok('Claude Save nevvel -> tablaban', true, await msg(page));
  ok('Save utan az urlap kiurul', (await cf.locator('input[name=name]').inputValue()) === '');

  // 5. Edit -> atnevezes
  let row = page.locator('#claudeList tr', { hasText: 'TESZT-UI' });
  await row.getByRole('button', { name: 'Edit' }).click();
  ok('Edit betolti a nevet', (await cf.locator('input[name=name]').inputValue()) === 'TESZT-UI');
  await cf.locator('input[name=name]').fill('TESZT-UI2');
  await cf.locator('button[type=submit]').click();
  await page.waitForFunction(() => [...document.querySelectorAll('#claudeList tr')].some(t => t.children[0].textContent === 'TESZT-UI2'), null, { timeout: 15000 });
  ok('atnevezes', !(await claudeRows()).includes('TESZT-UI'), JSON.stringify(await claudeRows()));

  // 6. Authenticate a sorbol -> uj ful a claude.com authorize-ra (nem lepunk be)
  row = page.locator('#claudeList tr', { hasText: 'TESZT-UI2' });
  const [popup] = await Promise.all([
    ctx.waitForEvent('page', { timeout: 15000 }),
    row.getByRole('button', { name: 'Authenticate' }).click(),
  ]);
  await popup.waitForURL(/claude\.(com|ai)\/.*oauth\/authorize/, { timeout: 20000 }).catch(() => {});
  const pu = popup.url();
  ok('Authenticate -> uj ful authorize URL', /oauth\/authorize/.test(pu), pu.replace(/(state|code_challenge)=[^&]+/g, '$1=***').slice(0, 110));
  ok('loginFlow megjelent', await page.locator('#loginFlow').isVisible());
  await popup.close();
  await page.locator('#loginFlow button', { hasText: 'Cancel' }).click();

  // 7. ures nev -> Profile-XX
  await page.locator('#claudeForm button', { hasText: 'New' }).click();
  await cf.locator('input[name=name]').fill('');
  await cf.locator('button[type=submit]').click();
  await page.waitForFunction(() => [...document.querySelectorAll('#claudeList tr')].some(t => /^Profile-\d\d$/.test(t.children[0].textContent)), null, { timeout: 15000 });
  const auto = (await claudeRows()).find(n => /^Profile-\d\d$/.test(n));
  ok('ures nev -> Profile-XX', !!auto, auto);

  // 8. ekezetes nev -> hiba, nem ment
  const before = (await claudeRows()).length;
  await cf.locator('input[name=name]').fill('Szilárd');
  await cf.locator('button[type=submit]').click();
  await sleep(2500);
  ok('ekezetes nev -> hibauzenet', /ASCII/.test(await msg(page)), await msg(page));
  ok('ekezetes nev -> nem mentett', (await claudeRows()).length === before);

  // 9. torles: a ket TESZT-profil (confirm dialogus elfogadasa)
  page.on('dialog', d => d.accept());
  for (const n of ['TESZT-UI2', auto]) {
    const r = page.locator('#claudeList tr').filter({ has: page.locator('td', { hasText: new RegExp('^' + n + '$') }) });
    await r.getByRole('button', { name: 'Delete' }).click();
    await page.waitForFunction((nn) => ![...document.querySelectorAll('#claudeList tr')].some(t => t.children[0].textContent === nn), n, { timeout: 15000 });
  }
  ok('TESZT Claude-profilok torolve, az eredetiek maradnak', JSON.stringify(await claudeRows()) === JSON.stringify(origClaude), JSON.stringify(await claudeRows()));

  // 10. Wi-Fi-profil hozzaadas (tiltva) -> frissul oldalujratoltes nelkul -> torles
  const wf = page.locator('#wifiForm');
  await wf.locator('input[name=ssid]').fill('TESZT-WIFI');
  await wf.locator('input[name=password]').fill('tesztjelszo1');
  await wf.locator('input[name=enabled]').uncheck();
  await wf.locator('button[type=submit]').click();
  await page.waitForFunction(() => [...document.querySelectorAll('#wifiList tr')].some(t => t.children[0].textContent === 'TESZT-WIFI'), null, { timeout: 15000 });
  ok('Wi-Fi mentes -> lista frissul reload nelkul', true, await msg(page));
  const wr = page.locator('#wifiList tr').filter({ has: page.locator('td', { hasText: /^TESZT-WIFI$/ }) });
  await wr.getByRole('button', { name: 'Delete' }).click();
  await page.waitForFunction(() => ![...document.querySelectorAll('#wifiList tr')].some(t => t.children[0].textContent === 'TESZT-WIFI'), null, { timeout: 15000 });
  ok('Wi-Fi TESZT torolve', true, JSON.stringify(await wifiRows()));

  // 11. idozona: a jelenlegi ertek ujramentese
  const tz = await page.locator('#tzStr').inputValue();
  await page.locator('form', { has: page.locator('#tzStr') }).locator('button[type=submit]').click();
  await page.waitForFunction(() => /Time zone saved/.test(document.getElementById('msg').textContent), null, { timeout: 10000 });
  ok('TZ mentes (valtozatlan)', true, tz + ' | ' + await msg(page));

  // 12. display & refresh: jelenlegi ertekek ujramentese
  await page.locator('form', { has: page.locator('#rot') }).locator('button[type=submit]').click();
  await page.waitForFunction(() => document.getElementById('msg').textContent === 'Saved', null, { timeout: 10000 });
  ok('Display/refresh mentes (valtozatlan)', true);

  // 13. scan
  await page.locator('button', { hasText: 'Scan' }).click();
  await page.waitForFunction(() => /Scan done/.test(document.getElementById('msg').textContent), null, { timeout: 40000 });
  ok('Scan', true, await msg(page));

  // 14. oldal ujratoltes: a munkamenet megmarad (sessionStorage)
  await page.reload();
  await page.waitForSelector('#claudeList', { timeout: 15000 });
  ok('reload utan belepve marad', true);

  // 15. logout -> vissza a login-hejra, API zarva
  await page.evaluate(() => logout());
  await page.waitForSelector('#f', { state: 'visible', timeout: 15000 });
  const st = await (await page.request.get(B + '/api/status')).json();
  ok('logout -> login-hej, status zarva', st.locked === true, JSON.stringify(st));

  ok('nincs JS-hiba', consoleErrors.length === 0, consoleErrors.join(' | '));
  await browser.close();
  for (const r of results) console.log(r[0], r[1], r[2] ? '— ' + r[2] : '');
  console.log('FAIL db:', results.filter(r => r[0] !== 'OK  ').length, '/', results.length);
})().catch(async e => {
  for (const r of results) console.log(r[0], r[1], r[2] ? '— ' + r[2] : '');
  console.log('MEGSZAKADT:', e.message.split('\n')[0]);
  process.exit(1);
});

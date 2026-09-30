// Run with Node and Playwright installed. No hardware or network access is used.
const assert = require('node:assert/strict');
const { readFileSync, mkdirSync } = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');
const html = readFileSync(path.join(__dirname, '../../src/web_ui.html'), 'utf8');
const config = {
  configured: true, radio_profile: 'zigbee', hostname: 'bc250', wifi_ssid: 'Home',
  zigbee_channel: 0, zigbee_model: 'BC250 Controller',
  ble_scan_interval_ms: 100, ble_scan_window_ms: 50, ble_absent_ms: 10000,
  pins: { ps_on: {gpio: 4}, power_button: {gpio: 5}, power_sense: {gpio: 6}, status_led: {gpio: 8} },
  timing: { strategy: 1, inter_output_delay_ms: 500, button_pulse_ms: 250, handoff_delay_ms: 500,
    start_timeout_ms: 30000, shutdown_timeout_ms: 30000, force_off_ms: 5000, retry_cooldown_ms: 60000 },
  sense_on_ms: 100, sense_off_ms: 100,
  psu_i2c: { enabled: false, sda_gpio: -1, scl_gpio: -1, address: 95, poll_interval_ms: 2000 },
  buttons: [], ble_devices: [], recommended_gpios: [4,5,6,8], blocked_gpios: [12,14]
};
const status = { power_state: 'off', sensed_on: false, config_ap: true, wifi_ip: '192.168.4.1',
  wifi_connected: false, zigbee_joined: false, zigbee_started: false, psu_i2c: {enabled: false}, ota_enabled: true };

(async () => {
  const browser = await chromium.launch({headless: true,
    ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? {executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE} : {})});
  const errors = [];
  async function fixture(overrides = {}, fail = '') {
    const page = await browser.newPage({viewport: {width: 1024, height: 900}});
    const calls = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.clock.install();
    await page.route('http://bc250.test/**', async route => {
      const req = route.request(), url = new URL(req.url()).pathname;
      calls.push({url, method: req.method(), body: req.postData()});
      if (req.method() !== 'GET' && url === fail) return route.fulfill({status: 503, body: 'Please retry'});
      if (url === '/') return route.fulfill({contentType:'text/html', body: html});
      if (url.endsWith('/events')) return route.fulfill({contentType:'text/event-stream', body:': ready\n\n'});
      const response = url.endsWith('/config') && req.method() === 'GET' ? {...config, ...overrides} :
        url.endsWith('/status') ? status : url.endsWith('/i2c/scan') ? {addresses:[94], complete:true} : {accepted:true};
      return route.fulfill({contentType:'application/json', body: JSON.stringify(response)});
    });
    await page.goto('http://bc250.test/');
    await page.waitForFunction(() => loaded && lastStatus);
    return {page, calls};
  }
  const visible = (page, id) => page.locator('#'+id).isVisible();
  const editNetwork = async page => {await page.getByRole('button', {name:'Settings', exact:true}).click(); await page.locator('#networksection > summary').click();};
  try {
    let {page, calls} = await fixture();
    assert.equal(await visible(page,'savedock'), false, 'initial page has no save bar');
    assert.equal(await page.locator('#settings').getAttribute('open'), null);
    assert.equal(await page.locator('#pairbutton').isEnabled(), true);
    mkdirSync(path.join(__dirname, '../../build/ui-preview'), {recursive:true});
    await page.screenshot({path:path.join(__dirname, '../../build/ui-preview/desktop.png'), fullPage:true});
    await editNetwork(page);
    await page.locator('#hostname').fill('new-name');
    assert.equal(await visible(page,'savedock'), true);
    assert.equal(await page.locator('#savebutton').isEnabled(), true);
    assert.equal(await page.locator('#pairbutton').isEnabled(), false, 'pairing cannot drop edits');
    await page.locator('#hostname').fill('bc250');
    assert.equal(await visible(page,'savedock'), false, 'reverting hides save bar');
    await page.locator('#zbchannel').fill('');
    assert.equal(await visible(page,'savedock'), true, 'blank differs from zero');
    assert.equal(await page.locator('#savebutton').isEnabled(), false);
    await page.locator('#discardbutton').click();
    assert.equal(await page.locator('#zbchannel').inputValue(), '0');
    assert.equal(await visible(page,'savedock'), false);
    await page.locator('#securitysection > summary').click();
    await page.locator('#admin').fill('password123');
    await page.locator('#discardbutton').click();
    assert.equal(await page.locator('#admin').inputValue(), '');
    await page.locator('#buttonsection > summary').click();
    await page.getByRole('button',{name:'Add button',exact:true}).click();
    assert.equal(await visible(page,'savedock'), true);
    await page.getByRole('button',{name:'Remove button 1',exact:true}).click();
    assert.equal(await visible(page,'savedock'), false);
    await page.locator('#blesection > summary').click();
    await page.getByRole('button',{name:'Add manually',exact:true}).click();
    await page.locator('#bledevices input').first().fill('Remote');
    await page.locator('#discardbutton').click();
    assert.equal(await page.locator('#bledevices .item').count(), 0);
    await page.locator('#psusection > summary').click();
    await page.locator('#psusda').fill('0'); await page.locator('#psuscl').fill('1');
    await page.locator('.psu-tools > summary').click();
    await page.locator('#i2cscanbutton').click();
    await page.getByRole('button',{name:'Use as PIC address'}).click();
    assert.equal(await page.locator('#psuaddress').inputValue(), '94');
    await page.locator('#discardbutton').click();
    assert.equal(await page.locator('#psuaddress').inputValue(), '95');
    await page.locator('#pairbutton').click(); await page.locator('#cancelpair').click();
    assert.equal(calls.filter(c=>c.url.endsWith('/zigbee')).length, 0);
    await page.locator('#pairbutton').click(); await page.locator('#confirmpair').click();
    await page.waitForFunction(() => sessionEnded);
    const pair = calls.filter(c=>c.url.endsWith('/zigbee'));
    assert.equal(pair.length, 1); assert.deepEqual(JSON.parse(pair[0].body), {action:'commission'});
    assert.equal(await visible(page,'sessionnotice'), true);
    const polls = calls.filter(c=>c.url.endsWith('/status')).length;
    await page.clock.runFor(6000);
    assert.equal(calls.filter(c=>c.url.endsWith('/status')).length, polls, 'polling stops before disconnect');
    assert.equal(await page.getByRole('button',{name:'Power on',exact:true}).isEnabled(), false);
    await page.close();

    ({page, calls} = await fixture({}, '/api/v1/zigbee'));
    await page.locator('#pairbutton').click(); await page.locator('#confirmpair').click();
    await page.waitForFunction(() => !closingAp && !$('message').hidden);
    assert.equal(await visible(page,'sessionnotice'), false);
    assert.equal(await page.locator('#confirmpair').isEnabled(), true, 'failed request can retry');
    await page.close();

    ({page, calls} = await fixture({}, '/api/v1/config'));
    await editNetwork(page); await page.locator('#hostname').fill('updated'); await page.locator('#savebutton').click();
    await page.waitForFunction(() => !saving && !$('message').hidden);
    assert.equal(await visible(page,'savedock'), true); assert.equal(await page.locator('#hostname').inputValue(), 'updated');
    assert.equal(await page.locator('#savebutton').isEnabled(), true);
    await page.close();

    ({page, calls} = await fixture());
    await editNetwork(page); await page.locator('#hostname').fill('updated'); await page.locator('#savebutton').click();
    await page.waitForFunction(() => sessionEnded);
    assert.equal(await visible(page,'savedock'), false);
    assert.equal(calls.filter(c=>c.method==='PUT').length, 1);
    assert.equal(JSON.parse(calls.find(c=>c.method==='PUT').body).hostname, 'updated');
    await page.close();

    ({page, calls} = await fixture({radio_profile:'wifi'}));
    assert.equal(await visible(page,'pairingsection'), false);
    await editNetwork(page); await page.locator('#radio').selectOption('zigbee');
    assert.equal(await visible(page,'pairingsection'), true);
    assert.equal(await page.locator('#pairbutton').isEnabled(), false, 'unsaved Zigbee profile cannot pair');
    await page.close();

    ({page, calls} = await fixture({configured:false}));
    assert.notEqual(await page.locator('#settings').getAttribute('open'), null);
    assert.equal(await page.locator('#pairbutton').isEnabled(), false);
    await page.close();

    ({page, calls} = await fixture());
    for (const width of [390,320]) {
      await page.setViewportSize({width,height:844});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth), true);
      if (width===390) await page.screenshot({path:path.join(__dirname,'../../build/ui-preview/mobile.png'),fullPage:true});
    }
    await editNetwork(page); await page.locator('#hostname').fill('phone');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth), true);
    await page.screenshot({path:path.join(__dirname,'../../build/ui-preview/mobile-edit.png'),fullPage:true});
    await page.locator('#discardbutton').click();
    await page.getByText('Setup Wi-Fi options',{exact:true}).click();
    await page.locator('#closeapbutton').click(); await page.locator('#confirmcloseap').click();
    await page.waitForFunction(() => sessionEnded);
    assert.equal(calls.filter(c=>c.url.endsWith('/wifi/ap/close')).length, 1);
    await page.close();
    assert.deepEqual(errors, []);
    console.log('Web UI: dirty/revert/discard, validation, dynamic lists, save success/failure, pairing/cancel/failure, mode gating, AP close, and responsive layouts passed.');
  } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode=1;});

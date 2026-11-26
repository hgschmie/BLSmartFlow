// Render the firmware pages against local fixtures, with no controller or internet connection.
// Set CHROME_PATH to a Chromium executable and install playwright-core to run this check.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require('playwright-core');
const www = path.resolve(__dirname, '../src/www');
const output = path.resolve(__dirname, '../images/2.0.0');
const firmwareversion = 'Stable 2.0.0 (2026.11.25)';
const ranges = { '5m': [1, 300], '1h': [15, 240], '6h': [60, 360], '24h': [300, 288] };
const types = { '.html':'text/html', '.js':'text/javascript', '.css':'text/css', '.png':'image/png', '.ico':'image/x-icon' };

function history(query, now) {
  const [step, count] = ranges[query.get('range')];
  const cursor = Math.floor(now / step);
  const full = query.get('boot') !== 'preview';
  const start = full ? cursor - count + 1 : Number(query.get('since'));
  const points = [];
  for (let number = start; number <= cursor; ++number) {
    const t = number * step;
    // A print finishes shortly before the end of the displayed interval.
    const phase = (t % 3600 + 3600) % 3600;
    const temp = phase < 3320 ? 219 + 1.2 * Math.sin(t / 17) : 24 + 195 * Math.exp(-(phase - 3320) / 195);
    const fan1 = phase < 3170 ? 38 : phase < 3250 ? 57 : phase < 3310 ? 74 : 81;
    const fan2 = phase < 3250 ? 42 : phase < 3320 ? 60 : 100;
    points.push([t, Math.round(temp), fan1, fan2]);
  }
  return {boot:'preview', now, cursor, full, points};
}

async function run() {
  const browser = await chromium.launch({executablePath: process.env.CHROME_PATH, headless:true});
  fs.mkdirSync(output,{recursive:true});
  try {
    for (const [device, width, height] of [['desktop',1440,960],['mobile',390,844]]) {
      const page = await browser.newPage({viewport:{width,height},deviceScaleFactor:1});
      const errors = [];
      page.on('pageerror', err=>errors.push(err.message));
      const config = {firmwareversion, ip:'192.0.2.83',code:'*****4c2',id:'************608',
        debuging:true,debugingchange:false,mqttdebug:false, fansSynchronized:false,fan2Configured:true,
        staticfanspeed:100,staticfanspeed2:100,followPrinterFan:true,printerFanSource:'chamber',
        postPrint:false,postPrintSeconds:600,postPrintSpeed:100,postPrint2:true,postPrintSeconds2:600,postPrintSpeed2:100};
      let recovery = false, remaining = 520, savedSection;
      const started = Date.now();
      let historyReads = 0;
      const sensor = () => ({temp:154,speed:81,speed2:config.fansSynchronized?81:remaining?100:60,
        printerFans:{chamber:70},postPrintRemaining:0,postPrintRemaining2:remaining,printerConnection:{state:'connected'}});
      await page.route('**/*',async route=>{
        const url = new URL(route.request().url());
        assert.equal(url.hostname,'preview.test');
        const params = new URLSearchParams(route.request().postData() || '');
        switch(url.pathname) {
          case '/history':
            historyReads++;
            return route.fulfill({json:history(url.searchParams, 24*3600+3400+Math.floor((Date.now()-started)/1000))});
          case '/getOptions': return route.fulfill({json:config});
          case '/sensorData': return route.fulfill({json:sensor()});
          case '/getFanConfig': return route.fulfill({json:{
            fansSynchronized:config.fansSynchronized,
            points:[{temp:30,speed:0},{temp:100,speed:30},{temp:180,speed:65},{temp:250,speed:100}],
            followPoints:[{input:0,speed:0},{input:1,speed:35},{input:100,speed:100}]}});
          case '/wifi/status': return route.fulfill({json:{firmwareversion,ssid:recovery?'':'Workshop',hasPassword:!recovery,connected:!recovery,apActive:recovery,ip:'192.0.2.89'}});
          case '/submitOptions':
            savedSection = params.get('section');
            if (savedSection === 'settings') {
              assert(!params.has('staticfan')); config.ip = params.get('ip');
            } else {
              assert.equal(savedSection,'fans'); assert(!params.has('ip')); config.fansSynchronized = params.get('fansSynchronized') === 'on';
            }
            return route.fulfill({body:'Settings saved'});
          case '/cancelPostPrint':
            assert.equal(params.get('fan'),'2'); remaining = 0;
            return route.fulfill({json:sensor()});
        }
        const filename = url.pathname==='/' && recovery || url.pathname==='/wifi' ? 'wifiPortal.html'
          : ['/','/dashboard','/fans','/settings'].includes(url.pathname) ? 'fanpage.html' : url.pathname.slice(1);
        assert(Object.hasOwn(types,path.extname(filename)));
        return route.fulfill({contentType:types[path.extname(filename)],body:fs.readFileSync(path.join(www,filename))});
      });
      async function shot(name) {
        await page.locator('#save-notice').waitFor({state:'hidden'});
        await page.evaluate(()=>document.fonts.ready);
        assert(await page.evaluate(()=>[...document.images].every(img=>img.complete && img.naturalWidth>0)),name+' images');
        assert(await page.evaluate(()=>document.documentElement.scrollWidth <= innerWidth), name+' overflows');
        await page.screenshot({path:path.join(output,name+'-'+device+'.png'),fullPage:true,animations:'disabled'});
      }
      const tab = name=>page.getByRole('navigation').getByRole('link',{name,exact:true}).click();
      await page.goto('http://preview.test/');
      await page.waitForFunction(()=>document.querySelector('#reading2').textContent === '100%');
      await shot('dashboard');
      for (const [range, [, count]] of Object.entries(ranges)) {
        await page.selectOption('#historyRange',range);
        await page.waitForFunction(count=>Chart.getChart('history0').data.datasets[0].data.length===count,count);
      }
      await page.waitForFunction(()=>document.querySelector('#fanButton2').disabled === false);
      await page.click('#fanButton2');
      await page.waitForFunction(()=>document.querySelector('#fan2Source').textContent==='');
      await tab('Settings');
      await shot('settings');
      await page.fill('#printerip','192.0.2.84');
      await tab('Fans');
      await page.waitForFunction(()=>Chart.getChart('myChart1')?.width>0 && !document.querySelector('#pointSelect1').disabled);
      await page.selectOption('#pointSelect1','1');
      await shot('fans');
      await tab('WiFi');
      await page.frameLocator('#wifi-frame').getByText('Connected to Workshop.').waitFor();
      await shot('wifi');
      await page.goBack();
      assert.equal(await page.inputValue('#pointSelect1'),'1');
      await tab('Settings');
      assert.equal(await page.inputValue('#printerip'),'192.0.2.84');
      await page.click('#saveOptions');
      await page.waitForFunction(()=>!document.querySelector('#saveOptions').disabled);
      assert.equal(savedSection,'settings');
      await tab('Fans');
      assert.equal(await page.inputValue('#pointSelect1'),'1');
      await page.locator('label[for=fansSynchronized]').click();
      await page.click('#saveFans');
      await page.waitForFunction(()=>document.querySelector('#graphPanel2').hidden && !document.querySelector('#saveFans').disabled);
      assert.equal(savedSection,'fans');
      await page.waitForSelector('#save-notice',{state:'hidden'});
      await shot('fans-synchronized');
      recovery = true;
      await page.goto('http://preview.test/');
      await page.getByText('No Wi-Fi network is configured.',{exact:false}).waitFor();
      await shot('wifi-recovery');
      assert.deepEqual(errors,[]);
      assert(historyReads>=4);
      console.log(device+' previews and navigation, history, cancellation, draft and save checks passed');
      await page.close();
    }
  } finally { await browser.close(); }
}
run().catch(error=>{console.error(error);process.exitCode=1;});

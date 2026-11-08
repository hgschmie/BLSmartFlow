// Run with: node --test test/settings-page.test.js
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

const html = readFileSync(join(__dirname, '../src/www/fanpage.html'), 'utf8');
const script = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)]
  .map(match => match[1]).find(script => script.includes('function updateOptions()'));
const settle = () => new Promise(resolve => setImmediate(resolve));
const copy = value => JSON.parse(JSON.stringify(value));
const points1 = [{ temp: 0, speed: 0 }, { temp: 200, speed: 60 }, { temp: 400, speed: 100 }];
const identity = [{ input: 0, speed: 0 }, { input: 1, speed: 1 }, { input: 100, speed: 100 }];
const exhaust = [{ input: 0, speed: 0 }, { input: 1, speed: 35 }, { input: 100, speed: 100 }];
const points2 = [{ temp: 0, speed: 20 }, { temp: 200, speed: 40 }, { temp: 400, speed: 70 }];
const baseConfig = {
  firmwareversion: 'Stable 1.1.0 (2026.10.23)', ip: '192.0.2.83', code: '*****123', id: '************456',
  staticfans: false, staticfanspeed: 0, debuging: true, debugingchange: false, mqttdebug: false,
};
const separateConfig = {
  ...baseConfig, fansSynchronized: false, fan2Configured: true, staticfans2: true, staticfanspeed2: 69,
};

function loadPage({ chartAvailable = true } = {}) {
  const elements = new Map([...html.matchAll(/<[^>]+\bid=['"]([^'"]+)['"][^>]*>/g)].map(match => {
    const tag = match[0];
    const element = {
      value: /\bvalue=['"]([^'"]*)['"]/.exec(tag)?.[1] || '',
      name: /\bname=['"]([^'"]*)['"]/.exec(tag)?.[1],
      type: /\btype=['"]([^'"]*)['"]/.exec(tag)?.[1],
      checked: /\bchecked\b/.test(tag), disabled: /\bdisabled\b/.test(tag), hidden: /\bhidden\b/.test(tag),
      textContent: '', style: {}, listeners: {},
      addEventListener(event, handler) { this.listeners[event] = handler; },
      reportValidity() { return true; },
    };
    return [match[1], element];
  }));
  const requests = [], fetches = [], events = {}, charts = [], timers = new Map();
  let now = 0, timerId = 0;
  class XMLHttpRequest {
    constructor() { requests.push(this); }
    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader() {}
    send(body) { this.body = body; }
    respond(status, body) {
      this.status = status; this.responseText = body; this.readyState = 4;
      this.onreadystatechange();
    }
  }
  class FormData {
    constructor() {
      this.entries = [...elements.values()].filter(element => element.name && !element.disabled &&
        (element.type !== 'checkbox' || element.checked)).map(element => [element.name, element.type === 'checkbox' ? 'on' : element.value]);
    }
    [Symbol.iterator]() { return this.entries[Symbol.iterator](); }
  }
  const context = vm.createContext({
    XMLHttpRequest, FormData, URLSearchParams,
    alert() { assert.fail('Unexpected blocking alert'); }, console: { error() {} }, setInterval() {},
    setTimeout(callback, delay) { timers.set(++timerId, { callback, at: now + delay }); return timerId; },
    clearTimeout(id) { timers.delete(id); },
    fetch(url, options) {
      return new Promise((resolve, reject) => fetches.push({ url, options, resolve, reject, done: false }));
    },
    window: { addEventListener(event, handler) { events[event] = handler; } },
    document: {
      querySelectorAll: () => [],
      getElementById(id) { assert.ok(elements.has(id), `Missing element: ${id}`); return elements.get(id); },
    },
    ...(chartAvailable ? { Chart: class {
      constructor(canvas, config) { this.data = config.data; this.options = config.options; charts.push(this); }
      update() {}
      resize() {}
    } } : {}),
  });
  vm.runInContext(script, context);
  const page = {
    context, elements, requests, fetches, events, charts,
    get: id => elements.get(id),
    advanceTime(milliseconds) {
      now += milliseconds;
      for (const [id, timer] of timers) {
        if (timer.at <= now) { timers.delete(id); timer.callback(); }
      }
    },
    state: name => JSON.parse(vm.runInContext(`JSON.stringify(${name})`, context)),
    trigger(id, event) { elements.get(id).listeners[event]({ preventDefault() {} }); },
    pending(url) { const request = fetches.find(request => request.url === url && !request.done); assert.ok(request, `No pending ${url}`); return request; },
    async respond(url, body, status = 200) {
      const request = this.pending(url); request.done = true;
      request.resolve({ ok: status >= 200 && status < 300, json: async () => copy(body), text: async () => typeof body === 'string' ? body : JSON.stringify(body) });
      await settle();
    },
  };
  return page;
}

async function readyPage(config = baseConfig, mappings = {}) {
  const page = loadPage();
  page.events.DOMContentLoaded();
  page.requests[0].respond(200, JSON.stringify(config));
  await page.respond('/getFanConfig', { points: points1, points2, ...mappings, fansSynchronized: config.fansSynchronized !== false });
  await page.respond('/sensorData', { temp: 100, speed: 30, speed2: config.fansSynchronized === false ? 69 : 30 });
  return page;
}

test('settings load without waiting for external images or the chart library', () => {
  const page = loadPage({ chartAvailable: false });
  assert.equal(page.requests.length, 1);
  assert.equal(page.requests[0].url, '/getOptions');
  assert.equal(page.get('saveOptions').disabled, true);
  page.requests[0].respond(200, JSON.stringify(baseConfig));
  assert.equal(page.get('printerip').value, baseConfig.ip);
  assert.equal(page.get('printercode').value, baseConfig.code);
  assert.equal(page.get('printerserial').value, baseConfig.id);
  assert.equal(page.get('debuging').checked, true);
  assert.equal(page.get('saveOptions').disabled, false);
  assert.equal(page.get('settings-status').textContent, '');
  assert.ok(page.get('firmwareversion').textContent.includes(baseConfig.firmwareversion));
  page.events.DOMContentLoaded();
  assert.match(page.get('curve-status').textContent, /chart could not load/i);
  assert.equal(page.get('createPointBtn1').disabled, true);
});

test('legacy options default to one synchronized curve and feature section', async () => {
  const page = await readyPage();
  assert.equal(page.get('fansSynchronized').checked, true);
  assert.equal(page.get('fan2Features').hidden, true);
  assert.equal(page.get('fan1FeaturesTitle').hidden, true);
  assert.equal(page.get('fan1FeaturesTitle').textContent, '');
  assert.equal(page.get('fan2Indicator').hidden, true);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.charts[0].data.datasets[0].label, 'Both fans');
  assert.equal(page.get('followPrinterFan').checked, false);
  assert.equal(page.get('followPrinterFan2').checked, false);
  assert.equal(page.get('printerFanSource').value, 'chamber');
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan1Source').textContent, '');
});

for (const suffix of ['', '2']) {
  test(`Fan ${suffix || '1'} static and follow switches exclude each other but can both be off`, async () => {
    const page = await readyPage(separateConfig);
    page.get('staticfan' + suffix).checked = true;
    page.trigger('staticfan' + suffix, 'change');
    assert.equal(page.get('followPrinterFan' + suffix).checked, false);
    page.get('followPrinterFan' + suffix).checked = true;
    page.trigger('followPrinterFan' + suffix, 'change');
    assert.equal(page.get('staticfan' + suffix).checked, false);
    assert.equal(page.get('staticfan' + suffix).disabled, false);
    assert.equal(page.get('followPrinterFan' + suffix).disabled, false);
    page.get('followPrinterFan' + suffix).checked = false;
    page.trigger('followPrinterFan' + suffix, 'change');
    assert.equal(page.get('staticfan' + suffix).checked, false);
    page.get('followPrinterFan' + suffix).checked = true;
    page.trigger('followPrinterFan' + suffix, 'change');
    page.get('staticfan' + suffix).checked = true;
    page.trigger('staticfan' + suffix, 'change');
    assert.equal(page.get('followPrinterFan' + suffix).checked, false);
    page.get('staticfan' + suffix).checked = false;
    page.trigger('staticfan' + suffix, 'change');
    assert.equal(page.get('followPrinterFan' + suffix).checked, false);
  });
}

test('following is saved separately for each Fan and labels change only after successful reload', async () => {
  const page = await readyPage(separateConfig);
  for (const [suffix, source] of [['', 'chamber'], ['2', 'heatbreak']]) {
    page.get('followPrinterFan' + suffix).checked = true;
    page.trigger('followPrinterFan' + suffix, 'change');
    page.get('printerFanSource' + suffix).value = source;
  }
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan1Source').textContent, '');
  assert.equal(page.get('fan2Source').hidden, false);
  assert.equal(page.get('fan2Source').textContent, '');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('followPrinterFan'), 'on');
  assert.equal(body.get('printerFanSource'), 'chamber');
  assert.equal(body.get('followPrinterFan2'), 'on');
  assert.equal(body.get('printerFanSource2'), 'heatbreak');
  assert.equal(body.get('staticfan'), 'off');
  assert.equal(body.get('staticfan2'), 'off');
  page.requests[1].respond(200, 'Saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, staticfans2: false,
    followPrinterFan: true, printerFanSource: 'chamber', followPrinterFan2: true, printerFanSource2: 'heatbreak' }));
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.equal(page.get('fan2Source').textContent, 'Hotend Fan');
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan2Source').hidden, false);
});

test('following can be turned off explicitly and a failed save retains the active label', async () => {
  const page = await readyPage({ ...baseConfig, followPrinterFan: true, printerFanSource: 'auxiliary' });
  assert.equal(page.get('fan1Source').textContent, 'Auxiliary Fan');
  page.get('followPrinterFan').checked = false;
  page.trigger('followPrinterFan', 'change');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('followPrinterFan'), 'off');
  assert.equal(body.get('staticfan'), 'off');
  page.requests[1].respond(500, 'Flash write failed');
  assert.equal(page.get('fan1Source').textContent, 'Auxiliary Fan');
  assert.equal(page.get('fan1Source').hidden, false);
  page.context.submitOptions({ preventDefault() {} });
  page.requests[2].respond(200, 'Saved');
  page.requests[3].respond(200, JSON.stringify(baseConfig));
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan1Source').textContent, '');
});

test('first independent draft copies follow settings and sync saves preserve dormant Fan 2', async () => {
  const page = await readyPage({ ...baseConfig, followPrinterFan: true, printerFanSource: 'part' });
  page.get('fansSynchronized').checked = false;
  page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('followPrinterFan2').checked, true);
  assert.equal(page.get('printerFanSource2').value, 'part');
  assert.equal(page.get('followPrinterFan2').disabled, false);
  page.get('printerFanSource2').value = 'heatbreak';
  page.get('fansSynchronized').checked = true;
  page.trigger('fansSynchronized', 'change');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.has('followPrinterFan2'), false);
  assert.equal(body.has('printerFanSource2'), false);
  assert.equal(page.get('printerFanSource2').value, 'heatbreak');
});

test('synchronized indicators use Fan 1 and retain saved Fan 2 follow settings', async () => {
  const page = await readyPage({ ...baseConfig, fansSynchronized: true, fan2Configured: true,
    followPrinterFan: true, printerFanSource: 'chamber', followPrinterFan2: true, printerFanSource2: 'part' });
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.equal(page.get('fan2Indicator').hidden, true);
  page.get('fansSynchronized').checked = false;
  page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('followPrinterFan2').checked, true);
  assert.equal(page.get('printerFanSource2').value, 'part');
});

test('mixed follow modes retain an empty source row for the other Fan', async () => {
  for (const followsFirst of [false, true]) {
    const page = await readyPage({ ...separateConfig, staticfans2: false,
      followPrinterFan: followsFirst, printerFanSource: 'chamber',
      followPrinterFan2: !followsFirst, printerFanSource2: 'part' });
    assert.equal(page.get('fan1Source').hidden, false);
    assert.equal(page.get('fan2Source').hidden, false);
    assert.equal(page.get('fan1Source').textContent, followsFirst ? 'Chamber Fan' : '');
    assert.equal(page.get('fan2Source').textContent, followsFirst ? '' : 'Part Cooling Fan');
  }
});

test('submitting before settings arrive cannot overwrite saved credentials', () => {
  const page = loadPage(); let prevented = false;
  assert.equal(page.context.submitOptions({ preventDefault() { prevented = true; } }), false);
  assert.equal(prevented, true); assert.equal(page.requests.length, 1);
});

for (const [name, fail] of [
  ['HTTP error', xhr => xhr.respond(500, 'Failed')],
  ['invalid JSON', xhr => xhr.respond(200, '{')],
  ['missing settings', xhr => xhr.respond(200, '{}')],
  ['network error', xhr => xhr.onerror()],
  ['timeout', xhr => xhr.ontimeout()],
]) {
  test(`${name} shows an error, keeps Save disabled, and permits recovery`, () => {
    const page = loadPage();
    assert.equal(page.requests[0].timeout, 30000);
    fail(page.requests[0]);
    assert.match(page.get('settings-status').textContent, /unable to load/i);
    assert.equal(page.get('saveOptions').disabled, true);
    page.context.updateOptions();
    page.requests[1].respond(200, JSON.stringify(baseConfig));
    assert.equal(page.get('saveOptions').disabled, false);
  });
}

test('independent mode is only applied after successful settings save and reload', async () => {
  const page = await readyPage();
  page.get('staticfan').checked = true; page.get('staticfanspeed').value = '37';
  page.get('fansSynchronized').checked = false; page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('fan1FeaturesTitle').textContent, 'Fan 1');
  assert.equal(page.get('fan1FeaturesTitle').hidden, false);
  assert.equal(page.get('fan2Features').hidden, false);
  assert.equal(page.get('staticfan2').checked, true);
  assert.equal(page.get('staticfanspeed2').value, '37');
  assert.equal(page.get('fan2Indicator').hidden, true);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.get('createPointBtn1').disabled, true);
  assert.match(page.get('mode-status').textContent, /save settings/i);
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('fansSynchronized'), 'off');
  assert.equal(body.get('staticfan2'), 'on');
  assert.equal(body.get('staticfanspeed2'), '37');
  assert.equal(page.get('settings-fields').disabled, true);
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, staticfans: true, staticfanspeed: 37, staticfanspeed2: 37 }));
  await page.respond('/getFanConfig', { points: points1, points2: points1, fansSynchronized: false });
  assert.equal(page.get('fan2Indicator').hidden, false);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.charts[1].data.datasets.length, 1);
  assert.equal(page.get('graphPanel2').hidden, false);
  assert.equal(page.get('createPointBtn1').disabled, false);
});

test('returning to synchronized mode omits hidden Fan 2 settings without clearing them', async () => {
  const page = await readyPage(separateConfig);
  assert.equal(page.get('fansSynchronized').checked, false);
  page.get('fansSynchronized').checked = true; page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('fan2Features').hidden, true);
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('fansSynchronized'), 'on');
  assert.equal(body.has('staticfan2'), false); assert.equal(body.has('staticfanspeed2'), false);
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, fansSynchronized: true }));
  await page.respond('/getFanConfig', { points: points1, points2, fansSynchronized: true });
  assert.equal(page.charts[0].data.datasets.length, 1);
  page.get('fansSynchronized').checked = false; page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('staticfan2').checked, true);
  assert.equal(page.get('staticfanspeed2').value, 69);
});

test('a failed settings save retains the active synchronized view and shows the error', async () => {
  const page = await readyPage();
  page.get('fansSynchronized').checked = false; page.trigger('fansSynchronized', 'change');
  page.context.submitOptions({ preventDefault() {} });
  page.requests[1].respond(500, 'Failed to save settings to flash');
  assert.match(page.get('settings-status').textContent, /failed to save/i);
  assert.equal(page.get('fan2Indicator').hidden, true);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.get('saveOptions').disabled, false);
  assert.equal(page.get('save-notice').hidden, true);
  page.advanceTime(5000);
  assert.match(page.get('settings-status').textContent, /failed to save/i);
});

test('settings save shows a temporary notice that survives reloading settings', async () => {
  const page = await readyPage();
  page.context.submitOptions({ preventDefault() {} });
  page.requests[1].respond(200, 'Settings saved');
  assert.equal(page.get('save-notice').textContent, 'Settings saved successfully.');
  assert.equal(page.get('save-notice').hidden, false);
  page.requests[2].respond(200, JSON.stringify(baseConfig));
  page.advanceTime(3999);
  assert.equal(page.get('save-notice').hidden, false);
  page.advanceTime(1);
  assert.equal(page.get('save-notice').hidden, true);
  assert.equal(page.get('save-notice').textContent, '');
});

test('a newer save receives a full notice duration and a failed save has no stale success', async () => {
  const page = await readyPage(separateConfig);
  page.context.saveCurve(points1);
  await page.respond('/updateFanConfig', 'Saved');
  page.advanceTime(3000);
  page.context.saveCurve(points1);
  assert.equal(page.get('save-notice').hidden, true);
  await page.respond('/updateFanConfig', 'Saved');
  page.advanceTime(1000);
  assert.equal(page.get('save-notice').hidden, false);
  page.advanceTime(3000);
  assert.equal(page.get('save-notice').hidden, true);
  page.context.saveCurve(points1);
  await page.respond('/updateFanConfig', 'Saved');
  page.context.saveCurve(points1);
  await page.respond('/updateFanConfig', 'Flash write failed', 500);
  page.advanceTime(5000);
  assert.equal(page.get('save-notice').hidden, true);
  assert.match(page.get('curve-status1').textContent, /Flash write failed/);
});

test('each graph opens its own editor without changing the other selection', async () => {
  const page = await readyPage(separateConfig);
  assert.equal(page.get('selectedFan'), undefined);
  for (const fan of [1, 0]) {
    page.charts[fan].options.onClick({}, [{ datasetIndex: 0, index: 1 }]);
    assert.equal(page.state(`editors[${fan}].selectedIndex`), 1);
    assert.equal(page.get('editSection' + (fan + 1)).style.display, 'block');
    assert.equal(page.get('pointHeading' + (fan + 1)).textContent, 'Selected point — Fan ' + (fan + 1));
    assert.equal(page.get('edit-input' + (fan + 1)).value, 200);
    assert.equal(page.get('edit-speed' + (fan + 1)).value, [60, 40][fan]);
  }
  page.get('edit-speed1').value = '45';
  page.trigger('editPointBtn1', 'click');
  const body = new URLSearchParams(page.pending('/updateFanConfig').options.body);
  assert.equal(body.get('fan'), '1');
  assert.equal(body.get('mode'), 'temperature');
  page.charts[1].options.onClick({}, [{ datasetIndex: 0, index: 0 }]);
  assert.equal(page.state('editors[1].selectedIndex'), 1, 'Ignore clicks while saving');
  await page.respond('/updateFanConfig', 'Saved');
  assert.deepEqual(page.state('curves[1]'), points2);
  assert.equal(page.state('curves[0][1].speed'), 45);
  assert.equal(page.state('editors[1].selectedIndex'), 1);
});

test('editing Fan 2 targets only Fan 2 and only changes the saved display on success', async () => {
  const page = await readyPage(separateConfig);
  page.charts[1].options.onClick({}, [{ datasetIndex: 0, index: 1 }]);
  page.get('edit-input2').value = '180'; page.get('edit-speed2').value = '55';
  page.trigger('editPointBtn2','click');
  const body = new URLSearchParams(page.pending('/updateFanConfig').options.body);
  assert.equal(body.get('fan'), '2');
  assert.deepEqual(JSON.parse(body.get('points')).points[1], { temp: 180, speed: 55 });
  assert.deepEqual(page.state('curves'), [points1, points2]);
  assert.equal(page.get('createPointBtn1').disabled, true);
  assert.equal(page.get('createPointBtn2').disabled, true);
  assert.equal(page.get('saveOptions').disabled, true);
  await page.respond('/updateFanConfig', 'Fan curve saved');
  assert.deepEqual(page.state('curves[0]'), points1);
  assert.deepEqual(page.state('curves[1][1]'), { temp: 180, speed: 55 });
  assert.equal(page.get('save-notice').textContent, 'Fan 2 curve saved.');
  assert.equal(page.get('save-notice').hidden, false);
  page.advanceTime(4000);
  assert.equal(page.get('save-notice').hidden, true);
});

test('failed curve saves leave the previous curve visible', async () => {
  const page = await readyPage(separateConfig);
  const saving = page.context.saveCurve([{ temp: 10, speed: 80 }], 1);
  await page.respond('/updateFanConfig','Flash write failed',500); await saving;
  assert.deepEqual(page.state('curves'), [points1, points2]);
  assert.match(page.get('curve-status2').textContent, /flash write failed/i);
  assert.equal(page.get('createPointBtn2').disabled, false);
});

test('point creation, validation, and removal do not overwrite the other fan', async () => {
  const page = await readyPage(separateConfig);
  page.trigger('createPointBtn2','click');
  assert.equal(page.fetches.filter(r => r.url === '/updateFanConfig').length, 0);
  page.get('edit-input2').value = '200'; page.get('edit-speed2').value = '30';
  page.trigger('editPointBtn2','click');
  assert.match(page.get('curve-status2').textContent, /unique/i);
  page.get('edit-input2').value = '100'; page.get('edit-speed2').value = '30.5';
  page.trigger('editPointBtn2','click');
  assert.match(page.get('curve-status2').textContent, /whole-number/i);
  assert.equal(page.fetches.filter(r => r.url === '/updateFanConfig').length, 0);
  page.get('edit-speed2').value = '30'; page.trigger('editPointBtn2','click');
  await page.respond('/updateFanConfig','Saved');
  assert.equal(page.state('curves[1]').length, 4);
  assert.deepEqual(page.state('curves[0]'),points1);
  page.charts[1].options.onClick({}, [{ datasetIndex: 0, index: 1 }]);
  page.trigger('removePointBtn2','click'); await page.respond('/updateFanConfig','Saved');
  assert.deepEqual(page.state('curves'),[points1,points2]);
  await page.context.saveCurve([], 1);
  assert.match(page.get('curve-status2').textContent, /at least one/i);
});

test('sensor data controls two distinct animations and speed labels', async () => {
  const page = await readyPage(separateConfig);
  const polling = page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 180, speed: 0, speed2: 100 }); await polling;
  assert.equal(page.get('blades1').style.animation,'none');
  assert.equal(page.get('blades2').style.animation,'spin 0.5s linear infinite');
  assert.equal(page.get('fan1Percent').textContent,0);
  assert.equal(page.get('fan2Percent').textContent,100);
});

test('curve loading failures disable editing and successful retry restores it', async () => {
  const page = loadPage(); page.events.DOMContentLoaded();
  page.requests[0].respond(200, JSON.stringify(separateConfig));
  assert.equal(page.get('createPointBtn1').disabled, true);
  await page.respond('/getFanConfig', {},500);
  assert.equal(page.get('retryCurves').hidden, false);
  assert.equal(page.get('createPointBtn1').disabled, true);
  page.trigger('retryCurves','click');
  await page.respond('/getFanConfig',{ points: points1, points2, fansSynchronized: false });
  assert.equal(page.get('createPointBtn1').disabled,false);
});

test('mixed modes show separate axes, curves and reported inputs', async () => {
  const page = await readyPage({ ...separateConfig, staticfans2: false, followPrinterFan2: true,
    printerFanSource2: 'chamber' }, { followPoints: identity, followPoints2: exhaust });
  assert.equal(page.get('graphPanel2').hidden, false);
  assert.equal(page.charts[0].options.scales.x.title.text, 'Nozzle temperature (°C)');
  assert.equal(page.charts[0].options.scales.x.max, 400);
  assert.equal(page.charts[1].options.scales.x.title.text, 'Printer fan (%)');
  assert.equal(page.charts[1].options.scales.x.max, 100);
  assert.deepEqual(copy(page.charts[1].data.datasets[0].data), exhaust.map(p => ({ x: p.input, y: p.speed })));
  assert.match(page.get('sensorInput2').textContent, /Not reported/);
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 200, speed: 60, speed2: 80, printerFans: { chamber: 70 } });
  assert.equal(page.get('sensorInput1').textContent, 'Nozzle temperature: 200 °C');
  assert.equal(page.get('sensorInput2').textContent, 'Chamber Fan: 70 %');
  assert.equal(page.get('speedValue2').textContent, 80);
});

test('follow curves default to identity and edits preserve temperature curves and the other fan', async () => {
  const page = await readyPage({ ...separateConfig, followPrinterFan: true, printerFanSource: 'chamber' });
  assert.deepEqual(page.state('followCurves'), [identity, identity]);
  page.get('pointSelect1').value = '1'; page.trigger('pointSelect1', 'change');
  assert.equal(page.get('edit-input1').disabled, true);
  assert.equal(page.get('edit-speed1').disabled, false);
  assert.equal(page.get('removePointBtn1').disabled, true);
  page.get('edit-speed1').value = '35'; page.trigger('editPointBtn1', 'click');
  const body = new URLSearchParams(page.pending('/updateFanConfig').options.body);
  assert.equal(body.get('mode'), 'follow');
  assert.equal(body.get('fan'), '1');
  assert.deepEqual(JSON.parse(body.get('points')).points, exhaust);
  await page.respond('/updateFanConfig', 'Saved');
  assert.deepEqual(page.state('curves'), [points1, points2]);
  assert.deepEqual(page.state('followCurves'), [exhaust, identity]);
});

test('follow anchor inputs cannot move or be removed and zero output is locked', async () => {
  const page = await readyPage({ ...baseConfig, followPrinterFan: true });
  for (const index of [0, 1, 2]) {
    page.charts[0].options.onClick({}, [{ datasetIndex: 0, index }]);
    assert.equal(page.get('edit-input1').disabled, true);
    assert.equal(page.get('removePointBtn1').disabled, true);
    assert.equal(page.get('edit-speed1').disabled, index === 0);
    assert.equal(page.get('editPointBtn1').disabled, index === 0);
    page.trigger('removePointBtn1', 'click');
  }
  assert.equal(page.fetches.filter(r => r.url === '/updateFanConfig').length, 0);
  page.trigger('createPointBtn1', 'click');
  page.get('edit-input1').value = '50'; page.get('edit-speed1').value = '70';
  page.trigger('editPointBtn1', 'click');
  await page.respond('/updateFanConfig', 'Saved');
  page.charts[0].options.onClick({}, [{ datasetIndex: 0, index: 2 }]);
  assert.equal(page.get('edit-input1').disabled, false);
  assert.equal(page.get('removePointBtn1').disabled, false);
  page.trigger('removePointBtn1', 'click');
  await page.respond('/updateFanConfig', 'Saved');
  assert.deepEqual(page.state('followCurves[0]'), identity);
});

test('invalid follow inputs are rejected before sending and failed saves retain the mapping', async () => {
  const page = await readyPage({ ...baseConfig, followPrinterFan: true }, { followPoints: exhaust });
  for (const bad of [[], identity.slice(1), identity.slice(0,2),
      [{ input: 0, speed: 1 }, ...identity.slice(1)], [...identity, { input: 0.5, speed: 35 }],
      [...identity, { input: 101, speed: 50 }], [...identity, { input: 50, speed: 35.5 }]]) {
    await page.context.saveCurve(bad);
    assert.equal(page.fetches.filter(r => r.url === '/updateFanConfig').length, 0);
    assert.notEqual(page.get('curve-status1').textContent, '');
  }
  page.context.saveCurve(identity);
  await page.respond('/updateFanConfig', 'Flash write failed', 500);
  assert.deepEqual(page.state('followCurves[0]'), exhaust);
});

test('unsaved mode or source changes block both editors until saved and reloaded', async () => {
  const page = await readyPage(separateConfig, { followPoints: exhaust, followPoints2: identity });
  page.charts[0].options.onClick({}, [{ datasetIndex: 0, index: 1 }]);
  page.get('followPrinterFan').checked = true; page.trigger('followPrinterFan', 'change');
  assert.equal(page.get('createPointBtn1').disabled, true);
  assert.equal(page.get('createPointBtn2').disabled, true);
  assert.equal(page.charts[0].options.scales.x.max, 400);
  page.context.submitOptions({ preventDefault() {} });
  page.requests[1].respond(200, 'Saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, followPrinterFan: true, printerFanSource: 'chamber' }));
  await page.respond('/getFanConfig', { points: points1, points2, fansSynchronized: false,
    followPoints: exhaust, followPoints2: identity });
  assert.equal(page.get('editSection1').style.display, 'none');
  assert.equal(page.charts[0].options.scales.x.max, 100);
  assert.deepEqual(page.state('curves[0]'), points1);
  assert.equal(page.get('createPointBtn1').disabled, false);
  page.get('printerFanSource').value = 'part'; page.trigger('printerFanSource', 'change');
  assert.equal(page.get('createPointBtn1').disabled, true);
  page.get('printerFanSource').value = 'chamber'; page.trigger('printerFanSource', 'change');
  assert.equal(page.get('createPointBtn1').disabled, false);
});

test('Post Print defaults off and can be enabled alongside either normal override', async () => {
  const page = await readyPage(separateConfig);
  for (const suffix of ['', '2']) {
    assert.equal(page.get('postPrint' + suffix).checked, false);
    assert.equal(page.get('postPrintSeconds' + suffix).value, 600);
    assert.equal(page.get('postPrintSpeed' + suffix).value, 100);
    page.get('postPrint' + suffix).checked = true;
    page.trigger('postPrint' + suffix, 'change');
    for (const mode of ['staticfan', 'followPrinterFan']) {
      page.get(mode + suffix).checked = true;
      page.trigger(mode + suffix, 'change');
      assert.equal(page.get('postPrint' + suffix).checked, true);
      page.get('postPrint' + suffix).checked = false;
      page.trigger('postPrint' + suffix, 'change');
      assert.equal(page.get(mode + suffix).checked, true);
      page.get('postPrint' + suffix).checked = true;
      page.trigger('postPrint' + suffix, 'change');
      assert.equal(page.get(mode + suffix).checked, true);
    }
  }
});

test('Post Print saves independent values and explicitly saves Off without altering following', async () => {
  const page = await readyPage({ ...separateConfig, staticfans2: false,
    followPrinterFan: true, printerFanSource: 'chamber', postPrint: true, postPrintSeconds: 600, postPrintSpeed: 100,
    postPrint2: true, postPrintSeconds2: 30, postPrintSpeed2: 40 });
  assert.equal(page.get('postPrint2').checked, true);
  assert.equal(page.get('postPrintSeconds2').value, 30);
  page.get('postPrint').checked = false;
  page.trigger('postPrint', 'change');
  page.get('postPrintSeconds2').value = '120';
  page.get('postPrintSpeed2').value = '0';
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('postPrint'), 'off');
  assert.equal(body.get('followPrinterFan'), 'on');
  assert.equal(body.get('postPrint2'), 'on');
  assert.equal(body.get('postPrintSeconds2'), '120');
  assert.equal(body.get('postPrintSpeed2'), '0');
  page.requests[1].respond(500, 'Flash write failed');
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.match(page.get('settings-status').textContent, /Flash write failed/);
});

test('first split clones Post Print and synchronization omits all dormant Fan 2 inputs', async () => {
  const page = await readyPage({ ...baseConfig, postPrint: true, postPrintSeconds: 90, postPrintSpeed: 70 });
  page.get('fansSynchronized').checked = false;
  page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('postPrint2').checked, true);
  assert.equal(page.get('postPrintSeconds2').value, 90);
  assert.equal(page.get('postPrintSpeed2').value, 70);
  assert.equal(page.get('postPrintSeconds2').disabled, false);
  page.get('postPrintSeconds2').value = '45';
  page.get('fansSynchronized').checked = true;
  page.trigger('fansSynchronized', 'change');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  for (const name of ['postPrint2', 'postPrintSeconds2', 'postPrintSpeed2']) {
    assert.equal(body.has(name), false);
    assert.equal(page.get(name).disabled, true);
  }
  assert.equal(page.get('postPrintSeconds2').value, '45');
});

test('telemetry shows separate Post Print countdowns and restores normal source labels at expiration', async () => {
  const page = await readyPage({ ...separateConfig, followPrinterFan: true, printerFanSource: 'chamber' });
  for (const [remaining, remaining2] of [[600, 90], [510, 0], [0, 0]]) {
    const polling = page.context.fetchSensorData();
    await page.respond('/sensorData', { temp: 20, speed: remaining ? 100 : 70, speed2: remaining2 ? 50 : 69,
      postPrintRemaining: remaining, postPrintRemaining2: remaining2 });
    await polling;
    assert.equal(page.get('fan1Source').textContent, remaining ? `Post Print: ${remaining}s` : 'Chamber Fan');
    assert.equal(page.get('fan2Source').textContent, remaining2 ? `Post Print: ${remaining2}s` : '');
    assert.equal(page.get('fan1Source').hidden, false);
    assert.equal(page.get('fan2Source').hidden, false);
    assert.equal(page.get('fan1Percent').textContent, remaining ? 100 : 70);
  }
});

test('failed telemetry clears stale countdowns and pending polls cannot pile up', async () => {
  const page = await readyPage();
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 100, postPrintRemaining: 10, postPrintRemaining2: 10 });
  assert.equal(page.get('fan1Source').textContent, 'Post Print: 10s');
  page.context.fetchSensorData();
  page.context.fetchSensorData();
  assert.equal(page.fetches.filter(request => request.url === '/sensorData' && !request.done).length, 1);
  await page.respond('/sensorData', {}, 500);
  assert.equal(page.get('fan1Source').textContent, '');
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 100, postPrintRemaining: 8, postPrintRemaining2: 8 });
  assert.equal(page.get('fan1Source').textContent, 'Post Print: 8s');
});

async function runningPostPrintPage(config = separateConfig) {
  const page = await readyPage(config);
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 90,
    postPrintRemaining: 600, postPrintRemaining2: 120 });
  return page;
}

test('fan image buttons activate only during Post Print and cancel the selected fan', async () => {
  const page = await readyPage(separateConfig);
  assert.equal(page.get('fanButton1').disabled, true);
  page.trigger('fanButton1', 'click');
  assert.equal(page.fetches.filter(r => r.url === '/cancelPostPrint').length, 0);
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 90,
    postPrintRemaining: 600, postPrintRemaining2: 120 });
  assert.equal(page.get('fanButton2').disabled, false);
  assert.match(page.get('fanButton2').ariaLabel, /Cancel Post Print for Fan 2/);
  page.trigger('fanButton2', 'click');
  const request = page.pending('/cancelPostPrint');
  assert.equal(request.options.method, 'POST');
  assert.equal(new URLSearchParams(request.options.body).get('fan'), '2');
  assert.equal(page.get('fanButton1').disabled, true);
  assert.equal(page.get('fanButton2').disabled, true);
  page.trigger('fanButton2', 'click');
  assert.equal(page.fetches.filter(r => r.url === '/cancelPostPrint').length, 1);
  assert.equal(page.get('fan2Source').textContent, 'Post Print: 120s');
  await page.respond('/cancelPostPrint', { temp: 20, speed: 100, speed2: 0,
    postPrintRemaining: 600, postPrintRemaining2: 0 });
  assert.equal(page.get('fan1Source').textContent, 'Post Print: 600s');
  assert.equal(page.get('fanButton1').disabled, false);
  assert.equal(page.get('fanButton2').disabled, true);
  assert.equal(page.get('fan2Source').textContent, '');
  assert.equal(page.get('blades2').style.animation, 'none');
  assert.equal(page.get('fan2Percent').textContent, 0);
  assert.match(page.get('save-notice').textContent, /canceled for Fan 2/);
});

test('synchronized image cancels both outputs and restores the normal source label', async () => {
  const page = await runningPostPrintPage({ ...baseConfig, followPrinterFan: true, printerFanSource: 'chamber', postPrint: true });
  assert.match(page.get('fanButton1').ariaLabel, /Both fans/);
  page.trigger('fanButton1', 'click');
  assert.equal(new URLSearchParams(page.pending('/cancelPostPrint').options.body).get('fan'), '1');
  await page.respond('/cancelPostPrint', { temp: 20, speed: 40, speed2: 40,
    postPrintRemaining: 0, postPrintRemaining2: 0 });
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.equal(page.get('fan1Percent').textContent, 40);
  assert.equal(page.get('fanButton1').disabled, true);
  assert.equal(page.get('postPrint').checked, true);
  assert.match(page.get('save-notice').textContent, /Both fans/);
});

test('failed cancellation retains countdowns and offers a retry without a success notice', async () => {
  const page = await runningPostPrintPage();
  page.trigger('fanButton1', 'click');
  await page.respond('/cancelPostPrint', 'Request failed', 500);
  assert.match(page.get('fan-action-status').textContent, /Unable to confirm/);
  assert.equal(page.get('fan1Source').textContent, 'Post Print: 600s');
  assert.equal(page.get('fanButton1').disabled, false);
  assert.equal(page.get('save-notice').hidden, true);
  page.trigger('fanButton1', 'click');
  await page.respond('/cancelPostPrint', { temp: 20, speed: 0, speed2: 90,
    postPrintRemaining: 0, postPrintRemaining2: 120 });
  assert.equal(page.get('fan-action-status').textContent, '');
  assert.equal(page.get('fan1Source').textContent, '');
});

for (const status of [200, 500]) {
  test(`an older telemetry response (${status}) cannot restore a canceled countdown or clear the other fan`, async () => {
    const page = await runningPostPrintPage();
    page.context.fetchSensorData(); // This poll began before cancellation.
    page.trigger('fanButton1', 'click');
    page.context.fetchSensorData();
    assert.equal(page.fetches.filter(r => r.url === '/sensorData' && !r.done).length, 1);
    await page.respond('/cancelPostPrint', { temp: 20, speed: 0, speed2: 90,
      postPrintRemaining: 0, postPrintRemaining2: 120 });
    await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 90,
      postPrintRemaining: 600, postPrintRemaining2: 120 }, status);
    assert.equal(page.get('fan1Source').textContent, '');
    assert.equal(page.get('fan1Percent').textContent, 0);
    assert.equal(page.get('fan2Source').textContent, 'Post Print: 120s');
    assert.equal(page.get('fanButton2').disabled, false);
  });
}

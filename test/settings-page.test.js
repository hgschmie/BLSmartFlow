// Run with: node --test test/settings-page.test.js
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

const html = readFileSync(join(__dirname, '../src/www/fanpage.html'), 'utf8');
const script = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)]
  .map(match => match[1])
  .find(script => script.includes('function updateOptions()'));

function loadPage() {
  const elements = new Map([...html.matchAll(/\bid=['"]([^'"]+)['"]/g)]
    .map(match => [match[1], { value: '', checked: false, textContent: '' }]));
  const requests = [];
  class XMLHttpRequest {
    constructor() { requests.push(this); }
    open(method, url) { this.method = method; this.url = url; }
    send() {}
    respond(status, body) {
      this.status = status;
      this.responseText = body;
      this.readyState = 4;
      this.onreadystatechange();
    }
  }
  const context = vm.createContext({
    XMLHttpRequest,
    // Resource load events deliberately never fire: external images may stall.
    window: {},
    document: {
      querySelectorAll: () => [],
      getElementById: id => {
        assert.ok(elements.has(id), `Missing page element: ${id}`);
        return elements.get(id);
      },
    },
  });
  vm.runInContext(script, context);
  return { context, elements, requests };
}

const config = {
  firmwareversion: 'Stable 1.0.2 (2026.10.23)',
  ip: '192.0.2.83',
  code: '*****123',
  id: '************456',
  staticfans: false,
  staticfanspeed: 0,
  debuging: true,
  debugingchange: false,
  mqttdebug: false,
};

test('settings load without waiting for external images or window.onload', () => {
  const { elements, requests } = loadPage();
  assert.equal(requests.length, 1);
  assert.equal(requests[0].method, 'GET');
  assert.equal(requests[0].url, '/getOptions');
  assert.equal(elements.get('saveOptions').disabled, true);
  requests[0].respond(200, JSON.stringify(config));
  assert.equal(elements.get('printerip').value, config.ip);
  assert.equal(elements.get('printercode').value, config.code);
  assert.equal(elements.get('printerserial').value, config.id);
  assert.equal(elements.get('debuging').checked, true);
  assert.equal(elements.get('debugingchange').checked, false);
  assert.equal(elements.get('saveOptions').disabled, false);
  assert.equal(elements.get('settings-status').textContent, '');
  assert.ok(elements.get('firmwareversion').textContent.includes(config.firmwareversion));
});

test('submitting before settings arrive cannot overwrite saved credentials', () => {
  const { context, requests } = loadPage();
  let prevented = false;
  assert.equal(context.submitOptions({ preventDefault() { prevented = true; } }), false);
  assert.equal(prevented, true);
  assert.equal(requests.length, 1);
});

for (const [name, fail] of [
  ['HTTP error', xhr => xhr.respond(500, 'Failed')],
  ['invalid JSON', xhr => xhr.respond(200, '{')],
  ['missing settings', xhr => xhr.respond(200, '{}')],
  ['network error', xhr => xhr.onerror()],
  ['timeout', xhr => xhr.ontimeout()],
]) {
  test(`${name} shows an error and keeps Save disabled`, () => {
    const { context, elements, requests } = loadPage();
    assert.equal(requests[0].timeout, 30000);
    fail(requests[0]);
    assert.match(elements.get('settings-status').textContent, /unable to load/i);
    assert.equal(elements.get('saveOptions').disabled, true);
    // A subsequent successful load recovers the form.
    context.updateOptions();
    requests[1].respond(200, JSON.stringify(config));
    assert.equal(elements.get('saveOptions').disabled, false);
    assert.equal(elements.get('printerip').value, config.ip);
  });
}

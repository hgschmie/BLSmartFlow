const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const vm = require('node:vm');
const html = readFileSync(`${__dirname}/../src/www/wifiPortal.html`, 'utf8');
const script = /<script>([\s\S]*?)<\/script>/.exec(html)[1];
const settle = () => new Promise(resolve => setImmediate(resolve));
const status = { ssid:'WTam', hasPassword:true, connected:false, apActive:true, ip:'', firmwareversion:'Stable 1.6.0 (2026.11.20)' };
function page() {
  const elements = new Map([...html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)].map(([tag,id]) => [id, {
    value:'', textContent:'', disabled:/\bdisabled\b/.test(tag), hidden:/\bhidden\b/.test(tag), checked:false,
    listeners:{}, addEventListener(event,fn) { this.listeners[event]=fn; }
  }]));
  const requests=[], timers=[];
  const context=vm.createContext({
    window:{}, document:{ getElementById:id => elements.get(id) }, URLSearchParams,
    AbortSignal:{timeout:()=>({})}, setTimeout:fn=>timers.push(fn),
    fetch(url,options) { return new Promise((resolve,reject)=>requests.push({url,options,resolve,reject})); }
  });
  vm.runInContext(script,context);
  return {
    elements, requests, timers, get:id=>elements.get(id),
    async respond(data,code=200) {
      requests.shift().resolve({ok:code===200,json:async()=>data,text:async()=>data}); await settle();
    },
    submit() { elements.get('wifiForm').listeners.submit({preventDefault(){}}); }
  };
}
test('setup loads without outside assets and prefills only network information', async()=>{
  assert.doesNotMatch(html, /(?:src|href)="https?:\/\//);
  const p=page(); assert.equal(p.get('saveButton').disabled,true);
  p.submit(); assert.equal(p.requests.length,1);
  await p.respond(status);
  assert.equal(p.get('ssid').value,'WTam'); assert.equal(p.get('password').value,'');
  assert.equal(p.get('saveButton').disabled,false);
  assert.match(p.get('connectionStatus').textContent,/Retrying/);
});
test('failed initial load keeps save disabled and later polls do not overwrite editing', async()=>{
  const p=page(); p.requests.shift().reject(new Error('offline')); await settle();
  assert.equal(p.get('saveButton').disabled,true);
  p.timers.shift()(); await p.respond(status);
  p.get('ssid').value='Edited SSID'; p.get('password').value='new-password';
  p.timers.shift()(); await p.respond(status);
  assert.equal(p.get('ssid').value,'Edited SSID'); assert.equal(p.get('password').value,'new-password');
});
test('save posts only Wi-Fi fields, prevents duplicates and reports persistence failure', async()=>{
  const p=page(); await p.respond(status);
  p.get('password').value='new-password'; p.submit(); p.submit();
  assert.equal(p.requests.length,1); assert.equal(p.requests[0].url,'/wifi/save');
  assert.deepEqual(Object.fromEntries(p.requests[0].options.body),{ssid:'WTam',password:'new-password',open:'false'});
  await p.respond('Could not save settings',500);
  assert.equal(p.get('saveStatus').textContent,'Could not save settings');
  assert.equal(p.get('password').value,'new-password'); assert.equal(p.get('saveButton').disabled,false);
  p.submit(); await p.respond('Wi-Fi settings saved. Connecting…');
  assert.match(p.get('saveStatus').textContent,/saved/); assert.equal(p.get('password').value,'');
});
test('connected state shows LAN link and failures clear stale connection indication', async()=>{
  const p=page(); await p.respond({...status,connected:true,ip:'192.168.2.83'});
  assert.equal(p.get('connectedHelp').hidden,false);
  assert.equal(p.get('controllerLink').href,'http://192.168.2.83/dashboard');
  p.timers.shift()(); p.requests.shift().reject(new Error('offline')); await settle();
  assert.equal(p.get('connectedHelp').hidden,true);
  assert.match(p.get('connectionStatus').textContent,/unreachable/);
});
test('open-network selection is explicit and disables the password field', async()=>{
  const p=page(); await p.respond(status);
  assert.equal(p.get('openNetwork').checked,false);
  p.get('openNetwork').checked=true; p.get('openNetwork').listeners.change();
  assert.equal(p.get('password').disabled,true);
  p.submit(); assert.equal(p.requests[0].options.body.get('open'),'true');
  await p.respond('Saved');
});

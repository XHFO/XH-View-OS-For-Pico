/* SPDX-License-Identifier: Apache-2.0
 * 执行固件实际内嵌页面脚本；验证中文、保存预检和重复提交，无网络访问。
 * Execute the actual embedded page script; check Chinese copy, preflight and duplicate saves without networking.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
assert(html.includes('<title>家庭填写</title>') && html.includes('家庭与打印机'));
assert(!html.includes('å®') && !html.includes('\ufffd'));
assert(html.includes("id='go' type='button' disabled"));
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script);

const settle = async () => { for (let i = 0; i < 12; i++) await new Promise(resolve => setImmediate(resolve)); };
function fixture(respond) {
  const elements = new Map();
  class Element {
    constructor() { this.children = []; this.value = ''; this.checked = false; this.disabled = false; this._text = ''; }
    set id(value) { this._id = value; elements.set(value, this); }
    get id() { return this._id; }
    get firstChild() { return this.children[0] || null; }
    set textContent(value) { this._text = String(value); this.children = []; }
    get textContent() { return this._text; }
    appendChild(child) { this.children.push(child); return child; }
    insertBefore(child) { this.children.unshift(child); return child; }
  }
  for (const match of html.matchAll(/id='([^']+)'/g)) { const element = new Element(); element.id = match[1]; }
  elements.get('go').disabled = true;
  const calls = [];
  const context = {
    document: {getElementById: id => elements.get(id), createElement: () => new Element()},
    location: {hash: '#t=' + '5a'.repeat(16)}, Promise, setTimeout: (fn) => setImmediate(fn),
    fetch: async (path, options) => {
      const request = {path, method: options.method, headers: options.headers,
                       body: options.body ? JSON.parse(options.body) : undefined};
      calls.push(request);
      assert.equal(request.headers['X-Pico-Portal-Key'], '5a'.repeat(16));
      const reply = await respond(request);
      return {status: reply.status || 200, json: async () => reply.body || {ok: true, queued: true}};
    },
  };
  vm.createContext(context);
  vm.runInContext(script, context);
  return {context, calls, elements};
}
function getReply(request) {
  if (request.method !== 'GET') return null;
  if (request.path === '/api/lan/home') return {body: {config: {local: 'http://ha.local:8123', has_token: true}}};
  return {body: {printers: [{source: 1, name: 'HA', serial: 'sensor.printer'}, {source: 0}, {source: 0}]}};
}
async function main() {
  let app = fixture(request => getReply(request));
  app.context.save();
  assert.equal(app.calls.filter(call => call.method === 'PUT').length, 0, 'must wait for initial load');
  await settle();
  assert.equal(app.elements.get('go').disabled, false);
  app.elements.get('n2').value = 'Incomplete printer';
  app.context.save();
  await settle();
  assert.equal(app.calls.filter(call => call.method === 'PUT').length, 0, 'validate every printer before the first commit');
  assert(app.elements.get('s').textContent.includes('槽 3'));

  let release;
  app = fixture(request => getReply(request) || (request.path === '/api/lan/home' ?
    new Promise(resolve => { release = resolve; }) : {body: {ok: true, queued: true}}));
  await settle();
  app.elements.get('token').value = 'transient-test-token';
  app.context.save();
  app.context.save();
  assert.equal(app.calls.filter(call => call.method === 'PUT').length, 1, 'ignore duplicate Save while a write is pending');
  assert.equal(app.elements.get('go').disabled, true);
  release({body: {ok: true, queued: true}});
  await settle();
  assert.equal(app.calls.filter(call => call.method === 'PUT').length, 4);
  assert.equal(app.elements.get('go').disabled, false);
  assert.equal(app.elements.get('token').value, '');
  assert(app.elements.get('s').textContent.startsWith('已写入设备'));

  app = fixture(request => getReply(request) || (request.path.includes('slot=1') ?
    {status: 507, body: {error: 'save_failed'}} : {body: {ok: true}}));
  await settle();
  app.context.save();
  await settle();
  assert(app.elements.get('s').textContent.startsWith('部分设置已保存'));
  assert.equal(app.elements.get('go').disabled, false);
  assert.equal(app.calls.filter(call => call.method === 'PUT').length, 3, 'do not continue after a failed slot');
  let searches = 0;
  app = fixture(request => request.path === '/api/lan/home/discover' ?
    {body: ++searches === 1 ? {busy: true, servers: []} : {busy: false, servers: [{name: '<HA>', url: 'http://192.168.1.12:18123'}]}} : getReply(request));
  await settle();
  const beforeURL = app.elements.get('local').value;
  app.context.discover(); app.context.discover(); await settle();
  assert.equal(searches, 2, 'one search then poll; ignore duplicate click');
  assert.equal(app.elements.get('local').value, beforeURL, 'search must not replace the draft');
  assert.equal(app.calls.filter(c => c.method === 'PUT').length, 0);
  app.elements.get('discoveryResults').children[0].onclick();
  assert.equal(app.elements.get('local').value, 'http://192.168.1.12:18123');
  assert.equal(app.calls.filter(c => c.method === 'PUT').length, 0, 'selection is still an unsaved draft');
  assert.equal(app.elements.get('discover').disabled, false);
  app = fixture(request => request.method === 'GET' && request.path === '/api/lan/home' ?
    {body: {config: {local: 'http://ha.local:8123', has_token: true}, current_wifi: 'Live LAN', saved_wifi: []}} :
    getReply(request) || {body: {ok: true, queued: true}});
  await settle();
  assert.equal(app.elements.get('wifi').value, 'Live LAN');
  app.context.save(); await settle();
  assert.equal(app.calls.find(c => c.method === 'PUT' && c.path === '/api/lan/home').body.home_wifi, 'Live LAN');
  app = fixture(request => request.path === '/api/lan/home/discover' ? {status: 409, body: {error: 'offline'}} : getReply(request));
  await settle(); app.context.discover(); await settle();
  assert(app.elements.get('discoveryStatus').textContent.includes('Wi-Fi'));
  assert.equal(app.elements.get('discover').disabled, false);
  console.log('PASS: actual UTF-8 portal page, all-slot preflight, duplicate-submit guard and partial-save feedback');
}
main().catch(error => { console.error(error); process.exitCode = 1; });

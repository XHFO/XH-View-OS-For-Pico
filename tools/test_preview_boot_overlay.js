/* Run the real preview.js against controlled native bootstrap and image-decode promises.
 * No browser, native process, device or existing service is accessed.
 */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const directory = path.resolve(__dirname, '../../../outputs/firmware-preview');
const script = fs.readFileSync(path.join(directory, 'preview.js'), 'utf8');
const html = fs.readFileSync(path.join(directory, 'index.html'), 'utf8');
const css = fs.readFileSync(path.join(directory, 'preview.css'), 'utf8');
const output = process.argv[2] && path.resolve(process.argv[2]);
const flush = () => new Promise(resolve => setImmediate(resolve));
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => {resolve = yes; reject = no;});
  return {promise, resolve, reject};
}

async function fixture() {
  const nodes = new Map(), requests = [], draws = [], images = [];
  let bootstrap = deferred(), session = deferred();
  class Element {
    constructor(id) {this.id = id; this.hidden = false; this.dataset = {}; this.classList = {toggle() {}};}
    addEventListener() {}
    setAttribute() {}
    getContext() {return {drawImage: (...args) => draws.push(args)};}
    replaceChildren() {}
    append() {}
    set textContent(value) {
      assert.notEqual(this.id, 'loading', 'An error or retry must preserve the exact bitmap element');
      this.text = value;
    }
    get textContent() {return this.text;}
  }
  const document = new Element('document');
  document.hidden = false;
  document.getElementById = id => {if (!nodes.has(id)) nodes.set(id, new Element(id)); return nodes.get(id);};
  document.querySelectorAll = () => [];
  document.createElement = () => new Element('created');
  const native = {ok: true, session: 'isolated-preview-session', version: 'test', width: 684, height: 1216,
    route: 0, now_ms: 10000, image_url: '/api/frame?isolated=1'};
  const fetch = async url => {
    requests.push(url);
    if (url === '/api/bootstrap') {await bootstrap.promise; return {ok: true, json: async () => ({candidate: true, version: 'test'})};}
    if (url === '/app-packages/catalog.json') return {ok: true, json: async () => ({apps: []})};
    assert(['/api/session', '/api/restart'].includes(url), 'Unexpected fetch '+url);
    await session.promise;
    return {ok: true, json: async () => ({...native})};
  };
  const context = vm.createContext({document, window: new Element('window'), fetch,
    performance: {now: () => 0}, sessionStorage: {getItem: () => null, setItem() {}},
    setInterval() {}, setTimeout() {}, clearTimeout() {},
    Image: class {
      constructor() {this.pending = deferred(); images.push(this);}
      decode() {return this.pending.promise;}
    }});
  vm.runInContext(script, context, {filename: path.join(directory, 'preview.js')});
  await flush();
  return {nodes, requests, draws, images, native,
    bootstrap: () => bootstrap, session: () => session,
    reset() {bootstrap = deferred(); session = deferred();},
    restart: () => nodes.get('restart').onclick()};
}

(async () => {
  const checks = [];
  assert.match(html, /<img[^>]+class="boot-splash"[^>]+src="\/api\/boot-splash"[^>]+width="684"[^>]+height="1216"/);
  assert.match(html, /id="loading"[^>]+role="status"[^>]+aria-live="polite"/);
  assert.match(html, /id="loading-status"/);
  assert.match(css, /\.boot-splash\{[^}]*object-fit:contain/);
  assert.doesNotMatch(css.match(/\.loading\{[^}]+\}/)[0], /animation|transition/);
  checks.push('html-uses-exact-source-raster-and-accessible-status-without-animation');
  const t = await fixture();
  assert.equal(t.nodes.get('loading').hidden, false);
  assert.deepEqual(t.requests, ['/api/bootstrap']);
  t.bootstrap().resolve(); await flush(); await flush();
  assert.equal(t.nodes.get('loading').hidden, false);
  assert.equal(t.images.length, 0);
  t.session().resolve(); await flush(); await flush();
  assert.equal(t.images.length, 1);
  assert.equal(t.images[0].src, t.native.image_url);
  assert.equal(t.nodes.get('loading').hidden, false);
  assert.equal(t.draws.length, 0);
  t.images[0].pending.resolve(); await flush(); await flush();
  assert.equal(t.draws.length, 1);
  assert.equal(t.nodes.get('loading').hidden, true);
  assert.equal(t.nodes.get('status').textContent, '已就绪 · 可直接操作屏幕');
  checks.push('overlay-stays-through-bootstrap-native-session-and-frame-decode');
  checks.push('overlay-hides-immediately-after-actual-native-frame-draw');
  t.reset();
  const failed = t.restart(); await flush();
  assert.equal(t.nodes.get('loading').hidden, false);
  t.bootstrap().reject(new Error('isolated-startup-error'));
  await failed;
  assert.equal(t.nodes.get('loading').hidden, false);
  assert.equal(t.nodes.get('loading').dataset.failed, 'true');
  assert.equal(t.nodes.get('loading-status').textContent, '启动未完成，请点击「重新开始预览」');
  checks.push('failed-bootstrap-retains-raster-and-displays-retry-status');
  t.reset();
  const retry = t.restart(); await flush();
  assert.equal(t.nodes.get('loading').dataset.failed, undefined);
  assert.equal(t.nodes.get('loading-status').textContent, '正在启动 XH View OS For Pico 固件预览…');
  t.bootstrap().resolve(); t.session().resolve(); await flush(); await flush();
  assert.equal(t.images.length, 2);
  assert.equal(t.nodes.get('loading').hidden, false);
  t.images[1].pending.resolve(); await retry;
  assert.equal(t.nodes.get('loading').hidden, true);
  assert.equal(t.draws.length, 2);
  checks.push('retry-clears-error-and-hides-only-after-new-native-frame');
  const broken = await fixture();
  broken.bootstrap().resolve(); broken.session().resolve(); await flush(); await flush();
  broken.images[0].pending.reject(new Error('isolated-frame-decode-error')); await flush(); await flush();
  assert.equal(broken.nodes.get('loading').hidden, false);
  assert.equal(broken.nodes.get('loading').dataset.failed, 'true');
  assert.equal(broken.draws.length, 0);
  checks.push('frame-decode-error-does-not-reveal-empty-canvas');
  const report = {result: 'PASS', checks, real_preview_script: true, controlled_dom_and_image_decode: true,
    browser_access: false, native_runtime_started: false, hardware_access: false,
    script_sha256: crypto.createHash('sha256').update(script).digest('hex')};
  if (output) {fs.mkdirSync(output, {recursive: true}); fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2)+'\n');}
  console.log(`PASS: ${checks.length} real preview.js startup-overlay scenarios`);
})().catch(error => {console.error(error); process.exitCode = 1;});

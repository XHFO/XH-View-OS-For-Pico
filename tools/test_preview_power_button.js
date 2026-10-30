/* 中文：执行真实预览脚本，以 fake DOM/时钟定向检查电源按键；不启动服务、不访问浏览器或设备。
 * English: Run the real preview script with a fake DOM/clock. No browser, server or hardware access.
 */
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');

const preview = path.resolve(__dirname, '../../../outputs/firmware-preview/preview.js');
const source = fs.readFileSync(preview, 'utf8');
const output = process.argv[2] && path.resolve(process.argv[2]);
const checks = [];
const flush = () => new Promise(resolve => setImmediate(resolve));

async function fixture(capability = true) {
  let elapsed = 0, nextTimer = 1;
  const timers = new Map(), nodes = new Map(), requests = [], commands = [];
  class Element {
    constructor(id) { this.id = id; this.listeners = new Map(); this.captured = new Set(); this.dataset = {}; this.classList = {toggle() {}}; }
    addEventListener(type, fn, capture = false) {
      if (!this.listeners.has(type)) this.listeners.set(type, []);
      this.listeners.get(type).push({fn, capture: capture === true || capture?.capture === true});
    }
    dispatch(type, values = {}) {
      const event = {type, target: this, pointerId: 1, button: 0, detail: 1, defaultPrevented: false,
        preventDefault() { this.defaultPrevented = true; }, stopImmediatePropagation() { this.stopped = true; }, ...values};
      const listeners = this.listeners.get(type) || [];
      for (const capture of [true, false]) {
        for (const item of listeners.filter(item => item.capture === capture)) {
          if (event.stopped) break;
          item.fn(event);
        }
        if (!capture && !event.stopped && this['on'+type]) this['on'+type](event);
      }
      return event;
    }
    setPointerCapture(pointer) { this.captured.add(pointer); }
    hasPointerCapture(pointer) { return this.captured.has(pointer); }
    releasePointerCapture(pointer) { this.captured.delete(pointer); this.dispatch('lostpointercapture', {pointerId: pointer}); }
    getContext() { return {drawImage() {}}; }
    setAttribute() {}
    replaceChildren() {}
    append() {}
    focus() {}
  }
  const document = new Element('document');
  document.hidden = false;
  document.getElementById = id => { if (!nodes.has(id)) nodes.set(id, new Element(id)); return nodes.get(id); };
  document.querySelectorAll = () => [];
  document.createElement = () => new Element('created');
  const window = new Element('window');
  const native = {ok: true, session: 'test-only-session', version: 'test-only', width: 720, height: 1280,
    logical_width: 360, logical_height: 640, route: 0, power_state: 0, now_ms: 10000};
  if (capability !== 'missing') native.power_long_available = capability;
  const fetch = async (url, options = {}) => {
    requests.push(url);
    if (url === '/api/bootstrap') return {ok: true, json: async () => ({candidate: true, version: 'test-only', installed_version: 'test-only'})};
    if (url === '/app-packages/catalog.json') return {ok: true, json: async () => ({apps: []})};
    assert(['/api/session', '/api/command'].includes(url), 'Unexpected network boundary '+url);
    if (url === '/api/command') {
      const payload = JSON.parse(options.body); commands.push(payload);
      if (payload.action === 'powerkey') native.power_state = native.power_state ? 0 : 1;
      if (payload.action === 'powerlong') { native.power_state = 0; native.route = 26; }
      native.now_ms = 10000+elapsed;
    }
    return {ok: true, json: async () => ({...native})};
  };
  const context = vm.createContext({document, window, fetch,
    performance: {now: () => elapsed}, sessionStorage: {getItem: () => null, setItem() {}},
    setTimeout(fn, ms) { const id = nextTimer++; timers.set(id, {fn, due: elapsed+ms}); return id; },
    clearTimeout(id) { timers.delete(id); }, setInterval() { return nextTimer++; },
    Image: class {decode() { return Promise.resolve(); }} });
  vm.runInContext(source, context, {filename: preview}); await flush(); await flush();
  assert.equal(nodes.get('status').textContent, '已就绪 · 可直接操作屏幕');
  assert.equal(commands.length, 0);
  async function advance(ms) {
    const target = elapsed+ms;
    while (true) {
      const due = [...timers].filter(([, value]) => value.due <= target).sort((a, b) => a[1].due-b[1].due || a[0]-b[0])[0];
      if (!due) break;
      elapsed = due[1].due; timers.delete(due[0]); due[1].fn(); await flush();
    }
    elapsed = target; await flush();
  }
  const button = nodes.get('power');
  async function dispatch(type, values) { const event = button.dispatch(type, values); await flush(); return event; }
  async function short(ms = 150) { await dispatch('pointerdown'); await advance(ms); await dispatch('pointerup'); await dispatch('click'); }
  return {advance, dispatch, short, button, document, window, commands, requests, native,
    actions: () => commands.map(command => command.action), timers};
}

async function check(name, body) {
  const details = await body();
  checks.push({name, ...(details || {})});
}

(async () => {
  await check('short-only-after-complete-release-and-native-click-suppressed', async () => {
    const t = await fixture(); await t.dispatch('pointerdown'); await t.advance(1499); assert.deepEqual(t.actions(), []);
    await t.dispatch('pointerup'); await t.dispatch('click'); assert.deepEqual(t.actions(), ['powerkey']);
    assert.equal(t.button.captured.size, 0); return {commands: t.commands};
  });
  await check('long-at-exactly-1500ms-once-with-no-following-short-click', async () => {
    const t = await fixture(); await t.dispatch('pointerdown'); await t.advance(1499); assert.deepEqual(t.actions(), []);
    await t.advance(1); assert.deepEqual(t.actions(), ['powerlong']); await t.advance(3000);
    await t.dispatch('pointerup'); await t.dispatch('click'); assert.deepEqual(t.actions(), ['powerlong']);
    assert.equal(t.button.captured.size, 0); return {commands: t.commands};
  });
  for (const event of ['pointercancel', 'lostpointercapture', 'blur', 'visibilitychange']) {
    await check(event+'-cancels-pending-hold-and-release', async () => {
      const t = await fixture(); await t.dispatch('pointerdown'); await t.advance(700);
      if (event === 'blur') t.window.dispatch('blur');
      else if (event === 'visibilitychange') { t.document.hidden = true; t.document.dispatch(event); }
      else if (event === 'lostpointercapture') t.button.releasePointerCapture(1);
      else await t.dispatch(event);
      await t.advance(2000); await t.dispatch('pointerup'); assert.deepEqual(t.actions(), []);
      t.document.hidden = false; await t.short(); assert.deepEqual(t.actions(), ['powerkey']);
      return {commands: t.commands};
    });
  }
  for (const capability of [false, 'missing']) {
    await check('legacy-'+String(capability)+'-preserves-click-and-has-no-long-hold', async () => {
      const t = await fixture(capability); await t.dispatch('pointerdown'); await t.advance(3000);
      assert.deepEqual(t.actions(), []); await t.dispatch('pointerup'); await t.dispatch('click');
      assert.deepEqual(t.actions(), ['powerkey']); return {commands: t.commands};
    });
  }
  await check('keyboard-generated-click-with-detail-zero-still-works', async () => {
    const t = await fixture(); await t.short(); await t.dispatch('click', {detail: 0});
    assert.deepEqual(t.actions(), ['powerkey', 'powerkey']); return {commands: t.commands};
  });
  await check('right-button-and-mismatched-pointer-do-not-create-actions', async () => {
    const t = await fixture(); await t.dispatch('pointerdown', {button: 2}); await t.advance(2000);
    await t.dispatch('pointerup'); assert.deepEqual(t.actions(), []);
    await t.dispatch('pointerdown'); await t.dispatch('pointerup', {pointerId: 7}); assert.deepEqual(t.actions(), []);
    await t.dispatch('pointerup'); await t.dispatch('click'); assert.deepEqual(t.actions(), ['powerkey']);
    return {commands: t.commands};
  });
  await check('duplicate-down-keeps-one-hold-and-next-short-recovers', async () => {
    const t = await fixture(); await t.dispatch('pointerdown'); await t.advance(700); await t.dispatch('pointerdown', {pointerId: 2});
    await t.advance(800); assert.deepEqual(t.actions(), ['powerlong']); await t.dispatch('pointerup'); await t.dispatch('click');
    await t.short(); assert.deepEqual(t.actions(), ['powerlong', 'powerkey']); return {commands: t.commands};
  });
  await check('visible-visibilitychange-does-not-cancel-active-hold', async () => {
    const t = await fixture(); await t.dispatch('pointerdown'); await t.advance(700); t.document.dispatch('visibilitychange');
    await t.advance(800); await t.dispatch('pointerup'); await t.dispatch('click');
    assert.deepEqual(t.actions(), ['powerlong']); return {commands: t.commands};
  });
  const report = {result: 'PASS', checks, count: checks.length, source: preview,
    source_sha256: crypto.createHash('sha256').update(source).digest('hex'),
    runs_real_preview_script: true, fake_dom: true, fake_timers: true,
    browser_access: false, hardware_access: false, http_server_started: false,
    native_1500ms_physical_pmu_threshold_verified: false};
  if (output) {
    assert(!fs.existsSync(output) || fs.readdirSync(output).length === 0, 'Use a new output directory');
    fs.mkdirSync(output, {recursive: true}); fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2)+'\n');
  }
  console.log(`PASS: ${checks.length} real preview.js power-button scenarios`);
})().catch(error => {console.error(error); process.exitCode = 1;});

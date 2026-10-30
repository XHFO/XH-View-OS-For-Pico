/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 执行真实网页脚本，模拟网络与DOM验证用户流程。/ Run the actual webpage script with network and DOM boundaries mocked.
 */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync('components/read_pico_transfer/upload.html', 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const flush = () => new Promise(resolve => setImmediate(resolve));

async function setup({existing = false, confirmations = [], responses = [], deleteResponse = {ok: true}, fontsEnabled = true, bookLimit = 0, fetchHook} = {}) {
  const all = [];
  class Element {
    constructor() { this.children = []; this.dataset = {}; this.value = ''; all.push(this); }
    set textContent(text) { this.text = String(text); this.children = []; }
    get textContent() { return this.text || ''; }
    set innerHTML(_) { throw Error('Untrusted text must not use innerHTML'); }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; }
  }
  const nodes = new Map();
  const document = {getElementById(id) { if (!nodes.has(id)) nodes.set(id, new Element()); return nodes.get(id); },
    createElement() { return new Element(); }, querySelectorAll() { return all.filter(e => e.dataset.mutate); }};
  const state = {sent: [], payloads: [], xhrs: [], requested: [], confirmationMessages: [], deleted: 0, retried: 0, retryURLs: [], infoReads: 0, timers: new Map(), nextTimer: 1};
  const policy = {mode: 'ap', root: '/sdcard/books', free_bytes: 100000000, file_limit: bookLimit, wifi_configured: false,
    fonts_enabled: fontsEnabled, font_root: '/sdcard/fonts', font_limit: 33554432};
  const fetch = async (url, options = {}) => {
    state.requested.push(url);
    let status = 200, body;
    if (url === '/info') { ++state.infoReads; body = policy; }
    else if (options.method === 'DELETE') { ++state.deleted; body = deleteResponse; if (body.progress_cleanup_failed) status = 500; }
    else if (options.method === 'POST') { ++state.retried; state.retryURLs.push(url); body = {ok: true}; }
    else if (url.startsWith('/books?name=') || url.startsWith('/fonts?name=')) { status = existing ? 200 : 404; body = existing ? {item: {name: 'same.txt', size: 10}} : {error: '不存在'}; }
    else body = {root: '/sdcard/books', root_label: 'TF 卡', items: [], total: 0, pages: 0};
    if (fetchHook) { const override = await fetchHook(url, options, state); if (override) { status = override.status; body = override.body; } }
    return {ok: status === 200, status, json: async () => body};
  };
  class XHR {
    constructor() { this.upload = {}; }
    open(method, url) { this.url = url; state.xhrs.push(this); assert.equal(method, 'PUT'); }
    send(file) {
      state.sent.push(this.url);
      state.payloads.push(file);
      this.upload.onprogress({loaded: file.size / 2, total: file.size});
      const response = responses.shift() || {status: 200, body: {ok: true}};
      if (response === 'pending') return;
      queueMicrotask(() => {
        if (response === 'error') return this.onerror();
        this.status = response === 'invalid-json' ? 200 : response.status;
        this.responseText = response === 'invalid-json' ? '<html>not a device response</html>' : JSON.stringify(response.body); this.onload();
      });
    }
    abort() { this.onabort(); }
  }
  const context = vm.createContext({document, fetch, XMLHttpRequest: XHR, TextEncoder, Date, AbortController, setTimeout:(f,ms)=>{const id=state.nextTimer++;state.timers.set(id,{f,ms});return id;}, clearTimeout:id=>state.timers.delete(id),
    confirm: message => { state.confirmationMessages.push(message); return confirmations.length ? confirmations.shift() : true; }});
  vm.runInContext(source, context); await flush(); await flush();
  const file = {name: '<book & test>.txt', size: 5000000};
  function item(name = file.name, kind = 'book') { return {file: {...file, name}, row: new Element(), kind}; }
  async function run(items) { context.testItems = items; return vm.runInContext('run(testItems)', context); }
  return {context, nodes, state, item, run};
}

(async () => {
  let t = await setup({existing: true, confirmations: [false]});
  let item = t.item(); await t.run([item]); assert.match(item.row.textContent, /跳过/); assert.equal(t.state.sent.length, 0);
  t = await setup({existing: true, confirmations: [true]}); item = t.item(); await t.run([item]);
  assert.match(t.state.sent[0], /overwrite=1/); assert.match(item.row.textContent, /已保存/);
  t = await setup({confirmations: [false], responses: [{status: 409, body: {conflict: true}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /跳过/); assert.equal(t.state.sent.length, 1);
  t = await setup({responses: ['error', {status: 200, body: {ok: true}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /网络中断/);
  await item.row.children.find(b => b.textContent === '重试此文件').onclick(); assert.match(item.row.textContent, /已保存/); assert.equal(t.state.sent.length, 2);
  t = await setup({responses: ['pending']}); const items = [t.item('first.txt'), t.item('second.txt')];
  const pending = t.run(items); await flush(); t.nodes.get('cancel').onclick(); await pending;
  assert.equal(t.state.sent.length, 1); assert.match(items[0].row.textContent, /取消/); assert.match(items[1].row.textContent, /未上传/);
  t = await setup({responses: [{status: 500, body: {committed: true, progress_cleanup_failed: true, error: '文件已保存，但旧阅读进度清理失败'}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /已保存/);
  await item.row.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.equal(t.state.sent.length, 1); assert.equal(t.state.retried, 1);
  t = await setup({responses: [{status: 500, body: {committed: true, progress_cleanup_failed: true, name: 'Actual.txt', error: '清理失败'}}]});
  item = t.item('actual.txt'); await t.run([item]);
  await item.row.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.match(t.state.retryURLs[0], /name=Actual\.txt&/);
  t = await setup({deleteResponse: {deleted: true, progress_cleanup_failed: true, name: 'Actual.txt', error: '清理失败'}});
  item = t.item('actual.txt'); t.context.testBook = item.file; t.context.testRow = item.row;
  await vm.runInContext('removeBook(testBook,testRow)', t.context);
  const notice = t.nodes.get('results').children[0];
  await notice.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.match(t.state.retryURLs[0], /name=Actual\.txt&/);
  t = await setup({responses: [{status: 200, body: {ok: true, committed: true, storage_cleanup_failed: true, warning: '旧书备份已保留，请勿重复上传'}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /已保存.*备份已保留/);
  assert.equal(t.state.sent.length, 1); assert.equal(item.row.children.length, 0);
  t = await setup({confirmations: [false, true]}); item = t.item(); t.context.testBook = item.file; t.context.testRow = item.row;
  await vm.runInContext('removeBook(testBook,testRow)', t.context); assert.equal(t.state.deleted, 0);
  await vm.runInContext('removeBook(testBook,testRow)', t.context); assert.equal(t.state.deleted, 1);
  t = await setup({bookLimit: 1048576}); item = t.item('FullChinese.ttf', 'font');
  await t.run([item]); assert.match(t.state.sent[0], /^\/fonts\?name=FullChinese.ttf$/);
  assert(t.state.requested.some(u => u === '/fonts?name=FullChinese.ttf'));
  assert.match(item.row.textContent, /已保存.*字体/); assert.equal(t.state.retried, 0);
  t = await setup({fontsEnabled: false}); item = t.item('Font.ttf', 'font'); await t.run([item]);
  assert.match(item.row.textContent, /TF 卡/); assert.equal(t.state.sent.length, 0); assert(t.nodes.get('sendFonts').disabled);
  t = await setup(); item = t.item('Font.otf', 'font'); await t.run([item]);
  assert.match(item.row.textContent, /仅支持 TTF/); assert.equal(t.state.sent.length, 0);
  item = t.item('Huge.ttf', 'font'); item.file.size = 33554433; await t.run([item]);
  assert.match(item.row.textContent, /超过单文件/); assert.equal(t.state.sent.length, 0);
  t = await setup({existing: true, confirmations: [false]}); item = t.item('Font.ttf', 'font'); await t.run([item]);
  assert.match(item.row.textContent, /已跳过/); assert.equal(t.state.sent.length, 0);
  t = await setup({existing: true, confirmations: [true]}); item = t.item('Font.ttf', 'font'); await t.run([item]);
  assert.match(t.state.sent[0], /^\/fonts\?.*overwrite=1/);
  t = await setup({responses: [{status: 422, body: {error: '字体校验失败'}}, {status: 200, body: {ok: true}}]});
  item = t.item('Font.ttf', 'font'); await t.run([item]); assert.match(item.row.textContent, /校验失败/);
  await item.row.children.find(b => b.textContent === '重试此文件').onclick();
  assert(t.state.sent.every(u => u.startsWith('/fonts?'))); assert.match(item.row.textContent, /已保存/);
  t = await setup({responses: ['pending']}); const fonts = [t.item('One.ttf', 'font'), t.item('Two.ttf', 'font')];
  const fontPending = t.run(fonts); await flush(); t.nodes.get('cancel').onclick(); await fontPending;
  assert.equal(t.state.sent.length, 1); assert.match(fonts[1].row.textContent, /未上传/);
  // 批量继续发送独立文件；一键恢复只重发失败项，不重传已成功或已提交的文件。
  // Continue independent batch files; one-tap recovery only resends unsuccessful, never committed files.
  t = await setup({responses: [{status: 200, body: {ok: true}}, 'error', {status: 200, body: {ok: true}}]});
  const batch = [t.item('first.txt'), t.item('retry.epub'), t.item('third.txt')];
  await t.run(batch); assert.match(t.nodes.get('queueSummary').textContent, /已保存 2.*需处理 1/);
  assert.equal(t.nodes.get('retryFailed').hidden, false);
  await t.nodes.get('retryFailed').onclick();
  assert.equal(t.state.sent.length, 4); assert.match(t.state.sent[3], /name=retry.epub$/);
  assert.match(t.nodes.get('queueSummary').textContent, /已保存 3.*需处理 0/);
  assert.equal(t.nodes.get('retryFailed').hidden, true);
  assert.match(t.nodes.get('handoff').textContent, /完成，打开书架.*本地 TXT \/ EPUB 书架/);
  assert.equal(t.nodes.get('completion').hidden, false);
  t = await setup({responses: [{status: 500, body: {committed: true, progress_cleanup_failed: true, error: '文件已保存，但旧阅读进度清理失败'}}]});
  item = t.item(); await t.run([item]); assert.equal(t.nodes.get('retryFailed').hidden, true);
  await item.row.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.equal(t.state.sent.length, 1); assert.match(t.nodes.get('queueSummary').textContent, /已保存 1.*需处理 0/);
  // 浏览器发送100%并不等于设备提交；等待成功响应前不得显示已保存。
  // Browser upload100% is not a committed file; never mark saved before a success response.
  t = await setup({responses: ['pending']}); item = t.item('receiving.epub');
  const receiving = t.run([item]); await flush();
  t.state.xhrs[0].upload.onprogress({loaded: item.file.size, total: item.file.size});
  assert.match(t.nodes.get('progressText').textContent, /100%.*等待设备校验并保存/);
  assert.equal(t.nodes.get('completion').hidden, true);
  assert(t.nodes.get('saveWifi').disabled && t.nodes.get('forgetWifi').disabled && t.nodes.get('reconnect').disabled);
  const beforeConfig = t.state.requested.length;
  await vm.runInContext("configure('DELETE')", t.context); assert.equal(t.state.requested.length, beforeConfig);
  t.state.xhrs[0].status = 200; t.state.xhrs[0].responseText = '{"ok":true}'; t.state.xhrs[0].onload();
  await receiving; assert.equal(t.nodes.get('completion').hidden, false);
  assert.equal(t.state.payloads[0], item.file); // 原始文件体，保持既有接口。/ Keep the raw-file body contract.
  t = await setup({responses: ['invalid-json']}); item = t.item('uncertain.txt'); await t.run([item]);
  assert.match(item.row.textContent, /未确认保存结果/); assert.equal(t.nodes.get('completion').hidden, true);
  assert.equal(t.nodes.get('retryFailed').hidden, false);
  t = await setup({existing: true, confirmations: [false]}); item = t.item('keep.txt'); await t.run([item]);
  assert.match(t.state.confirmationMessages[0], /取消：保留原文件.*重命名/s);
  assert.equal(t.state.sent.length, 0); assert.equal(t.nodes.get('retryFailed').hidden, true);
  t = await setup(); item = t.item('unsupported.cbz'); await t.run([item]);
  assert.match(item.row.textContent, /仅支持 TXT \/ EPUB/); assert.equal(t.nodes.get('retryFailed').hidden, true);
  assert.equal(t.state.sent.length, 0);
  // 首次断网可原页重新连接，保留选中的文件；无需刷新丢失本地文件句柄。
  // Reconnect in place after initial failure, retaining selected files without a page reload.
  t = await setup({fetchHook: (url, options, state) => { if (url === '/info' && state.infoReads === 1) throw Error('offline'); }});
  assert.match(t.nodes.get('info').textContent, /连接中断.*重新连接/);
  const selected = [t.item('selected.txt').file]; t.nodes.get('files').files = selected;
  t.nodes.get('files').onchange(); assert(t.nodes.get('send').disabled);
  await t.nodes.get('reconnect').onclick(); assert.equal(t.nodes.get('files').files, selected);
  assert.match(t.nodes.get('selection').textContent, /已选择 1/); assert.equal(t.nodes.get('send').disabled, false);
  // 已取消时不再弹出稍后到达的同名确认，亦不发送请求体。
  // Cancellation suppresses a late duplicate prompt and prevents transmitting a body.
  let releaseLookup;
  t = await setup({existing: true, fetchHook: url => url.startsWith('/books?name=') ? new Promise(resolve => { releaseLookup = resolve; }) : undefined});
  item = t.item('late.txt'); const checking = t.run([item]); await flush();
  t.nodes.get('cancel').onclick(); releaseLookup(); await checking;
  assert.equal(t.state.confirmationMessages.length, 0); assert.equal(t.state.sent.length, 0);
  // 发送超时不会宣称成功，保留源文件供重试。/ A timeout retains the source and never reports success.
  t = await setup({responses:['pending']}); item=t.item('timeout.txt');
  const stalled=t.run([item]); await flush();
  assert(t.state.xhrs[0].timeout>=60000);t.state.xhrs[0].ontimeout();await stalled;
  assert.match(item.row.textContent,/超时/);assert.equal(item.retryable,true);
  assert.equal(t.nodes.get('completion').hidden,true);
  // 查询超时中止fetch并解锁，而非永久busy。/ Abort a stalled fetch and restore controls.
  t = await setup({fetchHook:(url,options)=>url.startsWith('/books?name=')?
    new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>reject(Error('aborted')))):undefined});
  item=t.item('lookup-timeout.txt');const waiting=t.run([item]);await flush();
  assert.equal(t.state.timers.size,1);[...t.state.timers.values()][0].f();await waiting;
  assert.match(item.row.textContent,/超时/);assert.equal(item.retryable,true);
  assert.equal(t.nodes.get('files').disabled,false);
  // 配网时锁住删除、搜索与上传，结束后统一解锁。/ Network provisioning holds all mutation controls.
  let releaseConfig;
  t=await setup({fetchHook:url=>url==='/wifi'?new Promise(resolve=>{releaseConfig=resolve;}):undefined});
  const config=vm.runInContext("configure('POST',{ssid:'test',password:'password'})",t.context);await flush();
  assert(t.nodes.get('search').disabled&&t.nodes.get('files').disabled);
  const count=t.state.requested.length;t.nodes.get('searchForm').onsubmit({preventDefault(){}});
  assert.equal(t.state.requested.length,count);releaseConfig();await config;
  assert.equal(t.nodes.get('search').disabled,false);
  console.log('transfer web interaction tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });

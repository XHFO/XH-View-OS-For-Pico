(() => {
  'use strict';

  const viewport = document.getElementById('viewport');
  const stage = document.getElementById('stage');
  const elementsLayer = document.getElementById('elements');
  const background = document.getElementById('background');
  const gridLayer = document.getElementById('grid');
  const screenList = document.getElementById('screenList');
  const saveState = document.getElementById('saveState');
  const overlapInfo = document.getElementById('overlapInfo');
  const inspector = document.getElementById('inspector');
  const emptyInspector = document.getElementById('emptyInspector');
  const exportModal = document.getElementById('exportModal');
  const exportText = document.getElementById('exportText');

  const state = {
    layouts: [],
    layout: null,
    selectedId: null,
    zoom: 100,
    scale: 1,
    dirty: false,
    saving: false,
    saveTimer: 0,
    undo: [],
    redo: [],
  };

  const prop = {
    id: document.getElementById('propId'),
    type: document.getElementById('propType'),
    label: document.getElementById('propLabel'),
    x: document.getElementById('propX'),
    y: document.getElementById('propY'),
    w: document.getElementById('propW'),
    h: document.getElementById('propH'),
    font: document.getElementById('propFont'),
    color: document.getElementById('propColor'),
  };

  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, Number.isFinite(+v) ? +v : 0));
  const snapValue = (v) => {
    if (!document.getElementById('gridToggle').checked) return v;
    const step = state.layout?.grid || 8;
    return Math.round(v / step) * step;
  };
  const clone = (value) => JSON.parse(JSON.stringify(value));
  const selected = () => state.layout?.elements.find((item) => item.id === state.selectedId) || null;
  const byId = (id) => state.layout?.elements.find((item) => item.id === id) || null;

  function setSave(text, kind = '') {
    saveState.textContent = text;
    saveState.className = 'save-state ' + kind;
  }

  function markDirty() {
    state.dirty = true;
    setSave('未保存', 'dirty');
    clearTimeout(state.saveTimer);
    state.saveTimer = window.setTimeout(saveLayout, 700);
  }

  async function api(url, options = {}) {
    const response = await fetch(url, options);
    if (!response.ok) {
      let message = response.statusText;
      try { message = (await response.json()).error || message; } catch (_) {}
      throw new Error(message);
    }
    const type = response.headers.get('content-type') || '';
    return type.includes('application/json') ? response.json() : response.text();
  }

  async function loadScreenList() {
    const data = await api('/api/layouts');
    state.layouts = data.items;
    renderScreenList();
  }

  function renderScreenList() {
    screenList.replaceChildren();
    for (const item of state.layouts) {
      const row = document.createElement('div');
      row.className = 'screen-item' + (state.layout?.id === item.id ? ' active' : '');
      const name = document.createElement('span');
      name.textContent = item.name || item.id;
      const count = document.createElement('span');
      count.className = 'count';
      count.textContent = String(item.count ?? '');
      row.append(name, count);
      row.addEventListener('click', () => openLayout(item.id));
      screenList.append(row);
    }
  }

  async function openLayout(id) {
    if (state.dirty) await saveLayout();
    setSave('加载中…');
    state.layout = await api('/api/layout/' + encodeURIComponent(id));
    state.layout.grid = state.layout.grid || 8;
    state.selectedId = null;
    state.undo = [];
    state.redo = [];
    state.dirty = false;
    background.src = state.layout.background || '';
    background.style.display = document.getElementById('bgToggle').checked && state.layout.background ? 'block' : 'none';
    renderScreenList();
    fitScale();
    render();
    setSave('已加载', 'saved');
  }

  function fitScale() {
    const width = Math.max(200, viewport.clientWidth - 44);
    const height = Math.max(160, viewport.clientHeight - 44);
    const base = Math.min(width / 640, height / 360, 1.35);
    const maxScale = Number(state.layout?.view?.w || 640) === 640 ? base : base;
    state.scale = Math.max(0.35, Math.min(maxScale, base)) * (state.zoom / 100);
  }

  function render() {
    if (!state.layout) return;
    const scale = state.scale;
    stage.style.width = (state.layout.view.w * scale) + 'px';
    stage.style.height = (state.layout.view.h * scale) + 'px';
    stage.style.setProperty('--grid', (state.layout.grid || 8) * scale + 'px');
    gridLayer.classList.toggle('hidden', !document.getElementById('gridToggle').checked);
    const showBackground = document.getElementById('bgToggle').checked && !!state.layout.background;
    background.style.display = showBackground ? 'block' : 'none';
    stage.classList.toggle('has-background', showBackground);
    elementsLayer.replaceChildren();
    const overlaps = new Set();
    const list = state.layout.elements;
    for (let i = 0; i < list.length; i++) {
      for (let j = i + 1; j < list.length; j++) {
        if (intersects(list[i], list[j])) {
          overlaps.add(list[i].id);
          overlaps.add(list[j].id);
        }
      }
    }
    for (const item of list) {
      const node = document.createElement('div');
      node.className = 'element type-' + (item.type || 'button') + (item.id === state.selectedId ? ' selected' : '') + (overlaps.has(item.id) ? ' overlap' : '');
      node.dataset.id = item.id;
      node.style.left = item.x * scale + 'px';
      node.style.top = item.y * scale + 'px';
      node.style.width = Math.max(4, item.w * scale) + 'px';
      node.style.height = Math.max(4, item.h * scale) + 'px';
      const label = document.createElement('div');
      label.className = 'label';
      label.textContent = item.label || item.id;
      if (item.font) label.style.fontSize = Math.max(9, Math.min(24, item.font * scale * 0.62)) + 'px';
      const handle = document.createElement('div');
      handle.className = 'handle';
      node.append(label, handle);
      node.addEventListener('pointerdown', beginDrag);
      handle.addEventListener('pointerdown', beginResize);
      elementsLayer.append(node);
    }
    const count = overlaps.size;
    overlapInfo.textContent = count ? `检测到 ${count} 个元素存在重叠` : '无重叠';
    overlapInfo.classList.toggle('bad', count > 0);
    document.getElementById('cursorInfo').textContent = `${state.layout.view.w} × ${state.layout.view.h}  ·  ${Math.round(scale * 100)}%`;
    renderInspector();
    updateUndoButtons();
  }

  function intersects(a, b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
  }

  function select(id) {
    state.selectedId = id;
    render();
  }

  function pushUndo() {
    if (!state.layout) return;
    state.undo.push(clone(state.layout));
    if (state.undo.length > 60) state.undo.shift();
    state.redo = [];
    updateUndoButtons();
  }

  function updateUndoButtons() {
    document.getElementById('undoBtn').disabled = state.undo.length === 0;
    document.getElementById('redoBtn').disabled = state.redo.length === 0;
  }

  function selectNoRender(id) {
    state.selectedId = id;
    for (const node of elementsLayer.querySelectorAll('.element')) {
      node.classList.toggle('selected', node.dataset.id === id);
    }
    renderInspector();
  }

  function beginDrag(event) {
    if (event.button !== 0) return;
    const id = event.currentTarget.dataset.id;
    selectNoRender(id);
    const item = byId(id);
    const scale = state.scale;
    const start = { x: event.clientX, y: event.clientY, ox: item.x, oy: item.y };
    pushUndo();
    const move = (ev) => {
      const dx = (ev.clientX - start.x) / scale;
      const dy = (ev.clientY - start.y) / scale;
      let x = start.ox + dx;
      let y = start.oy + dy;
      if (!ev.shiftKey) { x = snapValue(x); y = snapValue(y); }
      item.x = clamp(Math.round(x), 0, state.layout.view.w - item.w);
      item.y = clamp(Math.round(y), 0, state.layout.view.h - item.h);
      const node = elementsLayer.querySelector(`[data-id="${item.id}"]`);
      if (node) { node.style.left = item.x * scale + 'px'; node.style.top = item.y * scale + 'px'; }
      syncInspectorFields(item);
    };
    const end = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', end);
      window.removeEventListener('pointercancel', end);
      markDirty();
      render();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', end);
    window.addEventListener('pointercancel', end);
    event.preventDefault();
  }

  function beginResize(event) {
    event.stopPropagation();
    if (event.button !== 0) return;
    const id = event.currentTarget.parentElement.dataset.id;
    selectNoRender(id);
    const item = byId(id);
    const scale = state.scale;
    const start = { x: event.clientX, y: event.clientY, w: item.w, h: item.h };
    pushUndo();
    const move = (ev) => {
      let w = start.w + (ev.clientX - start.x) / scale;
      let h = start.h + (ev.clientY - start.y) / scale;
      if (!ev.shiftKey) { w = snapValue(w); h = snapValue(h); }
      item.w = clamp(Math.round(w), 8, state.layout.view.w - item.x);
      item.h = clamp(Math.round(h), 8, state.layout.view.h - item.y);
      const node = elementsLayer.querySelector(`[data-id="${item.id}"]`);
      if (node) { node.style.width = item.w * scale + 'px'; node.style.height = item.h * scale + 'px'; }
      syncInspectorFields(item);
    };
    const end = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', end);
      window.removeEventListener('pointercancel', end);
      markDirty();
      render();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', end);
    window.addEventListener('pointercancel', end);
    event.preventDefault();
  }

  function syncInspectorFields(item) {
    prop.x.value = item.x; prop.y.value = item.y; prop.w.value = item.w; prop.h.value = item.h;
    if (prop.font) prop.font.value = item.font ?? 12;
    if (prop.color) prop.color.value = item.color ?? 0;
  }

  function renderInspector() {
    const item = selected();
    if (!item) {
      inspector.hidden = true;
      emptyInspector.hidden = false;
      return;
    }
    inspector.hidden = false;
    emptyInspector.hidden = true;
    prop.id.value = item.id;
    prop.type.value = item.type || 'button';
    prop.label.value = item.label || '';
    syncInspectorFields(item);
  }

  function commitInspector() {
    const item = selected();
    if (!item) return;
    item.id = (prop.id.value || '').trim().replace(/[^a-zA-Z0-9_-]/g, '-') || item.id;
    item.type = prop.type.value;
    item.label = prop.label.value;
    item.x = clamp(prop.x.value, 0, state.layout.view.w - 8);
    item.y = clamp(prop.y.value, 0, state.layout.view.h - 8);
    item.w = clamp(prop.w.value, 8, state.layout.view.w - item.x);
    item.h = clamp(prop.h.value, 8, state.layout.view.h - item.y);
    item.font = clamp(prop.font.value, 8, 48);
    item.color = clamp(prop.color.value, 0, 15);
    state.selectedId = item.id;
    markDirty();
    render();
  }

  for (const input of [prop.id, prop.type, prop.label, prop.x, prop.y, prop.w, prop.h, prop.font, prop.color]) {
    input.addEventListener('change', commitInspector);
    if (input === prop.label) input.addEventListener('input', () => {
      const item = selected();
      if (item) { item.label = prop.label.value; render(); }
    });
  }

  async function saveLayout() {
    if (!state.layout || state.saving) return;
    clearTimeout(state.saveTimer);
    state.saving = true;
    setSave('保存中…');
    try {
      await api('/api/layout/' + encodeURIComponent(state.layout.id), {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(state.layout),
      });
      state.dirty = false;
      setSave('已保存', 'saved');
      const entry = state.layouts.find((item) => item.id === state.layout.id);
      if (entry) entry.count = state.layout.elements.length;
      renderScreenList();
    } catch (error) {
      setSave('保存失败', 'dirty');
      console.error(error);
    } finally {
      state.saving = false;
    }
  }

  function addElement() {
    if (!state.layout) return;
    pushUndo();
    let n = 1;
    while (byId('element-' + n)) n++;
    const item = { id: 'element-' + n, type: 'button', x: 24, y: 24, w: 120, h: 40, label: '新元素' };
    state.layout.elements.push(item);
    state.selectedId = item.id;
    markDirty();
    render();
  }

  function duplicateElement() {
    const item = selected();
    if (!item) return;
    pushUndo();
    let n = 2;
    while (byId(item.id + '-' + n)) n++;
    const copy = { ...clone(item), id: item.id + '-' + n, x: Math.min(state.layout.view.w - item.w, item.x + 12), y: Math.min(state.layout.view.h - item.h, item.y + 12) };
    state.layout.elements.push(copy);
    state.selectedId = copy.id;
    markDirty();
    render();
  }

  function deleteElement() {
    if (!state.layout || !state.selectedId) return;
    pushUndo();
    state.layout.elements = state.layout.elements.filter((item) => item.id !== state.selectedId);
    state.selectedId = null;
    markDirty();
    render();
  }

  function bringFront() {
    const item = selected();
    if (!item || !state.layout) return;
    pushUndo();
    state.layout.elements = state.layout.elements.filter((entry) => entry.id !== item.id);
    state.layout.elements.push(item);
    markDirty();
    render();
  }

  function undo() {
    if (!state.undo.length || !state.layout) return;
    state.redo.push(clone(state.layout));
    state.layout = state.undo.pop();
    state.selectedId = null;
    markDirty();
    render();
  }

  function redo() {
    if (!state.redo.length || !state.layout) return;
    state.undo.push(clone(state.layout));
    state.layout = state.redo.pop();
    state.selectedId = null;
    markDirty();
    render();
  }

  async function openExport() {
    if (!state.layout) return;
    if (state.dirty) await saveLayout();
    try {
      const code = await api('/api/export/' + encodeURIComponent(state.layout.id));
      exportText.value = code;
      exportModal.hidden = false;
      document.getElementById('copyState').textContent = '';
    } catch (error) {
      alert('导出失败：' + error.message);
    }
  }

  async function copyExport() {
    try {
      await navigator.clipboard.writeText(exportText.value);
      document.getElementById('copyState').textContent = '已复制';
    } catch (_) {
      exportText.select();
      document.execCommand('copy');
      document.getElementById('copyState').textContent = '已复制';
    }
  }

  document.getElementById('addBtn').addEventListener('click', addElement);
  document.getElementById('dupBtn').addEventListener('click', duplicateElement);
  document.getElementById('delBtn').addEventListener('click', deleteElement);
  document.getElementById('frontBtn').addEventListener('click', bringFront);
  document.getElementById('undoBtn').addEventListener('click', undo);
  document.getElementById('redoBtn').addEventListener('click', redo);
  document.getElementById('saveBtn').addEventListener('click', saveLayout);
  document.getElementById('exportBtn').addEventListener('click', openExport);
  document.getElementById('closeExport').addEventListener('click', () => { exportModal.hidden = true; });
  document.getElementById('copyBtn').addEventListener('click', copyExport);
  document.getElementById('bgToggle').addEventListener('change', render);
  document.getElementById('gridToggle').addEventListener('change', render);
  document.getElementById('zoom').addEventListener('input', (event) => {
    state.zoom = Number(event.target.value);
    document.getElementById('zoomValue').textContent = state.zoom + '%';
    fitScale();
    render();
  });

  window.addEventListener('resize', () => { fitScale(); render(); });
  document.addEventListener('keydown', (event) => {
    const tag = (event.target.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
    const meta = event.metaKey || event.ctrlKey;
    if (meta && event.key.toLowerCase() === 'z') {
      event.preventDefault();
      if (event.shiftKey) redo(); else undo();
      return;
    }
    if (meta && event.key.toLowerCase() === 'y') { event.preventDefault(); redo(); return; }
    if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); deleteElement(); return; }
    if (event.key.startsWith('Arrow')) {
      const item = selected();
      if (!item) return;
      event.preventDefault();
      const delta = event.shiftKey ? 10 : 1;
      if (!event.repeat) pushUndo();
      if (event.key === 'ArrowLeft') item.x = clamp(item.x - delta, 0, state.layout.view.w - item.w);
      if (event.key === 'ArrowRight') item.x = clamp(item.x + delta, 0, state.layout.view.w - item.w);
      if (event.key === 'ArrowUp') item.y = clamp(item.y - delta, 0, state.layout.view.h - item.h);
      if (event.key === 'ArrowDown') item.y = clamp(item.y + delta, 0, state.layout.view.h - item.h);
      markDirty();
      render();
    }
  });

  viewport.addEventListener('pointerdown', (event) => {
    if (event.target === viewport || event.target === stage || event.target === gridLayer) {
      state.selectedId = null;
      render();
    }
  });

  (async () => {
    try {
      await loadScreenList();
      if (state.layouts.length) await openLayout(state.layouts[0].id);
      else setSave('没有布局文件', 'dirty');
    } catch (error) {
      setSave('加载失败', 'dirty');
      console.error(error);
    }
  })();
})();

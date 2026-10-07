/* DOM-level behavior checks using a real C API fixture; no browser dependency. */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const snapshot = JSON.parse(fs.readFileSync('build/wifi-check/admin-test-dashboard.json', 'utf8'));
class Element {
  constructor(tag = 'div') {
    this.tag = tag; this.children = []; this.listeners = {}; this.attributes = {};
    this.value = ''; this.checked = false; this.hidden = false; this.disabled = false; this.dataset = {}; this.className = '';
    this.classList = {toggle: () => {}}; this.text = '';
  }
  set textContent(text) { this.text = String(text); this.children = []; }
  get textContent() { return this.text + this.children.map(child => typeof child === 'string' ? child : child.textContent).join(''); }
  append(...children) { this.children.push(...children.flatMap(child => child.tag === 'fragment' ? child.children : [child])); }
  replaceChildren(...children) { this.text = ''; this.children = []; this.append(...children); }
  get options() { return this.children; }
  cloneNode() { const copy = new Element(this.tag); copy.value = this.value; copy.textContent = this.textContent; return copy; }
  addEventListener(name, callback) { this.listeners[name] = callback; }
  setAttribute(name, value) { this.attributes[name] = value; }
}
const html = fs.readFileSync('admin/index.html', 'utf8'), elements = {};
for (const match of html.matchAll(/id="([^"]+)"/g)) elements[match[1]] = new Element();
for (const id of ['household-filter', 'tool-filter', 'state-filter']) {
  const option = new Element('option'); option.value = '0'; elements[id].append(option); elements[id].value = '0';
}
elements['auto-refresh'].checked = true;
const tabs = ['households', 'tools', 'loans'].map(tab => { const button = new Element('button'); button.dataset.tab = tab; return button; });
const requests = []; let fail = false, pending = null, delayed = false;
const sandbox = {
  console, Intl, URLSearchParams, AbortController, setTimeout, clearTimeout,
  setInterval: () => {},
  document: {
    hidden: false, getElementById: id => elements[id], createElement: tag => new Element(tag),
    createTextNode: text => String(text), createDocumentFragment: () => new Element('fragment'),
    querySelectorAll: () => tabs, addEventListener: () => {}
  },
  fetch: async url => {
    requests.push(url);
    if (delayed) { await new Promise(resolve => { pending = resolve; }); }
    return {ok: !fail, json: async () => fail ? {error: '시험 DB 연결 실패'} : structuredClone(snapshot)};
  }
};
vm.createContext(sandbox);
const run = code => vm.runInContext(code, sandbox);
const settle = async () => { for (let i = 0; i < 5; i++) await new Promise(resolve => setImmediate(resolve)); };
(async () => {
  vm.runInContext(fs.readFileSync('admin/dashboard.js', 'utf8'), sandbox);
  await settle();
  assert.equal(elements['metric-households'].textContent, snapshot.households.length + '세대');
  assert.equal(elements['metric-open'].textContent, snapshot.tools.filter(t => t.rental_state).length + '개');
  assert.equal(elements['policy-duration'].textContent, '5분');
  assert.equal(elements['policy-fee'].textContent, '500원');
  assert.equal(elements['policy-late'].textContent, '5분마다 500원');
  assert.equal(elements['test-badge'].textContent, '테스트 운영 · 5분');
  assert.match(elements['metric-amount'].textContent, /26,000원/);
  run("selectTab('tools')");
  assert.match(elements['table-body'].textContent, /대여 중|연체 중/);
  assert.match(elements['table-body'].textContent, /<script>/); // Database text stays a text node.
  elements.search.value = '존재하지 않는 공구'; run('renderTable()');
  assert.match(elements['table-body'].textContent, /기록이 없습니다/);
  run("viewHistory(3, 5)"); await settle();
  assert.match(requests.at(-1), /household=3&tool=5&state=0/);
  assert.match(elements['table-body'].textContent, /반납 완료/);
  assert.equal(elements.pagination.hidden, false);
  elements['state-filter'].value = '3'; elements['state-filter'].listeners.change(); await settle();
  assert.match(requests.at(-1), /state=3/);
  fail = true; await run('refresh()');
  assert.equal(elements.error.hidden, false);
  assert.match(elements.error.textContent, /마지막으로 조회한 데이터/);
  assert.match(elements.connection.textContent, /연결 끊김/);
  fail = false; await run('refresh()');
  assert.equal(elements.error.hidden, true);
  // A slow response for an old filter must not overwrite the latest filter's view.
  delayed = true; run('refresh()'); await settle();
  elements['household-filter'].value = '0'; elements['household-filter'].listeners.change();
  const release = pending; delayed = false; release(); await settle();
  assert.match(requests.at(-1), /household=0/);
  assert.equal(elements.refresh.disabled, false);
  console.log('PASS: dashboard metrics, 5-minute policy, safe text rendering, tabs, filters, empty state, stale-data warning, recovery and filter race');
})().catch(error => { console.error(error); process.exitCode = 1; });

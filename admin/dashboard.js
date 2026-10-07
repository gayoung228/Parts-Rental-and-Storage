'use strict';
const $ = id => document.getElementById(id);
const money = value => new Intl.NumberFormat('ko-KR').format(Number(value || 0)) + '원';
const number = value => new Intl.NumberFormat('ko-KR').format(Number(value || 0));
const titles = {
  households: ['세대별 대여 현황', '세대별 이용 현황과 청구 금액을 확인하세요.', '세대별 현황', '세대를 선택하면 대여 이력을 확인할 수 있습니다.'],
  tools: ['공구 대여 상태', '대여 가능한 공구와 현재 이용 세대를 확인하세요.', '공구별 상태', '공구별 대여 상태와 반납 예정 시각을 확인합니다.'],
  loans: ['대여·반납 이력', '세대와 공구별로 대여 내역과 발생한 요금을 확인하세요.', '대여 내역', '반납된 공구의 요금도 청구 예정 금액에 포함됩니다.']
};
let currentTab = 'households', data = null, busy = false, page = 1, viewGeneration = 0;
function cell(text, className = '') { const td = document.createElement('td'); td.className = className; td.textContent = text; return td; }
function labeled(primary, secondary = '') {
  const td = cell(''); const strong = document.createElement('span'); strong.className = 'primary-text'; strong.textContent = primary; td.append(strong);
  if (secondary) { const small = document.createElement('span'); small.className = 'secondary-text'; small.textContent = secondary; td.append(small); }
  return td;
}
function badge(text, kind) { const td = cell(''); const span = document.createElement('span'); span.className = 'badge ' + kind; span.textContent = text; td.append(span); return td; }
function timeText(seconds) { const n = Number(seconds); if (n < 60) return n + '초'; if (n < 3600) return number(Math.floor(n / 60)) + '분'; if (n < 86400) return number(Math.floor(n / 3600)) + '시간'; return number(Math.floor(n / 86400)) + '일'; }
function overdueText(seconds) { const n = Number(seconds); return n > 0 ? timeText(n) : '—'; }
function dateText(value) { return value ? value.slice(5, 16).replace(' ', ' · ') : '—'; }
function isOverdue(tool) { return tool.rental_state === 1 && tool.overdue_periods > 0; }
function viewHistory(household = 0, tool = 0) {
  $('household-filter').value = String(household); $('tool-filter').value = String(tool); $('state-filter').value = '0';
  selectTab('loans');
}
function historyCell(household = 0, tool = 0) { const td = cell(''); const b = document.createElement('button'); b.type = 'button'; b.className = 'link-button'; b.textContent = '이력 보기 →'; b.addEventListener('click', () => viewHistory(household, tool)); td.append(b); return td; }
function unitMetric(id, value, unit) { $(id).replaceChildren(document.createTextNode(number(value))); const span = document.createElement('span'); span.className = 'unit'; span.textContent = unit; $(id).append(span); }
function options(id, rows, label, key) {
  const select = $(id), previous = select.value, first = select.options[0].cloneNode(true);
  select.replaceChildren(first);
  for (const row of rows) { const option = document.createElement('option'); option.value = row[key]; option.textContent = label(row); select.append(option); }
  if ([...select.options].some(option => option.value === previous)) select.value = previous;
}
function renderSummary() {
  const houses = data.households, tools = data.tools, policy = data.policy[0];
  unitMetric('metric-households', houses.length, '세대');
  unitMetric('metric-open', tools.filter(t => t.rental_state === 1).length, '개');
  unitMetric('metric-overdue', tools.filter(isOverdue).length, '개');
  $('metric-amount').textContent = money(houses.reduce((sum, h) => sum + Number(h.amount_due_won), 0));
  $('metric-available').textContent = '대여 가능 ' + tools.filter(t => t.enabled && t.rental_state === 0).length + '개 / 전체 ' + tools.length + '개';
  if (policy) {
    const test = policy.duration_seconds === 300;
    $('test-badge').textContent = (test ? '테스트 운영 · ' : '대여 시간 · ') + timeText(policy.duration_seconds);
    $('policy-duration').textContent = timeText(policy.duration_seconds);
    $('policy-fee').textContent = money(policy.rental_fee_won);
    $('policy-late').textContent = timeText(policy.late_fee_period_seconds) + '마다 ' + money(policy.late_fee_won);
    $('policy-note').textContent = '반납 예정 시각을 넘으면 첫 연체료가 붙고, 이후 ' + timeText(policy.late_fee_period_seconds) + '를 넘길 때마다 추가됩니다.';
  } else {
    $('test-badge').textContent = '대여 정책 없음';
    $('policy-duration').textContent = $('policy-fee').textContent = $('policy-late').textContent = '미설정';
    $('policy-note').textContent = 'DB에 대여 정책이 없습니다. 관리자 서버의 적용 안내를 확인하세요.';
  }
  options('household-filter', houses, h => h.building_no + '동 ' + h.unit_no + '호', 'household_id');
  options('tool-filter', tools, t => t.tool_no + ' · ' + t.tool_name, 'id');
}
function head(labels) { const tr = document.createElement('tr'); for (const text of labels) { const th = document.createElement('th'); th.scope = 'col'; th.textContent = text; tr.append(th); } $('table-head').replaceChildren(tr); }
function emptyRow(columns, text) { const tr = document.createElement('tr'), td = cell(text, 'empty'); td.colSpan = columns; tr.append(td); return tr; }
function renderTable() {
  if (!data) return;
  const body = $('table-body'), fragment = document.createDocumentFragment();
  const search = $('search').value.trim().toLowerCase(), openOnly = $('open-only').checked;
  let rows, columns;
  if (currentTab === 'households') {
    head(['세대', '미반납 공구', '연체 시간 합계', '청구 예정 금액', '']); columns = 5;
    rows = data.households.filter(h => (!openOnly || h.unreturned_count > 0) && (h.building_no + '동 ' + h.unit_no + '호').toLowerCase().includes(search));
    for (const h of rows) {
      const tr = document.createElement('tr'); tr.append(labeled(h.building_no + '동 ' + h.unit_no + '호', '세대 ID ' + h.household_id), cell(number(h.unreturned_count) + '개'), cell(overdueText(h.overdue_seconds_total)), cell(money(h.amount_due_won), 'money-text'), historyCell(h.household_id)); fragment.append(tr);
    }
    $('table-note').textContent = '미반납 개수와 연체 시간은 현재 대여 중인 공구 기준입니다. 금액은 반납된 공구의 미청구 요금도 포함합니다.';
  } else if (currentTab === 'tools') {
    head(['공구', '대여 상태', '이용 세대', '반납 예정', '']); columns = 5;
    rows = data.tools.filter(t => (!openOnly || t.rental_state === 1) && (t.tool_no + ' ' + t.tool_name).toLowerCase().includes(search));
    for (const t of rows) {
      const tr = document.createElement('tr'); const late = isOverdue(t);
      tr.append(labeled(t.tool_name, '공구 번호 ' + t.tool_no), badge(t.rental_state ? (late ? '연체 중' : '대여 중') : (t.enabled ? '대여 가능' : '사용 중지'), late ? 'overdue' : t.rental_state ? 'open' : 'returned'), cell(t.household_id ? t.building_no + '동 ' + t.unit_no + '호' : '—'), cell(dateText(t.due_date), 'loan-date'), historyCell(0, t.id)); fragment.append(tr);
    }
    $('table-note').textContent = '상태 0은 미반납 대여 없음, 상태 1은 대여 중을 뜻합니다. 실물 공구의 위치와 보관함 문 상태는 별도로 확인하세요.';
  } else {
    head(['세대 / 공구', '상태', '대여 / 반납 예정', '반납 시각', '연체 시간', '대여료 / 연체료', '청구 예정']); columns = 7; rows = data.loans;
    for (const l of rows) {
      const tr = document.createElement('tr'); const late = l.is_returned === 0 && l.overdue_periods > 0;
      tr.append(labeled(l.building_no + '동 ' + l.unit_no + '호', l.tool_name + ' · #' + l.loan_id), badge(l.is_returned ? '반납 완료' : late ? '연체 중' : '대여 중', l.is_returned ? 'returned' : late ? 'overdue' : 'open'), labeled(dateText(l.rental_date), '예정 ' + dateText(l.due_date)), cell(dateText(l.return_date), 'loan-date'), cell(overdueText(l.overdue_seconds)), labeled(money(l.rental_fee_won), '연체료 ' + money(l.late_fee_won)), cell(money(l.amount_due_won), 'money-text')); fragment.append(tr);
    }
    const total = Number(data.count[0].total), last = Math.max(1, Math.ceil(total / data.page_size));
    $('page-info').textContent = number(total) + '건 · ' + page + ' / ' + last + '페이지';
    $('previous-page').disabled = page <= 1; $('next-page').disabled = page >= last;
    $('table-note').textContent = '최신 대여부터 20건씩 표시합니다. 반납 시 연체료 증가가 멈추며, 월별 청구로 이전된 금액은 청구 예정 금액에서 제외됩니다.';
  }
  if (!rows.length) fragment.append(emptyRow(columns, '해당 조건의 기록이 없습니다.'));
  body.replaceChildren(fragment);
  $('row-count').textContent = number(currentTab === 'loans' ? data.count[0].total : rows.length) + (currentTab === 'households' ? '세대' : currentTab === 'tools' ? '개' : '건');
}
function render() { renderSummary(); renderTable(); }
function selectTab(tab) {
  currentTab = tab; page = 1; viewGeneration++;
  for (const button of document.querySelectorAll('[data-tab]')) { const selected = button.dataset.tab === tab; button.classList.toggle('active', selected); button.setAttribute('aria-pressed', String(selected)); }
  const [title, description, tableTitle, subtitle] = titles[tab];
  $('page-title').textContent = title; $('page-description').textContent = description; $('table-title').textContent = tableTitle; $('table-subtitle').textContent = subtitle; $('breadcrumb').textContent = tableTitle;
  $('summary-filters').hidden = tab === 'loans'; $('loan-filters').hidden = tab !== 'loans'; $('pagination').hidden = tab !== 'loans';
  $('search').value = ''; $('open-only').checked = false; $('search').placeholder = tab === 'tools' ? '공구 이름 또는 번호 검색' : '동 또는 호수 검색';
  if (tab === 'loans') { $('table-body').replaceChildren(emptyRow(7, '대여 기록을 불러오고 있습니다.')); refresh(); }
  else renderTable();
}
function connection(text, kind = '') { $('connection').replaceChildren(); const dot = document.createElement('span'); dot.className = 'status-dot ' + kind; $('connection').append(dot, document.createTextNode(text)); }
async function refresh() {
  if (busy) return;
  busy = true; const generation = viewGeneration; $('refresh').disabled = true;
  const parameters = new URLSearchParams({page: String(page), household: $('household-filter').value, tool: $('tool-filter').value, state: $('state-filter').value});
  const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), 10000);
  try {
    const response = await fetch('/api/dashboard?' + parameters, {cache: 'no-store', signal: controller.signal});
    const snapshot = await response.json();
    if (!response.ok) throw new Error(snapshot.error || 'DB 정보를 조회하지 못했습니다.');
    if (generation !== viewGeneration) return;
    const totalPages = Math.max(1, Math.ceil(Number(snapshot.count[0].total) / snapshot.page_size));
    if (page > totalPages) { page = totalPages; viewGeneration++; return; }
    data = snapshot; $('error').hidden = true; render(); connection('DB 연결됨');
    $('updated').textContent = '최근 갱신 ' + data.clock[0].now.replace('T', ' ') + ' KST';
  } catch (error) {
    connection('조회 연결 끊김', 'failed'); $('error').hidden = false;
    $('error').textContent = (error.name === 'AbortError' ? '조회 시간이 초과됐습니다.' : error.message) + (data ? ' 현재 화면은 마지막으로 조회한 데이터입니다.' : ' 서버 실행 상태를 확인하고 다시 조회하세요.');
    if (!data) $('table-body').replaceChildren(emptyRow(5, 'DB에 연결하지 못했습니다. 새로고침으로 다시 시도하세요.'));
  } finally {
    clearTimeout(timeout); busy = false; $('refresh').disabled = false;
    if (generation !== viewGeneration) refresh();
  }
}
for (const b of document.querySelectorAll('[data-tab]')) b.addEventListener('click', () => selectTab(b.dataset.tab));
$('refresh').addEventListener('click', refresh);
$('search').addEventListener('input', renderTable); $('open-only').addEventListener('change', renderTable);
for (const id of ['household-filter', 'tool-filter', 'state-filter']) $(id).addEventListener('change', () => { page = 1; viewGeneration++; refresh(); });
$('reset-filters').addEventListener('click', () => { for (const id of ['household-filter', 'tool-filter', 'state-filter']) $(id).value = '0'; page = 1; viewGeneration++; refresh(); });
$('previous-page').addEventListener('click', () => { if (page > 1) { page--; viewGeneration++; refresh(); } });
$('next-page').addEventListener('click', () => { page++; viewGeneration++; refresh(); });
setInterval(() => { if ($('auto-refresh').checked && !document.hidden) refresh(); }, 5000);
document.addEventListener('visibilitychange', () => { if (!document.hidden && $('auto-refresh').checked) refresh(); });
refresh();

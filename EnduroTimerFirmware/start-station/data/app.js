console.log('EnduroTimer UI loaded');
const $ = (id) => document.getElementById(id);
let consecutiveFetchErrors = 0;
let riders = [];
let trails = [];
let settings = {};
let lastRunsRefreshMs = 0;
let lastCatalogRefreshMs = 0;

async function api(path, options = {}) {
  const response = await fetch(path, { cache: 'no-store', headers: { 'Content-Type': 'application/json' }, ...options });
  const text = await response.text();
  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (error) { throw new Error(`Invalid JSON from ${path}`); }
  if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}

function showMessage(text, isError = false, isWarning = false) {
  const element = $('message');
  element.textContent = text;
  element.className = `message ${isError ? 'error' : isWarning ? 'warning' : 'ok'}`;
}

function finishStateLabel(status) {
  if (status.finishHasError) return status.finishErrorMessage || 'ERROR';
  if (status.finishState === 'FinishSent') return 'Финиш отправлен / ждём ACK';
  return status.finishState || '—';
}

function signalText(rssi, snr) {
  return rssi === null || rssi === undefined ? 'NO SIGNAL' : `${Number(rssi).toFixed(0)} dBm${snr === null || snr === undefined ? '' : ` / ${Number(snr).toFixed(1)} dB`}`;
}

function renderStatus(status) {
  $('stateBadge').textContent = status.countdownText || status.state;
  $('stateBadge').className = `badge state-${String(status.state).toLowerCase()}`;
  $('serviceFlags').innerHTML = `OLED <span class="${status.oledOk ? 'online' : 'offline'}">${status.oledOk ? 'OK' : 'FAIL'}</span> · Wi-Fi <span class="${status.wifiOk ? 'online' : 'offline'}">${status.wifiOk ? 'OK' : 'FAIL'}</span> · Web <span class="${status.webOk ? 'online' : 'offline'}">${status.webOk ? 'OK' : 'FAIL'}</span> · LoRa <span class="${status.loraOk ? 'online' : 'offline'}">${status.loraOk ? 'OK' : 'FAIL'}</span>`;
  const lastSeen = Number(status.finishLastSeenAgoMs || 0);
  $('finishOnline').textContent = `Финиш: ${status.finishSignalText || (status.finishStationOnline ? signalText(status.finishRssi, null) : 'NO SIGNAL')}`;
  $('finishOnline').className = status.finishStationOnline ? 'online' : 'offline';
  $('finishState').textContent = `state: ${finishStateLabel(status)} · heartbeat: ${status.finishHeartbeatCount || 0} · last seen: ${lastSeen || '—'} ms`;
  $('finishState').className = status.finishHasError ? 'offline' : status.finishState === 'FinishSent' ? 'pending' : '';
  $('loraStats').textContent = `Signal from finish: ${signalText(status.finishRssi, status.finishSnr)}`;
  $('reverseLoraStats').textContent = `Старт по данным финиша: ${status.finishReportedStartSignalText || signalText(status.finishReportedStartRssi, status.finishReportedStartSnr)}${status.finishReportedStartLastSeenAgoMs ? ` (${status.finishReportedStartLastSeenAgoMs} ms ago)` : ''}`;
  $('lastPacket').textContent = `packet: ${status.lastLoRaPacketType || status.lastPacketType || '—'} · raw: ${status.lastLoRaRawShort || '—'}`;
  $('countdown').textContent = status.countdownText ? `Countdown: ${status.countdownText}` : 'Countdown: —';
  $('runTimer').textContent = status.currentRunElapsedFormatted || '00:00';
  $('lastResult').textContent = status.lastResultFormatted || '—';
  $('lastSource').textContent = status.lastFinishSource || '—';
  $('riderName').textContent = status.currentRiderName || 'Test Rider';
  $('trailName').textContent = status.currentTrailName || 'Default trail';
  $('selectedTrailName').textContent = status.selectedTrailName || status.currentTrailName || 'Default trail';
  $('runId').textContent = status.currentRunId || '—';
}

function renderRuns(runs) {
  const body = $('runsBody');
  $('runsError').textContent = '';
  if (!runs.length) { body.innerHTML = '<tr><td colspan="6">No runs yet</td></tr>'; return; }
  body.innerHTML = runs
    .filter((run) => run.status === 'Finished')
    .map((run) => `<tr><td><code>${run.runId}</code></td><td>${run.riderName}</td><td>${run.trailName || '—'}</td><td>${run.resultFormatted || '—'}</td><td>${run.status}</td><td>${run.source || run.finishSource || '—'}</td></tr>`)
    .join('') || '<tr><td colspan="6">No finished runs yet</td></tr>';
}

function renderCatalogs() {
  const activeRiders = riders.filter((r) => r.isActive);
  $('selectedRider').innerHTML = activeRiders.map((r) => `<option value="${r.riderId}" ${settings.selectedRiderId === r.riderId ? 'selected' : ''}>${r.displayName}</option>`).join('');
  $('ridersBody').innerHTML = riders.length ? riders.map((r) => `<tr><td>${r.displayName}</td><td>${r.isActive ? 'Yes' : 'No'}</td><td>${r.isActive ? `<button data-rider="${r.riderId}" class="secondary deactivate-rider">Deactivate</button>` : '—'}</td></tr>`).join('') : '<tr><td colspan="3">No riders</td></tr>';
  const activeTrails = trails.filter((t) => t.isActive);
  $('selectedTrail').innerHTML = activeTrails.map((t) => `<option value="${t.trailId}" ${settings.selectedTrailId === t.trailId ? 'selected' : ''}>${t.displayName}</option>`).join('');
  $('trailsBody').innerHTML = trails.length ? trails.map((t) => `<tr><td>${t.displayName}</td><td>${t.isActive ? 'Yes' : 'No'}</td><td>${t.isActive ? `<button data-trail="${t.trailId}" class="secondary deactivate-trail">Deactivate</button>` : '—'}</td></tr>`).join('') : '<tr><td colspan="3">No trails</td></tr>';
}

async function saveSettings() {
  await api('/api/settings', { method: 'POST', body: JSON.stringify({ selectedRiderId: $('selectedRider').value, selectedTrailId: $('selectedTrail').value }) });
  settings = await api('/api/settings');
  showMessage('Settings saved.');
}

async function refreshStatus() {
  try {
    const status = await api('/api/status');
    renderStatus(status);
    if (consecutiveFetchErrors > 0) showMessage('Web connection restored.');
    consecutiveFetchErrors = 0;
  } catch (error) {
    consecutiveFetchErrors += 1;
    if (consecutiveFetchErrors > 5) showMessage('Нет связи с верхним терминалом', true);
  }
}

async function refreshRuns() {
  try {
    renderRuns(await api('/api/runs'));
  } catch (error) {
    $('runsError').textContent = `Не удалось загрузить результаты: ${error.message}`;
  }
}

async function refreshCatalogs() {
  try {
    [riders, trails, settings] = await Promise.all([api('/api/riders'), api('/api/trails'), api('/api/settings')]);
    $('ridersError').textContent = '';
    $('trailsError').textContent = '';
    renderCatalogs();
  } catch (error) {
    $('ridersError').textContent = `Не удалось загрузить справочники: ${error.message}`;
  }
}

async function refresh() {
  await refreshStatus();
  const now = Date.now();
  if (now - lastRunsRefreshMs >= 2000) { lastRunsRefreshMs = now; refreshRuns(); }
  if (now - lastCatalogRefreshMs >= 10000) { lastCatalogRefreshMs = now; refreshCatalogs(); }
}

async function addRider() {
  const input = $('riderNameInput');
  const name = input.value.trim();
  $('ridersMessage').textContent = '';
  if (!name) { $('ridersMessage').textContent = 'Введите имя райдера'; $('ridersMessage').className = 'message error'; return; }
  console.log('Adding rider', name);
  try {
    const result = await api('/api/riders/add', { method: 'POST', body: JSON.stringify({ displayName: name }) });
    if (result.ok === false) throw new Error(result.error || 'Rider add failed');
    input.value = '';
    await refreshCatalogs();
    await refreshStatus();
    $('ridersMessage').textContent = 'Райдер добавлен';
    $('ridersMessage').className = 'message ok';
  } catch (error) {
    $('ridersMessage').textContent = `Ошибка добавления райдера: ${error.message}`;
    $('ridersMessage').className = 'message error';
    console.error('Rider add failed', error);
  }
}

async function addTrail() {
  const input = $('trailNameInput');
  const name = input.value.trim();
  $('trailsMessage').textContent = '';
  if (!name) { $('trailsMessage').textContent = 'Введите название трассы'; $('trailsMessage').className = 'message error'; return; }
  console.log('Adding trail', name);
  try {
    const result = await api('/api/trails/add', { method: 'POST', body: JSON.stringify({ displayName: name }) });
    if (result.ok === false) throw new Error(result.error || 'Trail add failed');
    input.value = '';
    await refreshCatalogs();
    settings = await api('/api/settings');
    $('trailsMessage').textContent = 'Трасса добавлена';
    $('trailsMessage').className = 'message ok';
  } catch (error) {
    $('trailsMessage').textContent = `Ошибка добавления трассы: ${error.message}`;
    $('trailsMessage').className = 'message error';
    console.error('Trail add failed', error);
  }
}

function bindUi() {
  $('resetBtn').addEventListener('click', async () => { try { await api('/api/system/reset', { method: 'POST' }); showMessage('Active run reset.'); refresh(); } catch (e) { showMessage(e.message, true); } });
  $('addRiderButton').addEventListener('click', addRider);
  $('addTrailButton').addEventListener('click', addTrail);
  $('selectedRider').addEventListener('change', saveSettings);
  $('selectedTrail').addEventListener('change', saveSettings);
  document.addEventListener('click', async (event) => {
    try {
      if (event.target.classList.contains('deactivate-rider')) { await api('/api/riders/deactivate', { method: 'POST', body: JSON.stringify({ riderId: event.target.dataset.rider }) }); await refreshCatalogs(); }
      if (event.target.classList.contains('deactivate-trail')) { await api('/api/trails/deactivate', { method: 'POST', body: JSON.stringify({ trailId: event.target.dataset.trail }) }); await refreshCatalogs(); }
    } catch (error) { showMessage(error.message, true); }
  });
}

document.addEventListener('DOMContentLoaded', () => {
  bindUi();
  refreshCatalogs();
  refreshRuns();
  refresh();
  setInterval(refresh, 1000);
  setInterval(refreshRuns, 2000);
});

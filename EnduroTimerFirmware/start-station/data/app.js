console.log("EnduroTimer UI loaded v0.17");
const $ = (id) => document.getElementById(id);
let consecutiveFetchErrors = 0;
let riders = [];
let trails = [];
let settings = {};
let timeSyncedOnce = false;
let addRiderInFlight = false;
let addTrailInFlight = false;
let writeInFlight = false;
let statusRefreshInFlight = false;
let runsRefreshInFlight = false;
let catalogsRefreshInFlight = false;

async function api(path, options = {}) {
  const controller = new AbortController();
  const method = String(options.method || 'GET').toUpperCase();
  const timeoutMs = method === 'GET' ? 3000 : 5000;
  const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(path, { cache: 'no-store', headers: { 'Content-Type': 'application/json' }, ...options, signal: controller.signal });
    const text = await response.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch (error) { throw new Error(`Invalid JSON from ${path}`); }
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    return data;
  } catch (error) {
    if (error.name === 'AbortError') throw new Error('Станция не ответила, повторите позже');
    throw error;
  } finally {
    clearTimeout(timeoutId);
  }
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

function ageText(ageMs) {
  return ageMs === null || ageMs === undefined || ageMs === 4294967295 ? '—' : `${ageMs} ms`;
}

function renderStatus(status) {
  $('stateBadge').textContent = status.countdownText || status.state;
  $('stateBadge').className = `badge state-${String(status.state).toLowerCase()}`;
  const finishFirmware = status.finishFirmwareVersion ? ` · Finish firmware: ${status.finishFirmwareVersion}` : '';
  $('firmwareVersions').textContent = `Start firmware: ${status.firmwareVersion || '—'}${finishFirmware}`;
  $('serviceFlags').innerHTML = `OLED <span class="${status.oledOk ? 'online' : 'offline'}">${status.oledOk ? 'OK' : 'FAIL'}</span> · Wi-Fi <span class="${status.wifiOk ? 'online' : 'offline'}">${status.wifiOk ? 'OK' : 'FAIL'}</span> · Web <span class="${status.webOk ? 'online' : 'offline'}">${status.webOk ? 'OK' : 'FAIL'}</span> · LoRa <span class="${status.loraOk ? 'online' : 'offline'}">${status.loraOk ? 'OK' : 'FAIL'}</span>`;
  const lastSeen = Number(status.finishLastSeenAgoMs || 0);
  const finishLinkActive = Boolean(status.finishLinkActive ?? status.finishStationOnline);
  const finishSignal = finishLinkActive ? (status.finishSignalText || signalText(status.finishRssi, status.finishSnr)) : 'NO SIGNAL';
  $('finishOnline').textContent = `Финишный терминал · Сигнал: ${finishSignal}`;
  $('finishOnline').className = finishLinkActive ? 'online' : 'offline';
  $('finishState').textContent = `Состояние: ${finishStateLabel(status)} · heartbeat: ${status.finishHeartbeatCount || 0} · последний пакет назад: ${ageText(lastSeen)}`;
  $('finishState').className = status.finishHasError ? 'offline' : status.finishState === 'FinishSent' ? 'pending' : '';
  $('loraStats').textContent = `Сигнал финиша: ${finishSignal} · age: ${ageText(lastSeen)} · пакетов: ${status.finishPacketCount || 0} · discovery: ${status.discoveryActive ? 'active' : 'inactive'}`;
  const reverseSignal = status.finishReportedStartLinkActive ? (status.finishReportedStartSignalText || signalText(status.finishReportedStartRssi, status.finishReportedStartSnr)) : 'NO SIGNAL';
  $('reverseLoraStats').textContent = `Сигнал старта по данным финиша: ${reverseSignal} · age: ${ageText(status.finishReportedStartLastSeenAgoMs)} · пакетов: ${status.finishReportedStartPacketCount || 0}`;
  $('lastPacket').textContent = `Последний пакет: ${status.finishLastPacketType || status.lastLoRaPacketType || status.lastPacketType || '—'} · последний пакет назад: ${ageText(lastSeen)}`;
  const finishLastPacketType = status.finishLastPacketType || status.lastLoRaPacketType || status.lastPacketType || '—';
  $('debugStartHb').textContent = status.startHeartbeatCount || 0;
  $('debugFinishHb').textContent = status.finishHeartbeatCount || 0;
  $('debugLastPacket').textContent = finishLastPacketType;
  $('debugFinishAge').textContent = ageText(lastSeen);
  $('debugFinishRssi').textContent = status.finishRssi === null || status.finishRssi === undefined ? '—' : `${Number(status.finishRssi).toFixed(0)} dBm`;
  $('debugFinishSnr').textContent = status.finishSnr === null || status.finishSnr === undefined ? '—' : `${Number(status.finishSnr).toFixed(1)} dB`;
  $('debugSignal').textContent = finishSignal;
  $('debugStartStatusAge').textContent = ageText(status.lastStartStatusSentAgoMs);
  $('debugDiscovery').textContent = status.discoveryActive ? `active · last HELLO ${ageText(status.lastDiscoverySentAgoMs)} ago` : 'inactive';
  $('debugFinishBootId').textContent = status.remoteBootId || status.finishBootId || '—';
  $('debugFinishRebootCount').textContent = status.remoteRebootCount || 0;
  $('debugFinishState').textContent = status.finishState || '—';
  $('debugReverseSignal').textContent = reverseSignal;
  $('debugReverseAge').textContent = ageText(status.finishReportedStartLastSeenAgoMs);
  const runStartAckStatus = status.runStartAckTimeout ? 'timeout' : status.runStartAckReceived ? 'OK' : status.pendingRunStartAck ? 'pending' : '—';
  if ($('debugRunStartAck')) $('debugRunStartAck').textContent = runStartAckStatus;
  if ($('debugRunStartAttempts')) $('debugRunStartAttempts').textContent = status.runStartAttempt ?? status.runStartAckAttempts ?? 0;
  if ($('debugFinishReportedState')) $('debugFinishReportedState').textContent = status.finishReportedState || status.finishState || '—';
  if ($('debugCurrentRunId')) $('debugCurrentRunId').textContent = status.currentRunId || '—';
  if ($('debugLoopMaxGap')) $('debugLoopMaxGap').textContent = status.loopMaxGapMs === undefined ? 'see /api/debug/status' : `${status.loopMaxGapMs} ms`;
  if ($('debugButtonLatency')) $('debugButtonLatency').textContent = status.startButtonLastLatencyMs === undefined ? 'see /api/debug/status' : `${status.startButtonLastLatencyMs} ms`;
  $('countdown').textContent = status.countdownText ? `Countdown: ${status.countdownText}` : 'Countdown: —';
  $('runTimer').textContent = status.currentRunElapsedFormatted || '00:00';
  $('lastResult').textContent = status.lastResultFormatted || '—';
  $('lastSource').textContent = status.lastFinishSource || '—';
  $('lastTiming').textContent = status.lastTimingSource ? `${status.lastTimingSource}: ${status.lastTimingNote || ''}` : '—';
  $('timeSynced').textContent = status.wallClockSynced ? 'да' : 'нет';
  $('stationTime').textContent = status.currentTimeText || '—';
  $('lastTimeSync').textContent = status.lastTimeSyncText ? `${status.lastTimeSyncText} (${status.timeSource || '—'})` : '—';
  if ($('uiLoaded')) $('uiLoaded').textContent = 'yes';
  if ($('finishWifiState')) $('finishWifiState').textContent = status.finishWifiConnected ? `connected ${status.finishIp || ''}` : 'not connected';
  if ($('finishWifiState')) $('finishWifiState').className = status.finishWifiConnected ? 'online' : 'offline';
  if ($('raceClockState')) $('raceClockState').textContent = status.finishRaceClockSynced ? 'SYNCED' : 'NOT SYNCED';
  if ($('raceClockState')) $('raceClockState').className = status.finishRaceClockSynced ? 'online' : 'offline';
  if ($('syncMode')) $('syncMode').textContent = status.raceClockSyncSource === 'WIFI_HTTP_ONCE' ? 'one-time at boot' : (status.raceClockSyncSource || '—');
  if ($('syncAccuracy')) $('syncAccuracy').textContent = status.finishSyncAccuracyMs === undefined ? '—' : `${status.finishSyncAccuracyMs} ms`;
  if ($('readyForRace')) $('readyForRace').textContent = status.systemReadyForRace ? 'yes' : 'no';
  if ($('readyForRace')) $('readyForRace').className = status.systemReadyForRace ? 'online' : 'offline';
  if ($('readyBlockReason')) $('readyBlockReason').textContent = status.readyBlockReason || '—';
  if ($('finishOffset')) $('finishOffset').textContent = status.finishRaceClockOffsetMs === undefined ? `${status.raceClockOffsetMs || 0} ms` : `${status.finishRaceClockOffsetMs} ms`;
  if ($('raceClockNow')) $('raceClockNow').textContent = status.raceClockNowMs || '—';
  if ($('lastSync')) $('lastSync').textContent = ageText(status.lastSyncAgoMs);
  if ($('remoteBootId')) $('remoteBootId').textContent = status.remoteBootId || status.finishBootId || '—';
  $('riderName').textContent = status.currentRiderName || 'Test Rider';
  $('trailName').textContent = status.currentTrailName || 'Default trail';
  $('selectedTrailName').textContent = status.selectedTrailName || status.currentTrailName || 'Default trail';
  $('runId').textContent = status.currentRunId || '—';
}

function renderRuns(runs) {
  const body = $('runsBody');
  $('runsError').textContent = '';
  if (!runs.length) { body.innerHTML = '<tr><td colspan="7">No runs yet</td></tr>'; return; }
  body.innerHTML = runs
    .filter((run) => run.status === 'Finished')
    .map((run) => `<tr title="runId: ${run.runId}"><td>${run.runNumber || '—'}</td><td>${run.startedAtText || run.runStartedAtText || 'TIME NOT SYNCED'}</td><td>${run.riderName}</td><td>${run.trailName || '—'}</td><td>${run.resultFormatted || '—'}</td><td>${run.status}</td><td>${run.source || run.finishSource || '—'}</td></tr>`)
    .join('') || '<tr><td colspan="7">No finished runs yet</td></tr>';
}

function renderCatalogs() {
  const activeRiders = riders.filter((r) => r.isActive);
  $('selectedRider').innerHTML = activeRiders.map((r) => `<option value="${r.riderId}" ${settings.selectedRiderId === r.riderId ? 'selected' : ''}>${r.displayName}</option>`).join('');
  $('ridersBody').innerHTML = riders.length ? riders.map((r) => `<tr><td>${r.displayName}</td><td>${r.isActive ? 'Yes' : 'No'}</td><td>${r.isActive ? `<button data-rider="${r.riderId}" class="secondary deactivate-rider">Deactivate</button>` : '—'}</td></tr>`).join('') : '<tr><td colspan="3">No riders</td></tr>';
  const activeTrails = trails.filter((t) => t.isActive);
  $('selectedTrail').innerHTML = activeTrails.map((t) => `<option value="${t.trailId}" ${settings.selectedTrailId === t.trailId ? 'selected' : ''}>${t.displayName}</option>`).join('');
  $('trailsBody').innerHTML = trails.length ? trails.map((t) => `<tr><td>${t.displayName}</td><td>${t.isActive ? 'Yes' : 'No'}</td><td>${t.isActive ? `<button data-trail="${t.trailId}" class="secondary deactivate-trail">Deactivate</button>` : '—'}</td></tr>`).join('') : '<tr><td colspan="3">No trails</td></tr>';
}

function localIsoText(date) {
  const pad = (value) => String(value).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

async function syncBrowserTime(showToast = false) {
  const now = new Date();
  await api('/api/time/sync', { method: 'POST', body: JSON.stringify({ epochMs: now.getTime(), timezoneOffsetMinutes: now.getTimezoneOffset(), isoLocal: localIsoText(now) }) });
  timeSyncedOnce = true;
  if (showToast) showMessage('Время синхронизировано.');
  await refreshStatus();
}

async function saveSettings() {
  await api('/api/settings', { method: 'POST', body: JSON.stringify({ selectedRiderId: $('selectedRider').value, selectedTrailId: $('selectedTrail').value }) });
  settings = await api('/api/settings');
  showMessage('Settings saved.');
}

async function refreshStatus() {
  if (writeInFlight || statusRefreshInFlight) return;
  statusRefreshInFlight = true;
  try {
    const status = await api('/api/status');
    renderStatus(status);
    if (consecutiveFetchErrors > 0) showMessage('Web connection restored.');
    consecutiveFetchErrors = 0;
  } catch (error) {
    consecutiveFetchErrors += 1;
    if (consecutiveFetchErrors > 5) showMessage('Нет связи с верхним терминалом', true);
  } finally {
    statusRefreshInFlight = false;
  }
}

async function refreshRuns() {
  if (writeInFlight || runsRefreshInFlight) return;
  runsRefreshInFlight = true;
  try {
    renderRuns(await api('/api/runs'));
  } catch (error) {
    $('runsError').textContent = `Не удалось загрузить результаты: ${error.message}`;
  } finally {
    runsRefreshInFlight = false;
  }
}

async function refreshCatalogs(force = false) {
  if ((!force && writeInFlight) || catalogsRefreshInFlight) return;
  catalogsRefreshInFlight = true;
  try {
    [riders, trails, settings] = await Promise.all([api('/api/riders'), api('/api/trails'), api('/api/settings')]);
    $('ridersError').textContent = '';
    $('trailsError').textContent = '';
    renderCatalogs();
  } catch (error) {
    $('ridersError').textContent = `Не удалось загрузить справочники: ${error.message}`;
  } finally {
    catalogsRefreshInFlight = false;
  }
}

async function refreshCatalogsOnce() {
  return refreshCatalogs(true);
}

async function refresh() {
  await refreshStatus();
}

async function addRider() {
  if (addRiderInFlight) {
    $('ridersMessage').textContent = 'Добавление райдера уже выполняется...';
    $('ridersMessage').className = 'message warning';
    return;
  }
  const input = $('riderNameInput');
  const button = $('addRiderButton');
  const name = input.value.trim();
  $('ridersMessage').textContent = '';
  if (!name) { $('ridersMessage').textContent = 'Введите имя райдера'; $('ridersMessage').className = 'message error'; return; }
  console.log('Adding rider', name);
  addRiderInFlight = true;
  writeInFlight = true;
  if (button) { button.disabled = true; button.textContent = 'Добавляю...'; }
  try {
    const result = await api('/api/riders/add', { method: 'POST', body: JSON.stringify({ displayName: name }) });
    if (result.ok === false) throw new Error(result.error || 'Rider add failed');
    input.value = '';
    if (result.rider) {
      const existingIndex = riders.findIndex((r) => r.riderId === result.rider.riderId || r.id === result.rider.riderId);
      const rider = { id: result.rider.riderId, riderId: result.rider.riderId, displayName: result.rider.displayName, isActive: result.rider.isActive };
      if (existingIndex >= 0) riders[existingIndex] = { ...riders[existingIndex], ...rider };
      else riders.push(rider);
      if (!settings.selectedRiderId) settings.selectedRiderId = result.rider.riderId;
      renderCatalogs();
    }
    await refreshCatalogsOnce();
    setTimeout(refreshStatus, 750);
    $('ridersMessage').textContent = 'Райдер добавлен';
    $('ridersMessage').className = 'message ok';
  } catch (error) {
    $('ridersMessage').textContent = `Ошибка добавления райдера: ${error.message}`;
    $('ridersMessage').className = 'message error';
    console.error('Rider add failed', error);
  } finally {
    addRiderInFlight = false;
    writeInFlight = false;
    if (button) { button.disabled = false; button.textContent = 'Добавить райдера'; }
  }
}

async function addTrail() {
  if (addTrailInFlight) {
    $('trailsMessage').textContent = 'Добавление трассы уже выполняется...';
    $('trailsMessage').className = 'message warning';
    return;
  }
  const input = $('trailNameInput');
  const button = $('addTrailButton');
  const name = input.value.trim();
  $('trailsMessage').textContent = '';
  if (!name) { $('trailsMessage').textContent = 'Введите название трассы'; $('trailsMessage').className = 'message error'; return; }
  console.log('Adding trail', name);
  addTrailInFlight = true;
  writeInFlight = true;
  if (button) { button.disabled = true; button.textContent = 'Добавляю...'; }
  try {
    const result = await api('/api/trails/add', { method: 'POST', body: JSON.stringify({ displayName: name }) });
    if (result.ok === false) throw new Error(result.error || 'Trail add failed');
    input.value = '';
    if (result.trail) {
      const existingIndex = trails.findIndex((t) => t.trailId === result.trail.trailId || t.id === result.trail.trailId);
      const trail = { id: result.trail.trailId, trailId: result.trail.trailId, displayName: result.trail.displayName, isActive: result.trail.isActive };
      if (existingIndex >= 0) trails[existingIndex] = { ...trails[existingIndex], ...trail };
      else trails.push(trail);
      if (!settings.selectedTrailId) settings.selectedTrailId = result.trail.trailId;
      renderCatalogs();
    }
    await refreshCatalogsOnce();
    setTimeout(refreshStatus, 750);
    $('trailsMessage').textContent = 'Трасса добавлена';
    $('trailsMessage').className = 'message ok';
  } catch (error) {
    $('trailsMessage').textContent = `Ошибка добавления трассы: ${error.message}`;
    $('trailsMessage').className = 'message error';
    console.error('Trail add failed', error);
  } finally {
    addTrailInFlight = false;
    writeInFlight = false;
    if (button) { button.disabled = false; button.textContent = 'Добавить трассу'; }
  }
}

function bindUi() {
  $('resetBtn').addEventListener('click', async () => { try { await api('/api/system/reset', { method: 'POST' }); showMessage('Active run reset.'); refresh(); } catch (e) { showMessage(e.message, true); } });
  const addRiderButton = $('addRiderButton');
  if (!addRiderButton) console.error('addRiderButton not found');
  else addRiderButton.addEventListener('click', addRider);
  const addTrailButton = $('addTrailButton');
  if (!addTrailButton) console.error('addTrailButton not found');
  else addTrailButton.addEventListener('click', addTrail);
  $('selectedRider').addEventListener('change', async () => { writeInFlight = true; try { await saveSettings(); await refreshCatalogsOnce(); } finally { writeInFlight = false; } });
  $('selectedTrail').addEventListener('change', async () => { writeInFlight = true; try { await saveSettings(); await refreshCatalogsOnce(); } finally { writeInFlight = false; } });
  $('syncTimeBtn').addEventListener('click', async () => { try { await syncBrowserTime(true); } catch (e) { showMessage(e.message, true); } });
  document.addEventListener('click', async (event) => {
    try {
      if (event.target.classList.contains('deactivate-rider')) { writeInFlight = true; await api('/api/riders/deactivate', { method: 'POST', body: JSON.stringify({ riderId: event.target.dataset.rider }) }); writeInFlight = false; await refreshCatalogsOnce(); }
      if (event.target.classList.contains('deactivate-trail')) { writeInFlight = true; await api('/api/trails/deactivate', { method: 'POST', body: JSON.stringify({ trailId: event.target.dataset.trail }) }); writeInFlight = false; await refreshCatalogsOnce(); }
    } catch (error) { writeInFlight = false; showMessage(error.message, true); }
  });
}

document.addEventListener('DOMContentLoaded', () => {
  bindUi();
  syncBrowserTime(false).catch((error) => showMessage(`Не удалось синхронизировать время: ${error.message}`, false, true));
  refreshCatalogsOnce();
  refreshRuns();
  refresh();
  setInterval(refreshStatus, 2000);
  setInterval(refreshRuns, 5000);
});

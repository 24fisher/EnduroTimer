const $ = (id) => document.getElementById(id);
let consecutiveFetchErrors = 0;
let riders = [];
let trails = [];
let settings = {};

async function api(path, options = {}) {
  const response = await fetch(path, { cache: 'no-store', headers: { 'Content-Type': 'application/json' }, ...options });
  const data = await response.json();
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
  return rssi === null || rssi === undefined ? 'No packets' : `${Number(rssi).toFixed(0)} dBm / ${Number(snr).toFixed(1)} dB`;
}

function renderStatus(status) {
  $('stateBadge').textContent = status.state;
  $('stateBadge').className = `badge state-${String(status.state).toLowerCase()}`;
  $('serviceFlags').innerHTML = `OLED <span class="${status.oledOk ? 'online' : 'offline'}">${status.oledOk ? 'OK' : 'FAIL'}</span> · Wi-Fi <span class="${status.wifiOk ? 'online' : 'offline'}">${status.wifiOk ? 'OK' : 'FAIL'}</span> · Web <span class="${status.webOk ? 'online' : 'offline'}">${status.webOk ? 'OK' : 'FAIL'}</span> · LoRa <span class="${status.loraOk ? 'online' : 'offline'}">${status.loraOk ? 'OK' : 'FAIL'}</span>`;
  const lastSeen = Number(status.finishLastSeenAgoMs || 0);
  $('finishOnline').textContent = status.finishStationOnline ? `Online, last seen ${lastSeen} ms ago` : 'Offline';
  $('finishOnline').className = status.finishStationOnline ? 'online' : 'offline';
  $('finishState').textContent = `state: ${finishStateLabel(status)} · heartbeat: ${status.finishHeartbeatCount || 0}`;
  $('finishState').className = status.finishHasError ? 'offline' : status.finishState === 'FinishSent' ? 'pending' : '';
  $('loraStats').textContent = signalText(status.finishRssi, status.finishSnr);
  $('reverseLoraStats').textContent = `reported Start RSSI/SNR: ${signalText(status.finishReportedStartRssi, status.finishReportedStartSnr)}`;
  $('countdown').textContent = status.countdownText ? `Countdown: ${status.countdownText}` : 'Countdown: —';
  $('runTimer').textContent = status.currentRunElapsedFormatted || '00:00';
  $('lastResult').textContent = status.lastResultFormatted || '—';
  $('lastSource').textContent = status.lastFinishSource || '—';
  $('riderName').textContent = status.currentRiderName || 'Test Rider';
  $('trailName').textContent = status.currentTrailName || 'Трасса по умолчанию';
  $('runId').textContent = status.currentRunId || '—';
}

function renderRuns(runs) {
  const body = $('runsBody');
  if (!runs.length) { body.innerHTML = '<tr><td colspan="6">No runs yet</td></tr>'; return; }
  body.innerHTML = runs.map((run) => `<tr><td><code>${run.runId}</code></td><td>${run.riderName}</td><td>${run.trailName || '—'}</td><td>${run.resultFormatted || '—'}</td><td>${run.status}</td><td>${run.finishSource || '—'}</td></tr>`).join('');
}

function renderCatalogs() {
  const activeRiders = riders.filter((r) => r.isActive);
  $('selectedRider').innerHTML = activeRiders.map((r) => `<option value="${r.riderId}" ${settings.selectedRiderId === r.riderId ? 'selected' : ''}>${r.displayName}</option>`).join('');
  $('ridersBody').innerHTML = riders.map((r) => `<tr><td>${r.displayName}</td><td>${r.isActive ? 'Yes' : 'No'}</td><td>${r.isActive ? `<button data-rider="${r.riderId}" class="secondary deactivate-rider">Deactivate</button>` : '—'}</td></tr>`).join('');
  const activeTrails = trails.filter((t) => t.isActive);
  $('selectedTrail').innerHTML = activeTrails.map((t) => `<option value="${t.trailId}" ${settings.selectedTrailId === t.trailId ? 'selected' : ''}>${t.displayName}</option>`).join('');
  $('trailsBody').innerHTML = trails.map((t) => `<tr><td>${t.displayName}</td><td>${t.isActive ? 'Yes' : 'No'}</td><td>${t.isActive ? `<button data-trail="${t.trailId}" class="secondary deactivate-trail">Deactivate</button>` : '—'}</td></tr>`).join('');
}

async function saveSettings() {
  await api('/api/settings', { method: 'POST', body: JSON.stringify({ selectedRiderId: $('selectedRider').value, selectedTrailId: $('selectedTrail').value }) });
  showMessage('Settings saved.');
}

async function refresh() {
  try {
    const [status, runs, ridersData, trailsData, settingsData] = await Promise.all([api('/api/status'), api('/api/runs'), api('/api/riders'), api('/api/trails'), api('/api/settings')]);
    riders = ridersData; trails = trailsData; settings = settingsData;
    renderStatus(status); renderRuns(runs); renderCatalogs();
    if (consecutiveFetchErrors > 0) showMessage('Web connection restored.');
    consecutiveFetchErrors = 0;
  } catch (error) {
    consecutiveFetchErrors += 1;
    if (consecutiveFetchErrors > 5) showMessage('Нет связи с верхним терминалом', true);
  }
}

$('resetBtn').addEventListener('click', async () => { try { await api('/api/system/reset', { method: 'POST' }); showMessage('Active run reset.'); refresh(); } catch (e) { showMessage(e.message, true); } });
$('addRiderBtn').addEventListener('click', async () => { try { await api('/api/riders/add', { method: 'POST', body: JSON.stringify({ displayName: $('riderInput').value }) }); $('riderInput').value = ''; refresh(); } catch (e) { showMessage(e.message, true); } });
$('addTrailBtn').addEventListener('click', async () => { try { await api('/api/trails/add', { method: 'POST', body: JSON.stringify({ displayName: $('trailInput').value }) }); $('trailInput').value = ''; refresh(); } catch (e) { showMessage(e.message, true); } });
$('selectedRider').addEventListener('change', saveSettings);
$('selectedTrail').addEventListener('change', saveSettings);
document.addEventListener('click', async (event) => {
  if (event.target.classList.contains('deactivate-rider')) { await api('/api/riders/deactivate', { method: 'POST', body: JSON.stringify({ riderId: event.target.dataset.rider }) }); refresh(); }
  if (event.target.classList.contains('deactivate-trail')) { await api('/api/trails/deactivate', { method: 'POST', body: JSON.stringify({ trailId: event.target.dataset.trail }) }); refresh(); }
});

refresh();
setInterval(refresh, 1000);

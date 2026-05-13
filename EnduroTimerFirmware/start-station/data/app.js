const $ = (id) => document.getElementById(id);

let consecutiveFetchErrors = 0;

async function api(path, options = {}) {
  const response = await fetch(path, { cache: 'no-store', ...options });
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

function renderStatus(status) {
  $('stateBadge').textContent = status.state;
  $('stateBadge').className = `badge state-${String(status.state).toLowerCase()}`;
  $('serviceFlags').innerHTML = `OLED <span class="${status.oledOk ? 'online' : 'offline'}">${status.oledOk ? 'OK' : 'FAIL'}</span> · Wi-Fi <span class="${status.wifiOk ? 'online' : 'offline'}">${status.wifiOk ? 'OK' : 'FAIL'}</span> · Web <span class="${status.webOk ? 'online' : 'offline'}">${status.webOk ? 'OK' : 'FAIL'}</span> · LoRa <span class="${status.loraOk ? 'online' : 'offline'}">${status.loraOk ? 'OK' : 'FAIL'}</span>`;

  const lastSeen = Number(status.finishLastSeenAgoMs || 0);
  $('finishOnline').textContent = status.finishStationOnline ? `Online, last seen ${lastSeen} ms ago` : 'Offline';
  $('finishOnline').className = status.finishStationOnline ? 'online' : 'offline';
  $('finishState').textContent = `state: ${finishStateLabel(status)} · heartbeat: ${status.finishHeartbeatCount || 0}`;
  $('finishState').className = status.finishHasError ? 'offline' : status.finishState === 'FinishSent' ? 'pending' : '';

  $('loraStats').textContent = status.loraLastRssi === null ? 'No packets' : `${status.loraLastRssi.toFixed(0)} dBm / ${status.loraLastSnr.toFixed(1)} dB`;
  $('countdown').textContent = status.countdownText || '—';
  $('lastResult').textContent = status.lastResultFormatted || '—';
  $('lastSource').textContent = status.lastFinishSource || 'Finish button on lower Heltec';
  $('riderName').textContent = status.currentRiderName || 'Test Rider';
  $('runId').textContent = status.currentRunId || '—';
  $('startBtn').disabled = status.state !== 'Ready';
}

function renderRuns(runs) {
  const body = $('runsBody');
  if (!runs.length) {
    body.innerHTML = '<tr><td colspan="4">No runs yet</td></tr>';
    return;
  }
  body.innerHTML = runs.map((run) => `
    <tr>
      <td><code>${run.runId}</code></td>
      <td>${run.riderName}</td>
      <td>${run.resultFormatted || '—'}</td>
      <td>${run.status}</td>
    </tr>`).join('');
}

async function refresh() {
  try {
    const status = await api('/api/status');
    renderStatus(status);
    const runs = await api('/api/runs');
    renderRuns(runs);
    if (consecutiveFetchErrors > 0) showMessage('Web connection restored.');
    consecutiveFetchErrors = 0;
  } catch (error) {
    consecutiveFetchErrors += 1;
    if (consecutiveFetchErrors <= 2) {
      showMessage('Web connection is unstable; retrying…', false, true);
    } else if (consecutiveFetchErrors > 5) {
      showMessage('No response from station.', true);
    }
  }
}

$('startBtn').addEventListener('click', async () => {
  try {
    await api('/api/runs/start', { method: 'POST' });
    showMessage('Countdown started. Press the finish button on the lower Heltec after RUN_START.');
    refresh();
  } catch (error) {
    showMessage(error.message, true);
  }
});

$('resetBtn').addEventListener('click', async () => {
  try {
    await api('/api/system/reset', { method: 'POST' });
    showMessage('Active run reset.');
    refresh();
  } catch (error) {
    showMessage(error.message, true);
  }
});

refresh();
setInterval(refresh, 1000);

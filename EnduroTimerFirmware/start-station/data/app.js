const $ = (id) => document.getElementById(id);

async function api(path, options = {}) {
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}

function showMessage(text, isError = false) {
  const element = $('message');
  element.textContent = text;
  element.className = `message ${isError ? 'error' : 'ok'}`;
}

function renderStatus(status) {
  $('stateBadge').textContent = status.state;
  $('stateBadge').className = `badge state-${String(status.state).toLowerCase()}`;
  $('finishOnline').textContent = status.finishStationOnline ? 'Online' : 'Offline';
  $('finishOnline').className = status.finishStationOnline ? 'online' : 'offline';
  $('finishState').textContent = `state: ${status.finishState || '—'}`;
  $('loraStats').textContent = status.loraLastRssi === null ? 'No packets' : `${status.loraLastRssi.toFixed(0)} dBm / ${status.loraLastSnr.toFixed(1)} dB`;
  $('countdown').textContent = status.countdownText || '—';
  $('lastResult').textContent = status.lastResultFormatted || '—';
  $('lastSource').textContent = status.lastFinishSource || 'Finish simulation: 20 seconds after RUN_START';
  $('riderName').textContent = status.currentRiderName || 'Test Rider';
  $('runId').textContent = status.currentRunId || '—';
  $('startBtn').disabled = status.state === 'Countdown' || status.state === 'Riding';
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
  } catch (error) {
    showMessage(error.message, true);
  }
}

$('startBtn').addEventListener('click', async () => {
  try {
    await api('/api/runs/start', { method: 'POST' });
    showMessage('Countdown started. FinishStation will simulate finish after 20 seconds.');
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
setInterval(refresh, 500);

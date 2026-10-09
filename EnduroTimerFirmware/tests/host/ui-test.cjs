const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const elements = new Map();
const element = id => {
  if (!elements.has(id)) elements.set(id, {classList: {contains: () => false}, addEventListener() {}});
  return elements.get(id);
};
let pending, requests = [];
const ctx = vm.createContext({console, setTimeout, clearTimeout, AbortController,
  document: {getElementById: element, addEventListener() {}},
  fetch: async (path, options) => {
    requests.push([path, options.method]);
    if (options.method === 'POST') return new Promise(resolve => pending = resolve);
    return {ok: true, text: async () => JSON.stringify({state:'Ready', canStartCountdown:true})};
  }});
vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), ctx);
const run = code => vm.runInContext(code, ctx);
(async () => {
  for (const state of ['Ready','Countdown','WaitingStartGate','Riding','Finished']) {
    run(`renderStatus({state:'${state}', waitingStartGate:${state==='WaitingStartGate'}, canCancelStart:${['Countdown','WaitingStartGate'].includes(state)}, canStartCountdown:${state==='Ready'}})`);
    assert.equal(element('cancelStartBtn').hidden, !['Countdown','WaitingStartGate'].includes(state));
  }
  run("renderStatus({state:'WaitingStartGate',waitingStartGate:true,canCancelStart:true})");
  assert.equal(element('startGateStatus').textContent, 'Ожидание стартовых ворот');
  const first = run('startAction(false)');
  await run('startAction(false)');
  assert.equal(requests.filter(x=>x[1]==='POST').length,1);
  assert.equal(requests[0][0], '/api/start');
  pending({ok:true,text:async()=>'{"ok":true,"state":"Countdown"}'}); await first;
  const cancel = run('startAction(true)');
  assert.equal(requests.at(-1)[0], '/api/start/cancel');
  pending({ok:false,status:409,text:async()=>'{"error":"Race already started; use DNF/cancel run flow instead"}'}); await cancel;
  assert.match(element('message').textContent,/Race already started/);
  console.log('PASS: UI state visibility, waiting text, duplicate POST guard, endpoints, HTTP 409');
})().catch(error=>{console.error(error);process.exitCode=1;});

using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;

namespace EnduroTimer.Core.Services;

public sealed class EnduroTimerSystem
{
    private readonly IRunRepository _runs;
    private readonly IRegisteredRiderRepository _riders;
    private readonly ITrailRepository _trails;
    private readonly ILedDisplayService _led;
    private readonly IRfidReaderService _rfid;
    private readonly ISystemSettingsRepository _settings;
    private readonly IGroupQueueRepository _queueRepository;
    private bool _queueLoaded;
    private readonly object _gate = new();
    private SystemOperationMode _operationMode = SystemOperationMode.ManualEncoderSelection;
    private Guid? _selectedRiderId;
    private string? _selectedRiderName;
    private readonly List<Guid> _groupQueue = new();
    private int _groupQueuePosition;
    private bool _groupQueueFinished;
    private bool _groupSessionStopped;
    private string? _rfidBlockedReason;
    private long _showFinishedResultUntilMs;
    private string? _finishedResultLedText;
    private readonly List<RunRecord> _activeRuns = new();
    private CancellationTokenSource? _queueSessionCts;
    private Guid? _currentStartingRunId;
    private Guid? _currentStartingRiderId;
    private string? _currentStartingRiderName;
    private Guid? _nextQueuedRiderId;
    private string? _nextQueuedRiderName;
    private string _groupCountdownText = string.Empty;
    private int _nextSequenceNumber = 1;
    private long _modeFlashUntilMs;
    private string? _modeFlashText;

    public EnduroTimerSystem(UpperStationService upper, LowerStationService lower, IRunRepository runs, IRegisteredRiderRepository riders, ITrailRepository trails, ILedDisplayService led, IRfidReaderService rfid, ISystemSettingsRepository settings, IGroupQueueRepository queueRepository)
    {
        Upper = upper; Lower = lower; _runs = runs; _riders = riders; _trails = trails; _led = led; _rfid = rfid; _settings = settings; _queueRepository = queueRepository;
        Upper.RunFinished += OnManualRunFinishedAsync;
        _rfid.TagRead += OnTagReadAsync;
    }

    public UpperStationService Upper { get; }
    public LowerStationService Lower { get; }
    public SystemOperationMode OperationMode { get { lock (_gate) return _operationMode; } }
    public Guid? SelectedRiderId { get { lock (_gate) return _selectedRiderId; } }
    public string? SelectedRiderName { get { lock (_gate) return _selectedRiderName; } }
    public bool QueueAutoStartEnabled { get { lock (_gate) return _queueSessionCts is not null; } }

    public async Task<SystemStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        await RefreshLedAsync(cancellationToken);
        var settings = await _settings.GetAsync(cancellationToken);
        var selectedTrail = await ResolveSelectedTrailAsync(settings, cancellationToken);
        var lastRun = Upper.LastRun ?? (await _runs.ListAsync(1, cancellationToken)).FirstOrDefault();
        var startBlockedReason = await GetStartBlockedReasonAsync(cancellationToken);
        var next = await GetNextRiderAsync(cancellationToken);
        var activeRuns = BuildActiveRunStatuses();
        var expected = activeRuns.FirstOrDefault();
        Guid? currentRunId; Guid? currentRiderId; string? currentRiderName; Guid? nextQueuedRiderId; string? nextQueuedRiderName; bool auto; string groupCountdown; GroupSessionState groupState;
        lock (_gate)
        {
            currentRunId = _currentStartingRunId;
            currentRiderId = _currentStartingRiderId;
            currentRiderName = _currentStartingRiderName;
            nextQueuedRiderId = _nextQueuedRiderId;
            nextQueuedRiderName = _nextQueuedRiderName;
            auto = _queueSessionCts is not null;
            groupCountdown = _groupCountdownText;
            groupState = ComputeGroupSessionStateLocked(activeRuns.Count);
        }

        return new SystemStatus
        {
            UpperState = OperationMode == SystemOperationMode.GroupQueue && auto ? (string.IsNullOrEmpty(groupCountdown) ? UpperStationState.Ready : UpperStationState.Countdown) : Upper.State,
            LowerState = OperationMode == SystemOperationMode.GroupQueue && activeRuns.Count > 0 ? LowerStationState.WaitFinish : Lower.State,
            CountdownText = OperationMode == SystemOperationMode.GroupQueue ? groupCountdown : Upper.CountdownText,
            IsCountdownActive = OperationMode == SystemOperationMode.GroupQueue ? !string.IsNullOrEmpty(groupCountdown) : Upper.IsCountdownActive,
            Upper = Upper.Diagnostics, Lower = Lower.Diagnostics, BeamClear = Lower.BeamClear, RtcOffsetMs = Upper.RtcOffsetMs,
            RtcOffsetWarning = Upper.RtcOffsetWarning, IsTimeSynchronized = Upper.IsTimeSynchronized, TimeSyncRequired = Upper.TimeSyncRequired,
            CanStartRun = startBlockedReason is null, StartBlockedReason = startBlockedReason, ActiveRun = Upper.ActiveRun, LastRun = lastRun,
            OperationMode = OperationMode, OperationModeDisplayName = OperationMode == SystemOperationMode.GroupQueue ? "Очередь группы" : "Выбор райдера", SelectedTrailId = selectedTrail.TrailId, SelectedTrailName = selectedTrail.DisplayName, SelectedRiderId = SelectedRiderId, SelectedRiderName = SelectedRiderName,
            NextRiderId = next?.RiderId, NextRiderName = next?.DisplayName, LedDisplayText = _led.GetText(),
            CurrentStartingRunId = currentRunId, CurrentStartingRiderId = currentRiderId, CurrentStartingRiderName = currentRiderName,
            NextQueuedRiderId = nextQueuedRiderId, NextQueuedRiderName = nextQueuedRiderName,
            ExpectedFinisherRunId = expected?.RunId, ExpectedFinisherRiderName = expected?.RiderName,
            GroupQueueCompleted = groupState == GroupSessionState.Completed, AllQueuedRidersStarted = groupState is GroupSessionState.AllStarted or GroupSessionState.Completed, GroupSessionState = groupState, ActiveRunsCount = activeRuns.Count, ActiveRuns = activeRuns,
            QueueAutoStartEnabled = auto, GroupStartIntervalSeconds = Math.Max(3, settings.GroupStartIntervalSeconds),
            GroupQueuePosition = Math.Min(_groupQueuePosition, _groupQueue.Count), GroupQueueLength = _groupQueue.Count
        };
    }

    public async Task SetModeAsync(SystemOperationMode mode, CancellationToken cancellationToken = default)
    {
        if (mode != SystemOperationMode.GroupQueue) StopGroupQueueSession();
        lock (_gate) { _operationMode = mode; _modeFlashText = mode == SystemOperationMode.GroupQueue ? "РЕЖИМ: ОЧЕРЕДЬ" : "РЕЖИМ: ВЫБОР"; _modeFlashUntilMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + 1500; }
        await RefreshLedAsync(cancellationToken);
    }

    public async Task EncoderMoveAsync(int delta, CancellationToken cancellationToken = default)
    {
        if (OperationMode != SystemOperationMode.ManualEncoderSelection) return;
        var active = (await _riders.ListAsync(false, cancellationToken)).OrderBy(r => r.DisplayName).ToList();
        if (active.Count == 0) { lock (_gate) { _selectedRiderId = null; _selectedRiderName = null; } await RefreshLedAsync(cancellationToken); return; }
        lock (_gate)
        {
            var index = Math.Max(0, active.FindIndex(r => r.RiderId == _selectedRiderId));
            if (_selectedRiderId is null) index = 0; else index = (index + delta + active.Count) % active.Count;
            _selectedRiderId = active[index].RiderId; _selectedRiderName = active[index].DisplayName; _rfidBlockedReason = null;
        }
        await RefreshLedAsync(cancellationToken);
    }

    public Task EncoderPressAsync(CancellationToken cancellationToken = default) => RefreshLedAsync(cancellationToken);

    public async Task<GroupQueueState> GetGroupQueueAsync(CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        var riders = await _riders.ListAsync(true, cancellationToken);
        Guid[] ids; int pos; lock (_gate) { ids = _groupQueue.ToArray(); pos = _groupQueuePosition; }
        var entries = ids.Select(id => riders.FirstOrDefault(r => r.RiderId == id)).Where(r => r is not null).Select(r => new GroupQueueEntry { RiderId = r!.RiderId, DisplayName = r.DisplayName }).ToList();
        var next = entries.Count == 0 || pos >= entries.Count ? null : entries[pos];
        return new GroupQueueState { GroupQueue = entries, GroupQueuePosition = pos, NextRiderId = next?.RiderId, NextRiderName = next?.DisplayName };
    }

    public async Task<GroupQueueState> SetGroupQueueAsync(IEnumerable<Guid> riderIds, CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        var active = await _riders.ListAsync(false, cancellationToken); var unique = riderIds.Distinct().ToList();
        if (unique.Any(id => active.All(r => r.RiderId != id))) throw new ArgumentException("Group queue can contain only active riders");
        lock (_gate) { _groupQueue.Clear(); _groupQueue.AddRange(unique); _groupQueuePosition = 0; _groupQueueFinished = false; _groupSessionStopped = false; }
        await PersistQueueAsync(cancellationToken);
        await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken);
    }

    public async Task<GroupQueueState> MoveGroupQueueNextAsync(CancellationToken cancellationToken = default) { await AdvanceQueueAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> ResetGroupQueueAsync(CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) { _groupQueuePosition = 0; _groupQueueFinished = false; _groupSessionStopped = false; } await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> RemoveGroupQueueAtAsync(int index, CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) { if (index >= 0 && index < _groupQueue.Count) _groupQueue.RemoveAt(index); if (_groupQueuePosition > _groupQueue.Count) _groupQueuePosition = _groupQueue.Count; } await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> MoveGroupQueueItemAsync(int index, int delta, CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) { var target = index + delta; if (index >= 0 && index < _groupQueue.Count && target >= 0 && target < _groupQueue.Count) { (_groupQueue[index], _groupQueue[target]) = (_groupQueue[target], _groupQueue[index]); if (_groupQueuePosition == index) _groupQueuePosition = target; else if (_groupQueuePosition == target) _groupQueuePosition = index; } } await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<RfidReadResult> SimulateRfidAsync(string tagId, CancellationToken cancellationToken = default) => await _rfid.SimulateReadAsync(tagId, cancellationToken);

    public async Task<RunRecord> StartRunAsync(string? fallbackRider, string? trailName, CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        var settings = await _settings.GetAsync(cancellationToken);
        var trail = await ResolveSelectedTrailAsync(settings, cancellationToken);
        trailName = trail.DisplayName;
        if (OperationMode == SystemOperationMode.GroupQueue)
        {
            await StartGroupQueueSessionAsync(trailName, cancellationToken);
            return await WaitForFirstGroupRunAsync(cancellationToken);
        }

        var reason = await GetStartBlockedReasonAsync(cancellationToken); if (reason is not null) throw new InvalidOperationException(reason);
        if (SelectedRiderId is { } id && SelectedRiderName is { } name) return await Upper.StartRunAsync(name, trailName, id, SystemOperationMode.ManualEncoderSelection, null, trail.TrailId, cancellationToken);
        if (!string.IsNullOrWhiteSpace(fallbackRider)) return await Upper.StartRunAsync(fallbackRider, trailName, null, SystemOperationMode.ManualEncoderSelection, null, trail.TrailId, cancellationToken);
        throw new InvalidOperationException("No rider selected");
    }

    public async Task StartGroupQueueSessionAsync(string? trailName = null, CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        if (OperationMode != SystemOperationMode.GroupQueue) throw new InvalidOperationException("GroupQueue mode is not active");
        var reason = await GetStartBlockedReasonAsync(cancellationToken); if (reason is not null) throw new InvalidOperationException(reason);
        var settings = await _settings.GetAsync(cancellationToken);
        var trail = await ResolveSelectedTrailAsync(settings, cancellationToken);
        trailName = trail.DisplayName;
        var intervalSeconds = Math.Max(3, settings.GroupStartIntervalSeconds);
        var cts = new CancellationTokenSource();
        lock (_gate)
        {
            if (_queueSessionCts is not null) throw new InvalidOperationException("Group queue session already running");
            _groupQueuePosition = 0;
            _groupQueueFinished = false;
            _groupSessionStopped = false;
            _queueSessionCts = cts;
        }
        await PersistQueueAsync(cancellationToken);
        _ = Task.Run(() => RunGroupQueueSessionAsync(trail.TrailId, trailName!, intervalSeconds, cts.Token), CancellationToken.None);
    }

    public async Task StopGroupQueueSessionAsync(CancellationToken cancellationToken = default)
    {
        StopGroupQueueSession();
        await RefreshLedAsync(cancellationToken);
    }

    public async Task<RunRecord> FinishNextRunAsync(CancellationToken cancellationToken = default)
    {
        if (OperationMode != SystemOperationMode.GroupQueue)
        {
            if (Lower.State != LowerStationState.WaitFinish || Upper.ActiveRun is null) throw new InvalidOperationException("Finish station is not waiting for a run");
            throw new InvalidOperationException("Manual finish is handled by the finish sensor");
        }

        RunRecord run;
        lock (_gate)
        {
            run = _activeRuns.OrderBy(r => r.SequenceNumber).ThenBy(r => r.StartTimestampMs).FirstOrDefault() ?? throw new InvalidOperationException("No active runs to finish");
            run.FinishTimestampMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            run.ResultMs = run.FinishTimestampMs.Value - run.StartTimestampMs;
            run.Status = RunStatus.Finished;
            _activeRuns.Remove(run);
            _finishedResultLedText = $"FIN {run.RiderName} {TimeFormatter.FormatResult(run.ResultMs)}";
            _showFinishedResultUntilMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + 2000;
        }

        await _runs.UpdateAsync(run, cancellationToken);
        await RefreshLedAsync(cancellationToken);
        _ = Task.Run(async () => { await Task.Delay(2000); await RefreshLedAsync(CancellationToken.None); });
        return run;
    }

    public async Task MarkDnfAsync(Guid runId, CancellationToken cancellationToken = default)
    {
        RunRecord? groupRun = null;
        lock (_gate)
        {
            groupRun = _activeRuns.FirstOrDefault(r => r.RunId == runId);
            if (groupRun is not null)
            {
                _activeRuns.Remove(groupRun);
                groupRun.Status = RunStatus.Dnf;
                groupRun.FinishTimestampMs = null;
                groupRun.ResultMs = null;
            }
        }

        if (groupRun is not null)
        {
            await _runs.UpdateAsync(groupRun, cancellationToken);
            await RefreshLedAsync(cancellationToken);
            return;
        }

        await Upper.MarkDnfAsync(runId, cancellationToken);
        await RefreshLedAsync(cancellationToken);
    }

    public async Task ResetAsync(CancellationToken cancellationToken = default)
    {
        StopGroupQueueSession();
        Lower.Reset(); await Upper.ResetAsync(cancellationToken); lock (_gate) { _selectedRiderId = null; _selectedRiderName = null; _groupQueuePosition = 0; _groupQueueFinished = false; _groupSessionStopped = false; _rfidBlockedReason = null; _activeRuns.Clear(); ClearStartingState(); } await RefreshLedAsync(cancellationToken);
    }

    private async Task RunGroupQueueSessionAsync(Guid trailId, string trailName, int intervalSeconds, CancellationToken token)
    {
        try
        {
            var first = true;
            while (!token.IsCancellationRequested)
            {
                if (!Lower.Diagnostics.Online || !Lower.BeamClear || !Upper.IsTimeSynchronized) break;
                var rider = await GetNextRiderAsync(token);
                if (rider is null) break;

                var nextAfterCurrent = await GetQueuedRiderAfterCurrentAsync(token);
                var run = new RunRecord
                {
                    RiderId = rider.RiderId,
                    Rider = rider.DisplayName,
                    OperationMode = SystemOperationMode.GroupQueue,
                    QueuePosition = GetCurrentQueuePosition(),
                    TrailId = trailId,
                    TrailNameSnapshot = string.IsNullOrWhiteSpace(trailName) ? RunRecord.DefaultTrailName : trailName.Trim(),
                    TrailName = string.IsNullOrWhiteSpace(trailName) ? RunRecord.DefaultTrailName : trailName.Trim(),
                    Status = RunStatus.Pending,
                    CreatedAtMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                };

                lock (_gate)
                {
                    _currentStartingRunId = run.RunId;
                    _currentStartingRiderId = rider.RiderId;
                    _currentStartingRiderName = rider.DisplayName;
                    _nextQueuedRiderId = nextAfterCurrent?.RiderId;
                    _nextQueuedRiderName = nextAfterCurrent?.DisplayName;
                    _groupCountdownText = string.Empty;
                }
                await _runs.AddAsync(run, token);
                await RefreshLedAsync(token);

                if (!first)
                {
                    await DelayWithCancellationAsync(TimeSpan.FromSeconds(Math.Max(0, intervalSeconds - 3)), token);
                }

                foreach (var cue in new[] { "3", "2", "1" })
                {
                    lock (_gate) _groupCountdownText = cue;
                    await RefreshLedAsync(token);
                    await DelayWithCancellationAsync(TimeSpan.FromSeconds(1), token);
                }

                lock (_gate)
                {
                    run.StartTimestampMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                    run.Status = RunStatus.Riding;
                    run.SequenceNumber = _nextSequenceNumber++;
                    _groupCountdownText = "GO";
                    _activeRuns.Add(run);
                }
                await _runs.UpdateAsync(run, token);
                await RefreshLedAsync(token);
                await DelayWithCancellationAsync(TimeSpan.FromMilliseconds(750), token);
                await AdvanceQueueAsync(token);
                first = false;
            }
        }
        catch (OperationCanceledException)
        {
            await MarkCountdownInterruptedAsync(CancellationToken.None);
        }
        finally
        {
            lock (_gate)
            {
                _queueSessionCts?.Dispose();
                _queueSessionCts = null;
                if (_groupQueuePosition >= _groupQueue.Count) _groupSessionStopped = false;
                ClearStartingState();
            }
            await RefreshLedAsync(CancellationToken.None);
        }
    }

    private async Task MarkCountdownInterruptedAsync(CancellationToken ct)
    {
        Guid? runId; lock (_gate) runId = _currentStartingRunId;
        if (runId is null) return;
        var run = await _runs.GetAsync(runId.Value, ct);
        if (run is null || run.Status == RunStatus.Riding) return;
        run.Status = RunStatus.Dnf;
        await _runs.UpdateAsync(run, ct);
    }

    private async Task DelayWithCancellationAsync(TimeSpan delay, CancellationToken token)
    {
        if (delay > TimeSpan.Zero) await Task.Delay(delay, token);
    }

    private async Task<RunRecord> WaitForFirstGroupRunAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            lock (_gate)
            {
                var run = _activeRuns.OrderBy(r => r.SequenceNumber).LastOrDefault();
                if (run is not null) return run;
                if (_queueSessionCts is null) break;
            }
            await Task.Delay(100, cancellationToken);
        }
        throw new InvalidOperationException("Group queue session stopped before a rider started");
    }

    private void StopGroupQueueSession()
    {
        CancellationTokenSource? cts;
        lock (_gate) { cts = _queueSessionCts; _queueSessionCts = null; _groupSessionStopped = true; }
        cts?.Cancel();
    }

    private async Task<string?> GetStartBlockedReasonAsync(CancellationToken ct)
    {
        if (OperationMode == SystemOperationMode.GroupQueue)
        {
            if (QueueAutoStartEnabled) return "Group queue session already running";
            if (!Lower.Diagnostics.Online) return "Finish station is offline";
            if (!Lower.BeamClear) return "Finish beam is blocked";
            if (!Upper.IsTimeSynchronized) return "Time synchronization required";
            var q = await GetGroupQueueAsync(ct); if (q.GroupQueue.Count == 0) return "Group queue is empty";
            return null;
        }

        if (Upper.State is UpperStationState.Countdown or UpperStationState.Riding) return "Run already active";
        if (!Lower.Diagnostics.Online) return "Finish station is offline";
        if (!Lower.BeamClear) return "Finish beam is blocked";
        if (!Upper.IsTimeSynchronized) return "Time synchronization required";
        if (Upper.State == UpperStationState.Error) return "Critical station error";
        if (!string.IsNullOrEmpty(_rfidBlockedReason)) return _rfidBlockedReason;
        if (!(await _riders.ListAsync(false, ct)).Any()) return "No active riders registered";
        if (SelectedRiderId is null) return "No rider selected";
        return null;
    }

    private async Task<RegisteredRider?> GetNextRiderAsync(CancellationToken ct)
    {
        await EnsureQueueLoadedAsync(ct);
        Guid? id = null; lock (_gate) { if (_groupQueuePosition < _groupQueue.Count) id = _groupQueue[_groupQueuePosition]; }
        return id is null ? null : await _riders.GetAsync(id.Value, ct);
    }

    private async Task<RegisteredRider?> GetQueuedRiderAfterCurrentAsync(CancellationToken ct)
    {
        await EnsureQueueLoadedAsync(ct);
        Guid? id = null;
        lock (_gate)
        {
            if (_groupQueue.Count == 0) return null;
            var nextIndex = _groupQueuePosition + 1;
            if (nextIndex >= _groupQueue.Count)
            {
                return null;
            }
            id = _groupQueue[nextIndex];
        }
        return id is null ? null : await _riders.GetAsync(id.Value, ct);
    }

    private int GetCurrentQueuePosition() { lock (_gate) return _groupQueuePosition; }

    private IReadOnlyList<ActiveRunStatus> BuildActiveRunStatuses()
    {
        RunRecord[] runs; lock (_gate) runs = _activeRuns.OrderBy(r => r.SequenceNumber).ThenBy(r => r.StartTimestampMs).ToArray();
        var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        return runs.Select(r => new ActiveRunStatus { RunId = r.RunId, RiderId = r.RiderId, RiderName = r.RiderName, TrailId = r.TrailId, TrailName = (!string.IsNullOrWhiteSpace(r.TrailNameSnapshot) && (r.TrailNameSnapshot != RunRecord.DefaultTrailName || string.IsNullOrWhiteSpace(r.TrailName) || r.TrailName == RunRecord.DefaultTrailName)) ? r.TrailNameSnapshot : r.TrailName, StartedAtMs = r.StartTimestampMs, ElapsedFormatted = TimeFormatter.FormatResult(now - r.StartTimestampMs), SequenceNumber = r.SequenceNumber }).ToList();
    }

    private async Task RefreshLedAsync(CancellationToken ct)
    {
        if (!Upper.IsTimeSynchronized) { _led.SetText("SYNC TIME"); return; }
        if (!Lower.Diagnostics.Online) { _led.SetText("ФИНИШ OFFLINE"); return; }
        if (!Lower.BeamClear) { _led.SetText("ЛУЧ ПЕРЕКРЫТ"); return; }

        string? modeFlash; long modeFlashUntil;
        lock (_gate) { modeFlash = _modeFlashText; modeFlashUntil = _modeFlashUntilMs; }
        if (!string.IsNullOrEmpty(modeFlash) && DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() < modeFlashUntil) { _led.SetText(modeFlash); return; }

        if (OperationMode == SystemOperationMode.GroupQueue)
        {
            string? finished; long until; string countdown; string? current; string? next; GroupSessionState state; string? expected;
            int activeRunsCount;
            lock (_gate)
            {
                finished = _finishedResultLedText;
                until = _showFinishedResultUntilMs;
                countdown = _groupCountdownText;
                current = _currentStartingRiderName;
                next = _nextQueuedRiderName;
                activeRunsCount = _activeRuns.Count;
                state = ComputeGroupSessionStateLocked(activeRunsCount);
                expected = _activeRuns.OrderBy(r => r.SequenceNumber).ThenBy(r => r.StartTimestampMs).FirstOrDefault()?.RiderName;
            }
            if (!string.IsNullOrEmpty(countdown) && !string.IsNullOrWhiteSpace(current)) { _led.SetText($"{countdown} {current}"); return; }
            if (!string.IsNullOrEmpty(finished) && DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() < until) { _led.SetText(finished); return; }
            if (state == GroupSessionState.AllStarted) { _led.SetText(!string.IsNullOrWhiteSpace(expected) ? $"ЖДЁМ ФИНИШ: {expected}" : "ВСЕ СТАРТОВАЛИ"); return; }
            if (state == GroupSessionState.Completed) { _led.SetText("ОЧЕРЕДЬ ГОТОВА"); return; }
            var q = await GetGroupQueueAsync(ct);
            if (q.GroupQueue.Count == 0) { _led.SetText("ОЧЕРЕДЬ ПУСТА"); return; }
            var nowName = current ?? q.NextRiderName;
            var nextName = next ?? (await GetQueuedRiderAfterCurrentAsync(ct))?.DisplayName;
            _led.SetText(nowName is null ? "ОЧЕРЕДЬ ПУСТА" : $"NOW {nowName} | NEXT {nextName ?? "-"}");
            return;
        }

        if (!string.IsNullOrEmpty(Upper.CountdownText)) { _led.SetText(string.IsNullOrWhiteSpace(SelectedRiderName) ? Upper.CountdownText : $"{Upper.CountdownText} {SelectedRiderName}"); return; }
        if (Upper.LastRun?.Status == RunStatus.Finished && Upper.LastRun.ResultMs is not null && DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() < _showFinishedResultUntilMs) { _led.SetText($"FIN {Upper.LastRun.RiderName} {TimeFormatter.FormatResult(Upper.LastRun.ResultMs)}"); return; }
        if (Upper.ActiveRun is { Status: RunStatus.Riding } active) { _led.SetText(string.IsNullOrWhiteSpace(active.RiderName) ? "НА ТРАССЕ" : $"{active.RiderName} on course"); return; }
        if (!(await _riders.ListAsync(false, ct)).Any()) { _led.SetText("НЕТ РАЙДЕРОВ"); return; }
        _led.SetText(string.IsNullOrWhiteSpace(SelectedRiderName) ? "ВЫБЕРИ РАЙДЕРА" : $"ВЫБОР: {SelectedRiderName}");
    }

    private GroupSessionState ComputeGroupSessionStateLocked(int activeRunsCount)
    {
        if (_operationMode != SystemOperationMode.GroupQueue) return GroupSessionState.Idle;
        if (_queueSessionCts is not null) return GroupSessionState.Starting;
        if (_groupQueue.Count == 0) return GroupSessionState.Idle;
        if (_groupSessionStopped && _groupQueuePosition < _groupQueue.Count) return GroupSessionState.Stopped;
        if (_groupQueuePosition >= _groupQueue.Count && activeRunsCount > 0) return GroupSessionState.AllStarted;
        if (_groupQueuePosition >= _groupQueue.Count && activeRunsCount == 0) return GroupSessionState.Completed;
        return GroupSessionState.Idle;
    }

    private async Task<Trail> ResolveSelectedTrailAsync(SystemSettings settings, CancellationToken ct)
    {
        var defaultTrail = await _trails.EnsureDefaultAsync(settings.TrailName, ct);
        Trail? selected = null;
        if (settings.SelectedTrailId is { } id)
        {
            selected = await _trails.GetAsync(id, ct);
        }
        if (selected is null || !selected.IsActive)
        {
            selected = (await _trails.ListAsync(false, ct)).FirstOrDefault() ?? defaultTrail;
        }
        if (settings.SelectedTrailId != selected.TrailId || !string.IsNullOrWhiteSpace(settings.TrailName))
        {
            settings.SelectedTrailId = selected.TrailId;
            settings.TrailName = null;
            await _settings.SaveAsync(settings, ct);
        }
        return selected;
    }

    private async Task EnsureQueueLoadedAsync(CancellationToken ct)
    {
        if (_queueLoaded) return;
        var persisted = await _queueRepository.GetAsync(ct);
        lock (_gate)
        {
            if (_queueLoaded) return;
            _groupQueue.Clear();
            _groupQueue.AddRange(persisted.RiderIds);
            _groupQueuePosition = persisted.Position;
            _groupQueueFinished = _groupQueue.Count > 0 && _groupQueuePosition >= _groupQueue.Count;
            _queueLoaded = true;
        }
    }

    private Task PersistQueueAsync(CancellationToken ct)
    {
        Guid[] ids; int position;
        lock (_gate) { ids = _groupQueue.ToArray(); position = _groupQueuePosition; }
        return _queueRepository.SaveAsync(new PersistedGroupQueue { RiderIds = ids.ToList(), Position = position }, ct);
    }

    private async Task OnManualRunFinishedAsync(RunRecord run, CancellationToken ct) { _showFinishedResultUntilMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + 3000; await RefreshLedAsync(ct); _ = Task.Run(async () => { await Task.Delay(3000); Upper.ReadyAfterFinish(); await RefreshLedAsync(CancellationToken.None); }); }
    private async Task AdvanceQueueAsync(CancellationToken ct) { await EnsureQueueLoadedAsync(ct); lock (_gate) { if (_groupQueue.Count == 0) return; _groupQueuePosition++; if (_groupQueuePosition >= _groupQueue.Count) _groupQueueFinished = true; } await PersistQueueAsync(ct); await RefreshLedAsync(ct); }
    private void ClearStartingState() { _currentStartingRunId = null; _currentStartingRiderId = null; _currentStartingRiderName = null; _nextQueuedRiderId = null; _nextQueuedRiderName = null; _groupCountdownText = string.Empty; }
    private async Task OnTagReadAsync(RfidReadResult read, CancellationToken ct)
    {
        var rider = await _riders.GetByRfidTagAsync(read.TagId, ct);
        if (OperationMode == SystemOperationMode.ManualEncoderSelection)
        {
            lock (_gate) { _selectedRiderId = rider?.RiderId; _selectedRiderName = rider?.DisplayName; _rfidBlockedReason = rider is null ? "Unknown RFID tag" : null; }
            await RefreshLedAsync(ct);
        }
    }
}

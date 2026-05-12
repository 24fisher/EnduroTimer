using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;

namespace EnduroTimer.Core.Services;

public sealed class EnduroTimerSystem
{
    private readonly IRunRepository _runs;
    private readonly IRegisteredRiderRepository _riders;
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
    private bool _loopGroupQueue = true;
    private string? _rfidBlockedReason;
    private long _showFinishedResultUntilMs;

    public EnduroTimerSystem(UpperStationService upper, LowerStationService lower, IRunRepository runs, IRegisteredRiderRepository riders, ILedDisplayService led, IRfidReaderService rfid, ISystemSettingsRepository settings, IGroupQueueRepository queueRepository)
    {
        Upper = upper; Lower = lower; _runs = runs; _riders = riders; _led = led; _rfid = rfid; _settings = settings; _queueRepository = queueRepository;
        Upper.RunFinished += OnRunFinishedAsync;
        _rfid.TagRead += OnTagReadAsync;
    }

    public UpperStationService Upper { get; }
    public LowerStationService Lower { get; }
    public SystemOperationMode OperationMode { get { lock (_gate) return _operationMode; } }
    public Guid? SelectedRiderId { get { lock (_gate) return _selectedRiderId; } }
    public string? SelectedRiderName { get { lock (_gate) return _selectedRiderName; } }

    public async Task<SystemStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        await RefreshLedAsync(cancellationToken);
        var lastRun = Upper.LastRun ?? (await _runs.ListAsync(1, cancellationToken)).FirstOrDefault();
        var startBlockedReason = await GetStartBlockedReasonAsync(cancellationToken);
        var next = await GetNextRiderAsync(cancellationToken);
        return new SystemStatus
        {
            UpperState = Upper.State, LowerState = Lower.State, CountdownText = Upper.CountdownText, IsCountdownActive = Upper.IsCountdownActive,
            Upper = Upper.Diagnostics, Lower = Lower.Diagnostics, BeamClear = Lower.BeamClear, RtcOffsetMs = Upper.RtcOffsetMs,
            RtcOffsetWarning = Upper.RtcOffsetWarning, IsTimeSynchronized = Upper.IsTimeSynchronized, TimeSyncRequired = Upper.TimeSyncRequired,
            CanStartRun = startBlockedReason is null, StartBlockedReason = startBlockedReason, ActiveRun = Upper.ActiveRun, LastRun = lastRun,
            OperationMode = OperationMode, SelectedRiderId = SelectedRiderId, SelectedRiderName = SelectedRiderName,
            NextRiderId = next?.RiderId, NextRiderName = next?.DisplayName, LedDisplayText = _led.GetText(),
            GroupQueuePosition = _groupQueuePosition, GroupQueueLength = _groupQueue.Count
        };
    }

    public async Task SetModeAsync(SystemOperationMode mode, CancellationToken cancellationToken = default)
    {
        lock (_gate) _operationMode = mode;
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
        Guid[] ids; int pos; bool loop; lock (_gate) { ids = _groupQueue.ToArray(); pos = _groupQueuePosition; loop = _loopGroupQueue; }
        var entries = ids.Select(id => riders.FirstOrDefault(r => r.RiderId == id)).Where(r => r is not null).Select(r => new GroupQueueEntry { RiderId = r!.RiderId, DisplayName = r.DisplayName }).ToList();
        var next = entries.Count == 0 || pos >= entries.Count ? null : entries[pos];
        return new GroupQueueState { GroupQueue = entries, GroupQueuePosition = pos, LoopGroupQueue = loop, NextRiderId = next?.RiderId, NextRiderName = next?.DisplayName };
    }

    public async Task<GroupQueueState> SetGroupQueueAsync(IEnumerable<Guid> riderIds, bool loopGroupQueue, CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        var active = await _riders.ListAsync(false, cancellationToken); var unique = riderIds.Distinct().ToList();
        if (unique.Any(id => active.All(r => r.RiderId != id))) throw new ArgumentException("Group queue can contain only active riders");
        lock (_gate) { _groupQueue.Clear(); _groupQueue.AddRange(unique); _groupQueuePosition = 0; _loopGroupQueue = loopGroupQueue; }
        await PersistQueueAsync(cancellationToken);
        await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken);
    }

    public async Task<GroupQueueState> MoveGroupQueueNextAsync(CancellationToken cancellationToken = default) { await AdvanceQueueAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> ResetGroupQueueAsync(CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) _groupQueuePosition = 0; await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> RemoveGroupQueueAtAsync(int index, CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) { if (index >= 0 && index < _groupQueue.Count) _groupQueue.RemoveAt(index); if (_groupQueuePosition >= _groupQueue.Count) _groupQueuePosition = Math.Max(0, _groupQueue.Count - 1); } await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> MoveGroupQueueItemAsync(int index, int delta, CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) { var target = index + delta; if (index >= 0 && index < _groupQueue.Count && target >= 0 && target < _groupQueue.Count) { (_groupQueue[index], _groupQueue[target]) = (_groupQueue[target], _groupQueue[index]); if (_groupQueuePosition == index) _groupQueuePosition = target; else if (_groupQueuePosition == target) _groupQueuePosition = index; } } await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }
    public async Task<GroupQueueState> SetQueueLoopAsync(bool loop, CancellationToken cancellationToken = default) { await EnsureQueueLoadedAsync(cancellationToken); lock (_gate) _loopGroupQueue = loop; await PersistQueueAsync(cancellationToken); await RefreshLedAsync(cancellationToken); return await GetGroupQueueAsync(cancellationToken); }

    public async Task<RfidReadResult> SimulateRfidAsync(string tagId, CancellationToken cancellationToken = default) => await _rfid.SimulateReadAsync(tagId, cancellationToken);

    public async Task<RunRecord> StartRunAsync(string? fallbackRider, string? trailName, CancellationToken cancellationToken = default)
    {
        await EnsureQueueLoadedAsync(cancellationToken);
        var reason = await GetStartBlockedReasonAsync(cancellationToken); if (reason is not null) throw new InvalidOperationException(reason);
        var settings = await _settings.GetAsync(cancellationToken);
        trailName = string.IsNullOrWhiteSpace(trailName) ? settings.TrailName : trailName;
        if (OperationMode == SystemOperationMode.GroupQueue)
        {
            var next = await GetNextRiderAsync(cancellationToken) ?? throw new InvalidOperationException("Group queue is empty");
            return await Upper.StartRunAsync(next.DisplayName, trailName, next.RiderId, SystemOperationMode.GroupQueue, _groupQueuePosition, cancellationToken);
        }
        if (SelectedRiderId is { } id && SelectedRiderName is { } name) return await Upper.StartRunAsync(name, trailName, id, SystemOperationMode.ManualEncoderSelection, null, cancellationToken);
        if (!string.IsNullOrWhiteSpace(fallbackRider)) return await Upper.StartRunAsync(fallbackRider, trailName, null, SystemOperationMode.ManualEncoderSelection, null, cancellationToken);
        throw new InvalidOperationException("No rider selected");
    }

    public async Task ResetAsync(CancellationToken cancellationToken = default)
    {
        Lower.Reset(); await Upper.ResetAsync(cancellationToken); lock (_gate) { _selectedRiderId = null; _selectedRiderName = null; _groupQueuePosition = 0; _rfidBlockedReason = null; } await RefreshLedAsync(cancellationToken);
    }

    private async Task<string?> GetStartBlockedReasonAsync(CancellationToken ct)
    {
        if (Upper.State is UpperStationState.Countdown or UpperStationState.Riding) return "Run already active";
        if (!Lower.Diagnostics.Online) return "Finish station is offline";
        if (!Lower.BeamClear) return "Finish beam is blocked";
        if (!Upper.IsTimeSynchronized) return "Time synchronization required";
        if (Upper.State == UpperStationState.Error) return "Critical station error";
        if (OperationMode == SystemOperationMode.GroupQueue) { var q = await GetGroupQueueAsync(ct); if (q.GroupQueue.Count == 0) return "Group queue is empty"; if (q.GroupQueuePosition >= q.GroupQueue.Count) return "Group queue finished"; }
        else { if (!string.IsNullOrEmpty(_rfidBlockedReason)) return _rfidBlockedReason; if (!(await _riders.ListAsync(false, ct)).Any()) return "No active riders registered"; if (SelectedRiderId is null) return "No rider selected"; }
        return null;
    }

    private async Task<RegisteredRider?> GetNextRiderAsync(CancellationToken ct)
    {
        await EnsureQueueLoadedAsync(ct);
        Guid? id = null; lock (_gate) { if (_groupQueuePosition < _groupQueue.Count) id = _groupQueue[_groupQueuePosition]; }
        return id is null ? null : await _riders.GetAsync(id.Value, ct);
    }

    private async Task RefreshLedAsync(CancellationToken ct)
    {
        if (!Upper.IsTimeSynchronized) { _led.SetText("SYNC TIME"); return; }
        if (!Lower.Diagnostics.Online) { _led.SetText("FINISH OFFLINE"); return; }
        if (!Lower.BeamClear) { _led.SetText("BEAM BLOCKED"); return; }
        if (!string.IsNullOrEmpty(Upper.CountdownText)) { _led.SetText(Upper.CountdownText); return; }
        if (Upper.ActiveRun is { } active) { _led.SetText(string.IsNullOrWhiteSpace(active.RiderName) ? "RIDING" : active.RiderName); return; }
        if (Upper.LastRun?.Status == RunStatus.Finished && Upper.LastRun.ResultMs is not null && DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() < _showFinishedResultUntilMs) { _led.SetText(TimeFormatter.FormatResult(Upper.LastRun.ResultMs)); return; }
        if (OperationMode == SystemOperationMode.GroupQueue) { var q = await GetGroupQueueAsync(ct); _led.SetText(q.GroupQueue.Count == 0 ? "QUEUE EMPTY" : (q.NextRiderName ?? "QUEUE DONE")); }
        else if (!(await _riders.ListAsync(false, ct)).Any()) _led.SetText("NO RIDERS");
        else _led.SetText(SelectedRiderName ?? "SELECT RIDER");
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
            _loopGroupQueue = persisted.Loop;
            _queueLoaded = true;
        }
    }

    private Task PersistQueueAsync(CancellationToken ct)
    {
        Guid[] ids; int position; bool loop;
        lock (_gate) { ids = _groupQueue.ToArray(); position = _groupQueuePosition; loop = _loopGroupQueue; }
        return _queueRepository.SaveAsync(new PersistedGroupQueue { RiderIds = ids.ToList(), Position = position, Loop = loop }, ct);
    }

    private async Task OnRunFinishedAsync(RunRecord run, CancellationToken ct) { _showFinishedResultUntilMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + 3000; if (run.OperationMode == SystemOperationMode.GroupQueue) await AdvanceQueueAsync(ct); await RefreshLedAsync(ct); _ = Task.Run(async () => { await Task.Delay(3000); Upper.ReadyAfterFinish(); await RefreshLedAsync(CancellationToken.None); }); }
    private async Task AdvanceQueueAsync(CancellationToken ct) { await EnsureQueueLoadedAsync(ct); lock (_gate) { if (_groupQueue.Count == 0) return; _groupQueuePosition++; if (_groupQueuePosition >= _groupQueue.Count && _loopGroupQueue) _groupQueuePosition = 0; } await PersistQueueAsync(ct); await RefreshLedAsync(ct); }
    private async Task OnTagReadAsync(RfidReadResult read, CancellationToken ct)
    {
        var rider = await _riders.GetByRfidTagAsync(read.TagId, ct);
        if (OperationMode == SystemOperationMode.ManualEncoderSelection)
        {
            lock (_gate) { _selectedRiderId = rider?.RiderId; _selectedRiderName = rider?.DisplayName; _rfidBlockedReason = rider is null ? "Unknown RFID tag" : null; }
            _led.SetText(rider?.DisplayName ?? "UNKNOWN TAG");
        }
    }
}

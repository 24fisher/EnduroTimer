using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;
using EnduroTimer.Core.Protocol;

namespace EnduroTimer.Core.Services;

public sealed class SystemClockService(long offsetMs = 0) : IClockService
{
    public long OffsetMs { get; set; } = offsetMs;

    public long GetUnixTimeMilliseconds() => DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + OffsetMs;
}

public sealed class ManualClockService(long nowMs = 0) : IClockService
{
    public long NowMs { get; set; } = nowMs;

    public long GetUnixTimeMilliseconds() => NowMs;

    public void Advance(long milliseconds) => NowMs += milliseconds;
}

public sealed class ConsoleBuzzerService : IBuzzerService
{
    public List<string> Events { get; } = new();

    public Task BeepAsync(string cue, CancellationToken cancellationToken = default)
    {
        Events.Add(cue);
        Console.WriteLine($"[BUZZER] {cue}");
        return Task.CompletedTask;
    }
}

public sealed class InMemoryRadioTransport : IRadioTransport
{
    public event Func<RadioMessage, CancellationToken, Task>? MessageReceived;

    public List<RadioMessage> SentMessages { get; } = new();

    public async Task SendAsync(RadioMessage message, CancellationToken cancellationToken = default)
    {
        SentMessages.Add(message);
        if (MessageReceived is null)
        {
            return;
        }

        foreach (Func<RadioMessage, CancellationToken, Task> handler in MessageReceived.GetInvocationList())
        {
            await handler(message, cancellationToken);
        }
    }
}

public sealed class InMemoryRunRepository : IRunRepository
{
    private readonly List<RunRecord> _runs = new();
    private readonly object _gate = new();

    public Task AddAsync(RunRecord run, CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _runs.Add(run);
        }

        return Task.CompletedTask;
    }

    public Task<RunRecord?> GetAsync(Guid runId, CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            return Task.FromResult(_runs.FirstOrDefault(run => run.RunId == runId));
        }
    }

    public Task<IReadOnlyList<RunRecord>> ListAsync(int take = 50, CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            var personalBestIds = _runs
                .Where(run => run.Status == RunStatus.Finished && run.ResultMs is not null)
                .GroupBy(run => run.RiderId?.ToString() ?? run.Rider, StringComparer.OrdinalIgnoreCase)
                .Select(group => group.OrderBy(run => run.ResultMs!.Value).ThenBy(run => run.StartTimestampMs).First().RunId)
                .ToHashSet();

            return Task.FromResult<IReadOnlyList<RunRecord>>(_runs
                .OrderByDescending(run => run.StartTimestampMs)
                .Take(take)
                .Select(run => Clone(run, personalBestIds.Contains(run.RunId)))
                .ToList());
        }
    }

    public Task UpdateAsync(RunRecord run, CancellationToken cancellationToken = default) => Task.CompletedTask;

    public Task ClearAsync(CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _runs.Clear();
        }

        return Task.CompletedTask;
    }

    private static RunRecord Clone(RunRecord run, bool isPersonalBest = false) => new()
    {
        RunId = run.RunId,
        RiderId = run.RiderId,
        Rider = run.Rider,
        TrailId = run.TrailId,
        TrailNameSnapshot = (!string.IsNullOrWhiteSpace(run.TrailNameSnapshot) && (run.TrailNameSnapshot != RunRecord.DefaultTrailName || string.IsNullOrWhiteSpace(run.TrailName) || run.TrailName == RunRecord.DefaultTrailName)) ? run.TrailNameSnapshot : (string.IsNullOrWhiteSpace(run.TrailName) ? RunRecord.DefaultTrailName : run.TrailName),
        TrailName = (!string.IsNullOrWhiteSpace(run.TrailNameSnapshot) && (run.TrailNameSnapshot != RunRecord.DefaultTrailName || string.IsNullOrWhiteSpace(run.TrailName) || run.TrailName == RunRecord.DefaultTrailName)) ? run.TrailNameSnapshot : (string.IsNullOrWhiteSpace(run.TrailName) ? RunRecord.DefaultTrailName : run.TrailName),
        StartTimestampMs = run.StartTimestampMs,
        FinishTimestampMs = run.FinishTimestampMs,
        ResultMs = run.ResultMs,
        Status = run.Status,
        OperationMode = run.OperationMode,
        QueuePosition = run.QueuePosition,
        IsPersonalBest = isPersonalBest
    };
}


public sealed class InMemoryTrailRepository(IClockService clock) : ITrailRepository
{
    private readonly List<Trail> _trails = new();
    private readonly object _gate = new();
    public Task<IReadOnlyList<Trail>> ListAsync(bool includeInactive = true, CancellationToken cancellationToken = default)
    {
        lock (_gate) { EnsureAny(null); return Task.FromResult<IReadOnlyList<Trail>>(_trails.Where(t => includeInactive || t.IsActive).Select(Clone).ToList()); }
    }
    public Task<Trail?> GetAsync(Guid trailId, CancellationToken cancellationToken = default)
    {
        lock (_gate) { EnsureAny(null); return Task.FromResult(_trails.FirstOrDefault(t => t.TrailId == trailId) is { } t ? Clone(t) : null); }
    }
    public Task<Trail> AddAsync(string displayName, CancellationToken cancellationToken = default)
    {
        Validate(displayName);
        lock (_gate) { EnsureAny(null); EnsureUnique(displayName, null); var t = new Trail { DisplayName = displayName.Trim(), CreatedAtMs = clock.GetUnixTimeMilliseconds(), IsActive = true }; _trails.Add(t); return Task.FromResult(Clone(t)); }
    }
    public Task<Trail> UpdateAsync(Guid trailId, string displayName, bool isActive, CancellationToken cancellationToken = default)
    {
        Validate(displayName);
        lock (_gate) { EnsureAny(null); var t = _trails.FirstOrDefault(x => x.TrailId == trailId) ?? throw new KeyNotFoundException("Trail not found."); if (isActive) EnsureUnique(displayName, trailId); t.DisplayName = displayName.Trim(); t.IsActive = isActive; if (_trails.All(x => !x.IsActive)) _trails.Add(DefaultTrail(null)); return Task.FromResult(Clone(t)); }
    }
    public Task DeactivateAsync(Guid trailId, CancellationToken cancellationToken = default)
    {
        lock (_gate) { EnsureAny(null); (_trails.FirstOrDefault(x => x.TrailId == trailId) ?? throw new KeyNotFoundException("Trail not found.")).IsActive = false; if (_trails.All(x => !x.IsActive)) _trails.Add(DefaultTrail(null)); return Task.CompletedTask; }
    }
    public Task<Trail> EnsureDefaultAsync(string? legacyTrailName = null, CancellationToken cancellationToken = default)
    {
        lock (_gate) { EnsureAny(legacyTrailName); return Task.FromResult(Clone(_trails.First(t => t.IsActive))); }
    }
    private void EnsureAny(string? legacyTrailName) { if (_trails.Count == 0) _trails.Add(DefaultTrail(legacyTrailName)); if (_trails.All(t => !t.IsActive)) _trails.Add(DefaultTrail(null)); }
    private Trail DefaultTrail(string? name) => new() { DisplayName = string.IsNullOrWhiteSpace(name) ? RunRecord.DefaultTrailName : name.Trim(), IsActive = true, CreatedAtMs = clock.GetUnixTimeMilliseconds() };
    private void EnsureUnique(string displayName, Guid? except) { if (_trails.Any(t => t.IsActive && t.TrailId != except && string.Equals(t.DisplayName, displayName.Trim(), StringComparison.OrdinalIgnoreCase))) throw new InvalidOperationException("Active trail with this display name already exists"); }
    private static void Validate(string displayName) { if (string.IsNullOrWhiteSpace(displayName)) throw new ArgumentException("Display name is required"); }
    private static Trail Clone(Trail t) => new() { TrailId = t.TrailId, DisplayName = t.DisplayName, IsActive = t.IsActive, CreatedAtMs = t.CreatedAtMs };
}

public sealed class SimulatedLedDisplayService : ILedDisplayService
{
    private readonly object _gate = new();
    private string _text = string.Empty;
    public void SetText(string text) { lock (_gate) _text = text ?? string.Empty; }
    public string GetText() { lock (_gate) return _text; }
}

public sealed class SimulatedRfidReaderService(IClockService clock) : IRfidReaderService
{
    private RfidReadResult? _lastRead;
    public event Func<RfidReadResult, CancellationToken, Task>? TagRead;
    public Task<RfidReadResult?> GetLastReadAsync(CancellationToken cancellationToken = default) => Task.FromResult(_lastRead);
    public async Task<RfidReadResult> SimulateReadAsync(string tagId, CancellationToken cancellationToken = default)
    {
        _lastRead = new RfidReadResult { TagId = tagId.Trim(), ReadAtMs = clock.GetUnixTimeMilliseconds() };
        if (TagRead is not null)
        {
            foreach (Func<RfidReadResult, CancellationToken, Task> handler in TagRead.GetInvocationList()) await handler(_lastRead, cancellationToken);
        }
        return _lastRead;
    }
}

public sealed class InMemoryRegisteredRiderRepository(IClockService clock) : IRegisteredRiderRepository
{
    private readonly List<RegisteredRider> _riders = new();
    private readonly object _gate = new();

    public Task<IReadOnlyList<RegisteredRider>> ListAsync(bool includeInactive = true, CancellationToken cancellationToken = default)
    {
        lock (_gate) return Task.FromResult<IReadOnlyList<RegisteredRider>>(_riders.Where(r => includeInactive || r.IsActive).Select(Clone).ToList());
    }

    public Task<RegisteredRider?> GetAsync(Guid riderId, CancellationToken cancellationToken = default)
    {
        lock (_gate) return Task.FromResult(_riders.FirstOrDefault(r => r.RiderId == riderId) is { } r ? Clone(r) : null);
    }

    public Task<RegisteredRider?> GetByRfidTagAsync(string tagId, CancellationToken cancellationToken = default)
    {
        lock (_gate) return Task.FromResult(_riders.FirstOrDefault(r => r.IsActive && string.Equals(r.RfidTagId, tagId, StringComparison.OrdinalIgnoreCase)) is { } r ? Clone(r) : null);
    }

    public Task<RegisteredRider> AddAsync(string displayName, string? rfidTagId, CancellationToken cancellationToken = default)
    {
        Validate(displayName, rfidTagId, null);
        lock (_gate)
        {
            EnsureUniqueRfid(rfidTagId, null);
            var rider = new RegisteredRider { DisplayName = displayName.Trim(), RfidTagId = Normalize(rfidTagId), CreatedAtMs = clock.GetUnixTimeMilliseconds(), IsActive = true };
            _riders.Add(rider);
            return Task.FromResult(Clone(rider));
        }
    }

    public Task<RegisteredRider> UpdateAsync(Guid riderId, string displayName, string? rfidTagId, bool isActive, CancellationToken cancellationToken = default)
    {
        Validate(displayName, rfidTagId, riderId);
        lock (_gate)
        {
            var rider = _riders.FirstOrDefault(r => r.RiderId == riderId) ?? throw new KeyNotFoundException("Rider not found.");
            if (isActive) EnsureUniqueRfid(rfidTagId, riderId);
            rider.DisplayName = displayName.Trim(); rider.RfidTagId = Normalize(rfidTagId); rider.IsActive = isActive;
            return Task.FromResult(Clone(rider));
        }
    }

    public Task DeactivateAsync(Guid riderId, CancellationToken cancellationToken = default)
    {
        lock (_gate) (_riders.FirstOrDefault(r => r.RiderId == riderId) ?? throw new KeyNotFoundException("Rider not found.")).IsActive = false;
        return Task.CompletedTask;
    }

    private void Validate(string displayName, string? rfidTagId, Guid? riderId) { if (string.IsNullOrWhiteSpace(displayName)) throw new ArgumentException("Display name is required"); }
    private void EnsureUniqueRfid(string? tag, Guid? except) { var normalized = Normalize(tag); if (normalized is not null && _riders.Any(r => r.IsActive && r.RiderId != except && string.Equals(r.RfidTagId, normalized, StringComparison.OrdinalIgnoreCase))) throw new InvalidOperationException("Active rider with this RFID tag already exists"); }
    private static string? Normalize(string? value) => string.IsNullOrWhiteSpace(value) ? null : value.Trim();
    private static RegisteredRider Clone(RegisteredRider r) => new() { RiderId = r.RiderId, DisplayName = r.DisplayName, RfidTagId = r.RfidTagId, IsActive = r.IsActive, CreatedAtMs = r.CreatedAtMs };
}

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
                .GroupBy(run => run.Rider, StringComparer.OrdinalIgnoreCase)
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
        Rider = run.Rider,
        StartTimestampMs = run.StartTimestampMs,
        FinishTimestampMs = run.FinishTimestampMs,
        ResultMs = run.ResultMs,
        Status = run.Status,
        IsPersonalBest = isPersonalBest
    };
}

using System.Text.Json.Nodes;
using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;
using EnduroTimer.Core.Protocol;

namespace EnduroTimer.Core.Services;

public sealed class UpperStationService : IStartButtonService
{
    public const string DefaultStationId = "upper-start";
    private readonly IClockService _clock;
    private readonly IBuzzerService _buzzer;
    private readonly IRadioTransport _radio;
    private readonly IRunRepository _runs;
    private readonly object _gate = new();
    private RunRecord? _activeRun;
    private RunRecord? _lastRun;

    public UpperStationService(IClockService clock, IBuzzerService buzzer, IRadioTransport radio, IRunRepository runs)
    {
        _clock = clock;
        _buzzer = buzzer;
        _radio = radio;
        _runs = runs;
        State = UpperStationState.Boot;
        Diagnostics = new StationDiagnostics
        {
            StationId = DefaultStationId,
            Online = true,
            BatteryVoltage = 4.05,
            LastRssi = -69,
            LastSeenUnixMs = _clock.GetUnixTimeMilliseconds()
        };
        _radio.MessageReceived += OnRadioMessageAsync;
        State = UpperStationState.Ready;
    }

    public UpperStationState State { get; private set; }
    public StationDiagnostics Diagnostics { get; }
    public int CountdownStepDelayMs { get; init; } = 1000;
    public long RtcOffsetMs { get; private set; }
    public bool RtcOffsetWarning => Math.Abs(RtcOffsetMs) > 100;
    public RunRecord? ActiveRun => _activeRun;
    public RunRecord? LastRun => _lastRun;

    public async Task<RunRecord> StartRunAsync(string rider, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(rider))
        {
            throw new ArgumentException("Rider name or number is required.", nameof(rider));
        }

        lock (_gate)
        {
            if (State is UpperStationState.Countdown or UpperStationState.Riding)
            {
                throw new InvalidOperationException("A run is already active.");
            }

            State = UpperStationState.Countdown;
        }

        foreach (var cue in new[] { "3", "2", "1", "GO" })
        {
            await _buzzer.BeepAsync(cue, cancellationToken);
            if (CountdownStepDelayMs > 0 && cue != "GO")
            {
                await Task.Delay(CountdownStepDelayMs, cancellationToken);
            }
        }

        var run = new RunRecord
        {
            Rider = rider.Trim(),
            StartTimestampMs = _clock.GetUnixTimeMilliseconds(),
            Status = RunStatus.Riding
        };

        lock (_gate)
        {
            _activeRun = run;
            _lastRun = run;
            State = UpperStationState.Riding;
        }

        await _runs.AddAsync(run, cancellationToken);
        await _radio.SendAsync(RadioMessage.Create(
            RadioMessageType.RunStart,
            DefaultStationId,
            run.RunId,
            run.StartTimestampMs,
            new JsonObject { ["rider"] = run.Rider }), cancellationToken);

        return run;
    }

    async Task IStartButtonService.PressStartAsync(string rider, CancellationToken cancellationToken) =>
        await StartRunAsync(rider, cancellationToken);

    public async Task SyncTimeAsync(CancellationToken cancellationToken = default)
    {
        await _radio.SendAsync(RadioMessage.Create(RadioMessageType.SyncTime, DefaultStationId, timestampMs: _clock.GetUnixTimeMilliseconds()), cancellationToken);
    }

    public async Task MarkDnfAsync(Guid runId, CancellationToken cancellationToken = default)
    {
        var run = await _runs.GetAsync(runId, cancellationToken) ?? throw new KeyNotFoundException("Run not found.");
        run.Status = RunStatus.Dnf;
        run.FinishTimestampMs = null;
        run.ResultMs = null;
        lock (_gate)
        {
            if (_activeRun?.RunId == runId)
            {
                _activeRun = null;
                State = UpperStationState.Ready;
            }

            _lastRun = run;
        }

        await _runs.UpdateAsync(run, cancellationToken);
    }

    public async Task ResetAsync(CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            _activeRun = null;
            _lastRun = null;
            State = UpperStationState.Ready;
            RtcOffsetMs = 0;
        }

        await _runs.ClearAsync(cancellationToken);
    }

    private async Task OnRadioMessageAsync(RadioMessage message, CancellationToken cancellationToken)
    {
        if (message.StationId == DefaultStationId)
        {
            return;
        }

        Diagnostics.LastSeenUnixMs = _clock.GetUnixTimeMilliseconds();
        Diagnostics.LastRssi = -68;

        switch (message.Type)
        {
            case RadioMessageType.SyncTimeAck when message.TimestampMs is not null:
                var upperTimestamp = message.Payload["upperTimestampMs"]?.GetValue<long>() ?? _clock.GetUnixTimeMilliseconds();
                RtcOffsetMs = message.TimestampMs.Value - upperTimestamp;
                break;
            case RadioMessageType.Finish when message.RunId is not null && message.TimestampMs is not null:
                await FinishRunAsync(message.RunId.Value, message.TimestampMs.Value, cancellationToken);
                break;
        }
    }

    private async Task FinishRunAsync(Guid runId, long finishTimestampMs, CancellationToken cancellationToken)
    {
        RunRecord? run;
        lock (_gate)
        {
            run = _activeRun;
            if (State != UpperStationState.Riding || run is null || run.RunId != runId || run.Status != RunStatus.Riding)
            {
                return;
            }

            run.FinishTimestampMs = finishTimestampMs;
            run.ResultMs = finishTimestampMs - run.StartTimestampMs;
            run.Status = RunStatus.Finished;
            _lastRun = run;
            _activeRun = null;
            State = UpperStationState.Finished;
        }

        await _runs.UpdateAsync(run, cancellationToken);
        await _radio.SendAsync(RadioMessage.Create(
            RadioMessageType.FinishAck,
            DefaultStationId,
            run.RunId,
            _clock.GetUnixTimeMilliseconds()), cancellationToken);
    }
}

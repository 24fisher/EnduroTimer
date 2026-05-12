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
    private string _countdownText = string.Empty;

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
    public int GoDisplayDelayMs { get; init; } = 750;
    public string CountdownText
    {
        get
        {
            lock (_gate)
            {
                return _countdownText;
            }
        }
    }
    public bool IsCountdownActive
    {
        get
        {
            lock (_gate)
            {
                return State == UpperStationState.Countdown;
            }
        }
    }
    public long RtcOffsetMs { get; private set; }
    public bool RtcOffsetWarning => Math.Abs(RtcOffsetMs) > 100;
    public bool IsTimeSynchronized { get; private set; }
    public bool TimeSyncRequired => !IsTimeSynchronized;
    public RunRecord? ActiveRun
    {
        get
        {
            lock (_gate)
            {
                return _activeRun;
            }
        }
    }
    public RunRecord? LastRun
    {
        get
        {
            lock (_gate)
            {
                return _lastRun;
            }
        }
    }

    public event Func<RunRecord, CancellationToken, Task>? RunFinished;

    public async Task<RunRecord> StartRunAsync(string rider, string? trailName = null, Guid? riderId = null, SystemOperationMode operationMode = SystemOperationMode.ManualEncoderSelection, int? queuePosition = null, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(rider))
        {
            throw new ArgumentException("Rider name is required", nameof(rider));
        }

        if (!IsTimeSynchronized)
        {
            throw new InvalidOperationException("Time synchronization required before starting a run");
        }

        RunRecord run;
        lock (_gate)
        {
            if (State is UpperStationState.Countdown or UpperStationState.Riding)
            {
                throw new InvalidOperationException("Run already active");
            }

            run = new RunRecord
            {
                RiderId = riderId,
                Rider = rider.Trim(),
                OperationMode = operationMode,
                QueuePosition = queuePosition,
                TrailName = string.IsNullOrWhiteSpace(trailName) ? RunRecord.DefaultTrailName : trailName.Trim(),
                Status = RunStatus.Pending,
                CreatedAtMs = _clock.GetUnixTimeMilliseconds()
            };
            _activeRun = run;
            _lastRun = run;
            _countdownText = "3";
            State = UpperStationState.Countdown;
        }

        await _runs.AddAsync(run, cancellationToken);
        _ = Task.Run(() => RunCountdownAsync(run.RunId, CancellationToken.None), CancellationToken.None);

        return run;
    }

    async Task IStartButtonService.PressStartAsync(string rider, CancellationToken cancellationToken) =>
        await StartRunAsync(rider, cancellationToken: cancellationToken);

    private async Task RunCountdownAsync(Guid runId, CancellationToken cancellationToken)
    {
        try
        {
            foreach (var cue in new[] { "3", "2", "1" })
            {
                lock (_gate)
                {
                    if (_activeRun?.RunId != runId || State != UpperStationState.Countdown)
                    {
                        return;
                    }

                    _countdownText = cue;
                }

                await _buzzer.BeepAsync(cue, cancellationToken);
                if (CountdownStepDelayMs > 0)
                {
                    await Task.Delay(CountdownStepDelayMs, cancellationToken);
                }
            }

            RunRecord? run;
            lock (_gate)
            {
                if (_activeRun?.RunId != runId || State != UpperStationState.Countdown)
                {
                    return;
                }

                _countdownText = "GO";
                run = _activeRun;
                run.StartTimestampMs = _clock.GetUnixTimeMilliseconds();
                run.Status = RunStatus.Riding;
                _lastRun = run;
                State = UpperStationState.Riding;
            }

            await _buzzer.BeepAsync("GO", cancellationToken);
            await _runs.UpdateAsync(run, cancellationToken);
            await _radio.SendAsync(RadioMessage.Create(
                RadioMessageType.RunStart,
                DefaultStationId,
                run.RunId,
                run.StartTimestampMs,
                new JsonObject { ["rider"] = run.Rider, ["riderId"] = run.RiderId?.ToString(), ["trailName"] = run.TrailName }), cancellationToken);

            if (GoDisplayDelayMs > 0)
            {
                await Task.Delay(GoDisplayDelayMs, cancellationToken);
            }

            lock (_gate)
            {
                if (_activeRun?.RunId == runId && State == UpperStationState.Riding && _countdownText == "GO")
                {
                    _countdownText = string.Empty;
                }
            }
        }
        catch
        {
            lock (_gate)
            {
                if (_activeRun?.RunId == runId)
                {
                    State = UpperStationState.Error;
                    _countdownText = string.Empty;
                }
            }
        }
    }

    public async Task SyncTimeAsync(CancellationToken cancellationToken = default)
    {
        await _radio.SendAsync(RadioMessage.Create(RadioMessageType.SyncTime, DefaultStationId, timestampMs: _clock.GetUnixTimeMilliseconds()), cancellationToken);
    }

    public void SimulateRtcOffset(long rtcOffsetMs)
    {
        RtcOffsetMs = rtcOffsetMs;
        IsTimeSynchronized = !RtcOffsetWarning;
    }

    public void ReadyAfterFinish()
    {
        lock (_gate)
        {
            if (State == UpperStationState.Finished)
            {
                State = UpperStationState.Ready;
            }
        }
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
                _countdownText = string.Empty;
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
            IsTimeSynchronized = false;
            _countdownText = string.Empty;
        }
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
                IsTimeSynchronized = !RtcOffsetWarning;
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
            _countdownText = string.Empty;
            State = UpperStationState.Finished;
        }

        await _runs.UpdateAsync(run, cancellationToken);
        if (RunFinished is not null)
        {
            foreach (Func<RunRecord, CancellationToken, Task> handler in RunFinished.GetInvocationList())
            {
                await handler(run, cancellationToken);
            }
        }
        await _radio.SendAsync(RadioMessage.Create(
            RadioMessageType.FinishAck,
            DefaultStationId,
            run.RunId,
            _clock.GetUnixTimeMilliseconds()), cancellationToken);
    }
}

using System.Text.Json.Nodes;
using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;
using EnduroTimer.Core.Protocol;

namespace EnduroTimer.Core.Services;

public sealed class LowerStationService : IFinishSensorService
{
    public const string DefaultStationId = "lower-finish";
    private readonly IClockService _clock;
    private readonly IRadioTransport _radio;
    private readonly object _gate = new();
    private Guid? _activeRunId;
    private long? _lastFinishTimestampMs;

    public LowerStationService(IClockService clock, IRadioTransport radio)
    {
        _clock = clock;
        _radio = radio;
        State = LowerStationState.Boot;
        Diagnostics = new StationDiagnostics
        {
            StationId = DefaultStationId,
            Online = true,
            BatteryVoltage = 11.8,
            LastRssi = -72,
            LastSeenUnixMs = _clock.GetUnixTimeMilliseconds()
        };
        _radio.MessageReceived += OnRadioMessageAsync;
        State = LowerStationState.Idle;
    }

    public LowerStationState State { get; private set; }
    public StationDiagnostics Diagnostics { get; }
    public bool BeamClear { get; private set; } = true;
    public TimeSpan FinishDuplicateWindow { get; init; } = TimeSpan.FromSeconds(5);

    public async Task TriggerAsync(CancellationToken cancellationToken = default)
    {
        Guid runId;
        long finishTimestampMs;

        lock (_gate)
        {
            if (State != LowerStationState.WaitFinish || _activeRunId is null)
            {
                return;
            }

            finishTimestampMs = _clock.GetUnixTimeMilliseconds();
            if (_lastFinishTimestampMs is not null && finishTimestampMs - _lastFinishTimestampMs.Value < FinishDuplicateWindow.TotalMilliseconds)
            {
                return;
            }

            _lastFinishTimestampMs = finishTimestampMs;
            runId = _activeRunId.Value;
            State = LowerStationState.Finished;
        }

        await _radio.SendAsync(RadioMessage.Create(
            RadioMessageType.Finish,
            DefaultStationId,
            runId,
            finishTimestampMs,
            new JsonObject { ["beamClear"] = BeamClear }), cancellationToken);
    }

    public void SetBeamBlocked(bool blocked)
    {
        BeamClear = !blocked;
        if (blocked && State == LowerStationState.Idle)
        {
            State = LowerStationState.SensorBlocked;
        }
        else if (!blocked && State == LowerStationState.SensorBlocked)
        {
            State = LowerStationState.Idle;
        }
    }

    public void Reset()
    {
        lock (_gate)
        {
            _activeRunId = null;
            _lastFinishTimestampMs = null;
            State = BeamClear ? LowerStationState.Idle : LowerStationState.SensorBlocked;
        }
    }

    private void AcknowledgeFinish()
    {
        lock (_gate)
        {
            _activeRunId = null;
            State = BeamClear ? LowerStationState.Idle : LowerStationState.SensorBlocked;
        }
    }

    private async Task OnRadioMessageAsync(RadioMessage message, CancellationToken cancellationToken)
    {
        if (message.StationId == DefaultStationId)
        {
            return;
        }

        Diagnostics.LastSeenUnixMs = _clock.GetUnixTimeMilliseconds();
        Diagnostics.LastRssi = -70;

        switch (message.Type)
        {
            case RadioMessageType.Ping:
                await _radio.SendAsync(RadioMessage.Create(RadioMessageType.Pong, DefaultStationId, timestampMs: _clock.GetUnixTimeMilliseconds()), cancellationToken);
                break;
            case RadioMessageType.SyncTime when message.TimestampMs is not null:
                await _radio.SendAsync(RadioMessage.Create(
                    RadioMessageType.SyncTimeAck,
                    DefaultStationId,
                    timestampMs: _clock.GetUnixTimeMilliseconds(),
                    payload: new JsonObject { ["upperTimestampMs"] = message.TimestampMs.Value }), cancellationToken);
                break;
            case RadioMessageType.RunStart when message.RunId is not null:
                lock (_gate)
                {
                    _activeRunId = message.RunId;
                    _lastFinishTimestampMs = null;
                    State = BeamClear ? LowerStationState.WaitFinish : LowerStationState.SensorBlocked;
                }
                break;
            case RadioMessageType.FinishAck:
                AcknowledgeFinish();
                break;
        }
    }
}

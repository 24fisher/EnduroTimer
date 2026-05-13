namespace EnduroTimer.Core.Models;

public sealed class StationDiagnostics
{
    public string StationId { get; init; } = string.Empty;
    public bool Online { get; set; }
    public double BatteryVoltage { get; set; }
    public int LastRssi { get; set; }
    public long LastSeenUnixMs { get; set; }
}

public sealed class ActiveRunStatus
{
    public Guid RunId { get; init; }
    public Guid? RiderId { get; init; }
    public string RiderName { get; init; } = string.Empty;
    public long StartedAtMs { get; init; }
    public string ElapsedFormatted { get; init; } = string.Empty;
    public int SequenceNumber { get; init; }
}

public sealed class SystemStatus
{
    public UpperStationState UpperState { get; init; }
    public UpperStationState StartStationState => UpperState;
    public UpperStationState StartState => UpperState;
    public LowerStationState LowerState { get; init; }
    public LowerStationState FinishStationState => LowerState;
    public LowerStationState FinishState => LowerState;
    public string CountdownText { get; init; } = string.Empty;
    public bool IsCountdownActive { get; init; }
    public StationDiagnostics Upper { get; init; } = new();
    public StationDiagnostics Lower { get; init; } = new();
    public bool BeamClear { get; init; }
    public bool BeamBlocked => !BeamClear;
    public long? RtcOffsetMs { get; init; }
    public bool RtcOffsetWarning { get; init; }
    public bool RtcWarning => RtcOffsetWarning;
    public bool IsTimeSynchronized { get; init; }
    public bool TimeSyncRequired { get; init; }
    public bool CanStartRun { get; init; }
    public string? StartBlockedReason { get; init; }
    public RunRecord? ActiveRun { get; init; }
    public RunRecord? LastRun { get; init; }
    public SystemOperationMode OperationMode { get; init; }
    public Guid? SelectedRiderId { get; init; }
    public string? SelectedRiderName { get; init; }
    public Guid? NextRiderId { get; init; }
    public string? NextRiderName { get; init; }
    public Guid? CurrentStartingRunId { get; init; }
    public Guid? CountdownRunId => CurrentStartingRunId;
    public Guid? CurrentStartingRiderId { get; init; }
    public string? CurrentStartingRiderName { get; init; }
    public Guid? NextQueuedRiderId { get; init; }
    public string? NextQueuedRiderName { get; init; }
    public Guid? ExpectedFinisherRunId { get; init; }
    public string? ExpectedFinisherRiderName { get; init; }
    public int ActiveRunsCount { get; init; }
    public IReadOnlyList<ActiveRunStatus> ActiveRuns { get; init; } = Array.Empty<ActiveRunStatus>();
    public string LedDisplayText { get; init; } = string.Empty;
    public bool QueueAutoStartEnabled { get; init; }
    public int GroupStartIntervalSeconds { get; init; }
    public int GroupQueuePosition { get; init; }
    public int GroupQueueLength { get; init; }
    public Guid? CurrentRunId => ActiveRun?.RunId;
    public long? LastResultMs => LastRun?.ResultMs;
    public string LastResultFormatted => TimeFormatter.FormatResult(LastResultMs);
    public bool FinishStationOnline => Lower.Online;
    public double StartBatteryVoltage => Upper.BatteryVoltage;
    public double FinishBatteryVoltage => Lower.BatteryVoltage;
}

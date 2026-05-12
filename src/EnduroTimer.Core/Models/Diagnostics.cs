namespace EnduroTimer.Core.Models;

public sealed class StationDiagnostics
{
    public string StationId { get; init; } = string.Empty;
    public bool Online { get; set; }
    public double BatteryVoltage { get; set; }
    public int LastRssi { get; set; }
    public long LastSeenUnixMs { get; set; }
}

public sealed class SystemStatus
{
    public UpperStationState UpperState { get; init; }
    public UpperStationState StartStationState => UpperState;
    public LowerStationState LowerState { get; init; }
    public LowerStationState FinishStationState => LowerState;
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
    public Guid? CurrentRunId => ActiveRun?.RunId;
    public long? LastResultMs => LastRun?.ResultMs;
    public string LastResultFormatted => TimeFormatter.FormatResult(LastResultMs);
    public bool FinishStationOnline => Lower.Online;
    public double StartBatteryVoltage => Upper.BatteryVoltage;
    public double FinishBatteryVoltage => Lower.BatteryVoltage;
}

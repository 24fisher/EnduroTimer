using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;

namespace EnduroTimer.Core.Services;

public sealed class EnduroTimerSystem
{
    private readonly IRunRepository _runs;

    public EnduroTimerSystem(UpperStationService upper, LowerStationService lower, IRunRepository runs)
    {
        Upper = upper;
        Lower = lower;
        _runs = runs;
    }

    public UpperStationService Upper { get; }
    public LowerStationService Lower { get; }

    public async Task<SystemStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        var lastRun = Upper.LastRun ?? (await _runs.ListAsync(1, cancellationToken)).FirstOrDefault();
        var startBlockedReason = GetStartBlockedReason();
        return new SystemStatus
        {
            UpperState = Upper.State,
            LowerState = Lower.State,
            CountdownText = Upper.CountdownText,
            IsCountdownActive = Upper.IsCountdownActive,
            Upper = Upper.Diagnostics,
            Lower = Lower.Diagnostics,
            BeamClear = Lower.BeamClear,
            RtcOffsetMs = Upper.RtcOffsetMs,
            RtcOffsetWarning = Upper.RtcOffsetWarning,
            IsTimeSynchronized = Upper.IsTimeSynchronized,
            TimeSyncRequired = Upper.TimeSyncRequired,
            CanStartRun = startBlockedReason is null,
            StartBlockedReason = startBlockedReason,
            ActiveRun = Upper.ActiveRun,
            LastRun = lastRun
        };
    }

    private string? GetStartBlockedReason()
    {
        if (Upper.State is UpperStationState.Countdown or UpperStationState.Riding)
        {
            return "Run already active";
        }

        if (!Lower.Diagnostics.Online)
        {
            return "Finish station is offline";
        }

        if (!Lower.BeamClear)
        {
            return "Finish beam is blocked";
        }

        if (!Upper.IsTimeSynchronized)
        {
            return "Time synchronization required before starting a run";
        }

        if (Upper.State == UpperStationState.Error)
        {
            return "Critical station error";
        }

        return null;
    }

    public async Task ResetAsync(CancellationToken cancellationToken = default)
    {
        Lower.Reset();
        await Upper.ResetAsync(cancellationToken);
    }
}

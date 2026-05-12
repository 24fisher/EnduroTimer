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
        return new SystemStatus
        {
            UpperState = Upper.State,
            LowerState = Lower.State,
            Upper = Upper.Diagnostics,
            Lower = Lower.Diagnostics,
            BeamClear = Lower.BeamClear,
            RtcOffsetMs = Upper.RtcOffsetMs,
            RtcOffsetWarning = Upper.RtcOffsetWarning,
            ActiveRun = Upper.ActiveRun,
            LastRun = lastRun
        };
    }

    public async Task ResetAsync(CancellationToken cancellationToken = default)
    {
        Lower.Reset();
        await Upper.ResetAsync(cancellationToken);
    }
}

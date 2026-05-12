namespace EnduroTimer.Core.Models;

public sealed class RunRecord
{
    public const string DefaultTrailName = "Default trail";

    public Guid RunId { get; init; } = Guid.NewGuid();
    public string Rider { get; init; } = string.Empty;
    public string RiderName => Rider;
    public string TrailName { get; init; } = DefaultTrailName;
    public long StartTimestampMs { get; set; }
    public long? FinishTimestampMs { get; set; }
    public long? ResultMs { get; set; }
    public string ResultFormatted => TimeFormatter.FormatResult(ResultMs);
    public RunStatus Status { get; set; } = RunStatus.Pending;
    public bool IsPersonalBest { get; set; }
}

public static class TimeFormatter
{
    public static string FormatResult(long? resultMs)
    {
        if (resultMs is null)
        {
            return "--:--.---";
        }

        var value = Math.Max(0, resultMs.Value);
        var minutes = value / 60_000;
        var seconds = value % 60_000 / 1_000;
        var milliseconds = value % 1_000;
        return $"{minutes:00}:{seconds:00}.{milliseconds:000}";
    }
}

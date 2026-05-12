namespace EnduroTimer.Core.Models;

public sealed class RunRecord
{
    public Guid RunId { get; init; } = Guid.NewGuid();
    public string Rider { get; init; } = string.Empty;
    public long StartTimestampMs { get; set; }
    public long? FinishTimestampMs { get; set; }
    public long? ResultMs { get; set; }
    public RunStatus Status { get; set; } = RunStatus.Pending;
}

namespace EnduroTimer.Core.Models;

public sealed class SystemSettings
{
    public string TrailName { get; set; } = RunRecord.DefaultTrailName;
}

public sealed class PersistedGroupQueue
{
    public List<Guid> RiderIds { get; set; } = new();
    public int Position { get; set; }
    public bool Loop { get; set; } = true;
}

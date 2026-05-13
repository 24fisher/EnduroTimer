namespace EnduroTimer.Core.Models;

public sealed class SystemSettings
{
    public Guid? SelectedTrailId { get; set; }
    public string? TrailName { get; set; }
    public int GroupStartIntervalSeconds { get; set; } = 10;
}

public sealed class SystemSettingsDto
{
    public Guid? SelectedTrailId { get; init; }
    public string SelectedTrailName { get; init; } = RunRecord.DefaultTrailName;
    public int GroupStartIntervalSeconds { get; init; } = 10;
}

public sealed class PersistedGroupQueue
{
    public List<Guid> RiderIds { get; set; } = new();
    public int Position { get; set; }
}

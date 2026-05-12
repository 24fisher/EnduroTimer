namespace EnduroTimer.Core.Models;

public sealed class RegisteredRider
{
    public Guid RiderId { get; init; } = Guid.NewGuid();
    public string DisplayName { get; set; } = string.Empty;
    public string? RfidTagId { get; set; }
    public bool IsActive { get; set; } = true;
    public long CreatedAtMs { get; init; }
}

public sealed class GroupQueueEntry
{
    public Guid RiderId { get; init; }
    public string DisplayName { get; init; } = string.Empty;
}

public sealed class GroupQueueState
{
    public IReadOnlyList<GroupQueueEntry> GroupQueue { get; init; } = Array.Empty<GroupQueueEntry>();
    public int GroupQueuePosition { get; init; }
    public bool LoopGroupQueue { get; init; } = true;
    public Guid? NextRiderId { get; init; }
    public string? NextRiderName { get; init; }
}

public sealed class RfidReadResult
{
    public string TagId { get; init; } = string.Empty;
    public Guid? RiderId { get; init; }
    public string? RiderName { get; init; }
    public long ReadAtMs { get; init; }
}

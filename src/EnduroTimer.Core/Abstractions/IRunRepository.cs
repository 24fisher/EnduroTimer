using EnduroTimer.Core.Models;

namespace EnduroTimer.Core.Abstractions;

public interface IRunRepository
{
    Task AddAsync(RunRecord run, CancellationToken cancellationToken = default);
    Task<RunRecord?> GetAsync(Guid runId, CancellationToken cancellationToken = default);
    Task<IReadOnlyList<RunRecord>> ListAsync(int take = 50, CancellationToken cancellationToken = default);
    Task UpdateAsync(RunRecord run, CancellationToken cancellationToken = default);
    Task ClearAsync(CancellationToken cancellationToken = default);
}


public interface ISystemSettingsRepository
{
    Task<EnduroTimer.Core.Models.SystemSettings> GetAsync(CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.SystemSettings> SaveAsync(EnduroTimer.Core.Models.SystemSettings settings, CancellationToken cancellationToken = default);
}

public interface IGroupQueueRepository
{
    Task<EnduroTimer.Core.Models.PersistedGroupQueue> GetAsync(CancellationToken cancellationToken = default);
    Task SaveAsync(EnduroTimer.Core.Models.PersistedGroupQueue queue, CancellationToken cancellationToken = default);
}

public interface ITrailRepository
{
    Task<IReadOnlyList<EnduroTimer.Core.Models.Trail>> ListAsync(bool includeInactive = true, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.Trail?> GetAsync(Guid trailId, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.Trail> AddAsync(string displayName, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.Trail> UpdateAsync(Guid trailId, string displayName, bool isActive, CancellationToken cancellationToken = default);
    Task DeactivateAsync(Guid trailId, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.Trail> EnsureDefaultAsync(string? legacyTrailName = null, CancellationToken cancellationToken = default);
}

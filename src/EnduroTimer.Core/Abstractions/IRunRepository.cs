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

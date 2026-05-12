using EnduroTimer.Core.Protocol;

namespace EnduroTimer.Core.Abstractions;

public interface IClockService
{
    long GetUnixTimeMilliseconds();
}

public interface IStartButtonService
{
    Task PressStartAsync(string rider, CancellationToken cancellationToken = default);
}

public interface IBuzzerService
{
    Task BeepAsync(string cue, CancellationToken cancellationToken = default);
}

public interface IFinishSensorService
{
    Task TriggerAsync(CancellationToken cancellationToken = default);
}

public interface IRadioTransport
{
    event Func<RadioMessage, CancellationToken, Task>? MessageReceived;
    Task SendAsync(RadioMessage message, CancellationToken cancellationToken = default);
}

public interface ILedDisplayService
{
    void SetText(string text);
    string GetText();
}

public interface IRfidReaderService
{
    event Func<EnduroTimer.Core.Models.RfidReadResult, CancellationToken, Task>? TagRead;
    Task<EnduroTimer.Core.Models.RfidReadResult?> GetLastReadAsync(CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.RfidReadResult> SimulateReadAsync(string tagId, CancellationToken cancellationToken = default);
}

public interface IRegisteredRiderRepository
{
    Task<IReadOnlyList<EnduroTimer.Core.Models.RegisteredRider>> ListAsync(bool includeInactive = true, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.RegisteredRider?> GetAsync(Guid riderId, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.RegisteredRider?> GetByRfidTagAsync(string tagId, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.RegisteredRider> AddAsync(string displayName, string? rfidTagId, CancellationToken cancellationToken = default);
    Task<EnduroTimer.Core.Models.RegisteredRider> UpdateAsync(Guid riderId, string displayName, string? rfidTagId, bool isActive, CancellationToken cancellationToken = default);
    Task DeactivateAsync(Guid riderId, CancellationToken cancellationToken = default);
}

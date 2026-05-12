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

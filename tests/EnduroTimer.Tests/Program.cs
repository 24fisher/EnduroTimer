using EnduroTimer.Core.Models;
using EnduroTimer.Core.Protocol;
using EnduroTimer.Core.Services;

var tests = new (string Name, Func<Task> Test)[]
{
    ("Start creates Run in Riding", Tests.StartCreatesRunInRiding),
    ("Finish after start moves Run to Finished", Tests.FinishAfterStartMovesRunToFinished),
    ("Result is finish minus start", Tests.ResultIsFinishMinusStart),
    ("Duplicate finish within 5 seconds is ignored", Tests.DuplicateFinishWithinFiveSecondsIgnored),
    ("Finish before RunStart is ignored", Tests.FinishBeforeRunStartIgnored),
    ("RTC offset over 100 ms warns", Tests.RtcOffsetOver100MsWarns)
};

var failed = 0;
foreach (var (name, test) in tests)
{
    try
    {
        await test();
        Console.WriteLine($"PASS {name}");
    }
    catch (Exception ex)
    {
        failed++;
        Console.WriteLine($"FAIL {name}: {ex.Message}");
    }
}

return failed == 0 ? 0 : 1;

static class Tests
{
    public static async Task StartCreatesRunInRiding()
    {
        var fixture = Fixture.Create();
        var run = await fixture.Upper.StartRunAsync("101");
        Assert.Equal(RunStatus.Riding, run.Status);
        Assert.Equal(UpperStationState.Riding, fixture.Upper.State);
        Assert.Equal(LowerStationState.WaitFinish, fixture.Lower.State);
    }

    public static async Task FinishAfterStartMovesRunToFinished()
    {
        var fixture = Fixture.Create();
        var run = await fixture.Upper.StartRunAsync("102");
        fixture.LowerClock.Advance(12_345);
        await fixture.Lower.TriggerAsync();
        var stored = await fixture.Repository.GetAsync(run.RunId);
        Assert.NotNull(stored);
        Assert.Equal(RunStatus.Finished, stored!.Status);
        Assert.Equal(UpperStationState.Finished, fixture.Upper.State);
    }

    public static async Task ResultIsFinishMinusStart()
    {
        var fixture = Fixture.Create(startMs: 1_000_000, lowerMs: 1_000_000);
        var run = await fixture.Upper.StartRunAsync("103");
        fixture.LowerClock.Advance(65_432);
        await fixture.Lower.TriggerAsync();
        var stored = await fixture.Repository.GetAsync(run.RunId);
        Assert.Equal(65_432, stored!.ResultMs);
    }

    public static async Task DuplicateFinishWithinFiveSecondsIgnored()
    {
        var fixture = Fixture.Create();
        await fixture.Upper.StartRunAsync("104");
        await fixture.Lower.TriggerAsync();
        fixture.LowerClock.Advance(1_000);
        await fixture.Lower.TriggerAsync();
        var finishMessages = fixture.Radio.SentMessages.Count(message => message.Type == RadioMessageType.Finish);
        Assert.Equal(1, finishMessages);
    }

    public static async Task FinishBeforeRunStartIgnored()
    {
        var fixture = Fixture.Create();
        await fixture.Lower.TriggerAsync();
        Assert.Equal(0, fixture.Radio.SentMessages.Count(message => message.Type == RadioMessageType.Finish));
        Assert.Equal(LowerStationState.Idle, fixture.Lower.State);
    }

    public static async Task RtcOffsetOver100MsWarns()
    {
        var fixture = Fixture.Create(startMs: 1_000_000, lowerMs: 1_000_151);
        await fixture.Upper.SyncTimeAsync();
        Assert.Equal(151, fixture.Upper.RtcOffsetMs);
        Assert.True(fixture.Upper.RtcOffsetWarning);
    }
}

sealed class Fixture
{
    public required ManualClockService UpperClock { get; init; }
    public required ManualClockService LowerClock { get; init; }
    public required InMemoryRadioTransport Radio { get; init; }
    public required InMemoryRunRepository Repository { get; init; }
    public required UpperStationService Upper { get; init; }
    public required LowerStationService Lower { get; init; }

    public static Fixture Create(long startMs = 1_000_000, long? lowerMs = null)
    {
        var upperClock = new ManualClockService(startMs);
        var lowerClock = new ManualClockService(lowerMs ?? startMs);
        var radio = new InMemoryRadioTransport();
        var repository = new InMemoryRunRepository();
        var buzzer = new ConsoleBuzzerService();
        var lower = new LowerStationService(lowerClock, radio);
        var upper = new UpperStationService(upperClock, buzzer, radio, repository) { CountdownStepDelayMs = 0 };
        return new Fixture
        {
            UpperClock = upperClock,
            LowerClock = lowerClock,
            Radio = radio,
            Repository = repository,
            Upper = upper,
            Lower = lower
        };
    }
}

static class Assert
{
    public static void Equal<T>(T expected, T actual)
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
        {
            throw new InvalidOperationException($"Expected {expected}, got {actual}.");
        }
    }

    public static void True(bool actual)
    {
        if (!actual)
        {
            throw new InvalidOperationException("Expected true, got false.");
        }
    }

    public static void NotNull(object? actual)
    {
        if (actual is null)
        {
            throw new InvalidOperationException("Expected non-null value.");
        }
    }
}

using System.Diagnostics;
using EnduroTimer.Core.Models;
using EnduroTimer.Core.Protocol;
using EnduroTimer.Core.Services;

var tests = new (string Name, Func<Task> Test)[]
{
    ("Start immediately enters Countdown", Tests.StartImmediatelyEntersCountdown),
    ("Start returns before Countdown completes", Tests.StartReturnsBeforeCountdownCompletes),
    ("Countdown text changes through 3 2 1 GO", Tests.CountdownTextChangesThroughCues),
    ("Start timestamp is captured on GO", Tests.StartTimestampIsCapturedOnGo),
    ("After GO state becomes Riding", Tests.AfterGoStateBecomesRiding),
    ("Run already active is rejected", Tests.RunAlreadyActiveRejected),
    ("Finish after start moves Run to Finished", Tests.FinishAfterStartMovesRunToFinished),
    ("Result is finish minus start", Tests.ResultIsFinishMinusStart),
    ("Duplicate finish within 5 seconds is ignored", Tests.DuplicateFinishWithinFiveSecondsIgnored),
    ("Finish before RunStart is ignored", Tests.FinishBeforeRunStartIgnored),
    ("RTC offset over 100 ms warns", Tests.RtcOffsetOver100MsWarns),
    ("Formatter uses mm:ss.fff", Tests.FormatterUsesMinutesSecondsMilliseconds),
    ("Personal best marks fastest finished run", Tests.PersonalBestMarksFastestFinishedRun),
    ("Personal best ignores non-finished runs", Tests.PersonalBestIgnoresNonFinishedRuns),
    ("Personal best is independent per rider", Tests.PersonalBestIndependentPerRider),
    ("Single finished run is personal best", Tests.SingleFinishedRunIsPersonalBest)
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
    public static async Task StartImmediatelyEntersCountdown()
    {
        var fixture = Fixture.Create(countdownDelayMs: 100);
        var run = await fixture.Upper.StartRunAsync("101");
        Assert.Equal(RunStatus.Pending, run.Status);
        Assert.Equal(UpperStationState.Countdown, fixture.Upper.State);
        Assert.Equal("3", fixture.Upper.CountdownText);
        Assert.Equal(LowerStationState.Idle, fixture.Lower.State);
    }

    public static async Task StartReturnsBeforeCountdownCompletes()
    {
        var fixture = Fixture.Create(countdownDelayMs: 1000);
        var elapsed = Stopwatch.StartNew();
        await fixture.Upper.StartRunAsync("102");
        elapsed.Stop();
        Assert.True(elapsed.ElapsedMilliseconds < 500);
        Assert.Equal(UpperStationState.Countdown, fixture.Upper.State);
    }

    public static async Task CountdownTextChangesThroughCues()
    {
        var fixture = Fixture.Create(countdownDelayMs: 30, goDisplayDelayMs: 30);
        await fixture.Upper.StartRunAsync("103");
        await WaitUntil(() => fixture.Buzzer.Events.Contains("GO"));
        Assert.SequenceEqual(new[] { "3", "2", "1", "GO" }, fixture.Buzzer.Events);
        Assert.Equal("GO", fixture.Upper.CountdownText);
    }

    public static async Task StartTimestampIsCapturedOnGo()
    {
        var fixture = Fixture.Create(startMs: 1_000_000, countdownDelayMs: 50);
        var run = await fixture.Upper.StartRunAsync("104");
        Assert.Equal(0L, run.StartTimestampMs);
        fixture.UpperClock.Advance(10_000);
        await WaitUntil(() => fixture.Upper.State == UpperStationState.Riding);
        Assert.Equal(1_010_000, run.StartTimestampMs);
    }

    public static async Task AfterGoStateBecomesRiding()
    {
        var fixture = Fixture.Create(countdownDelayMs: 10);
        await fixture.Upper.StartRunAsync("105");
        await WaitUntil(() => fixture.Upper.State == UpperStationState.Riding);
        Assert.Equal(UpperStationState.Riding, fixture.Upper.State);
        Assert.Equal(LowerStationState.WaitFinish, fixture.Lower.State);
    }

    public static async Task RunAlreadyActiveRejected()
    {
        var fixture = Fixture.Create(countdownDelayMs: 100);
        await fixture.Upper.StartRunAsync("106");
        await Assert.ThrowsAsync<InvalidOperationException>(() => fixture.Upper.StartRunAsync("107"), "Run already active");
    }

    public static async Task FinishAfterStartMovesRunToFinished()
    {
        var fixture = Fixture.Create();
        var run = await StartAndWaitForRiding(fixture, "108");
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
        var run = await StartAndWaitForRiding(fixture, "109");
        fixture.LowerClock.Advance(65_432);
        await fixture.Lower.TriggerAsync();
        var stored = await fixture.Repository.GetAsync(run.RunId);
        Assert.Equal(65_432, stored!.ResultMs);
    }

    public static async Task DuplicateFinishWithinFiveSecondsIgnored()
    {
        var fixture = Fixture.Create();
        await StartAndWaitForRiding(fixture, "110");
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

    public static Task FormatterUsesMinutesSecondsMilliseconds()
    {
        Assert.Equal("01:03.218", TimeFormatter.FormatResult(63_218));
        return Task.CompletedTask;
    }

    public static async Task PersonalBestMarksFastestFinishedRun()
    {
        var repository = new InMemoryRunRepository();
        var slow = FinishedRun("Andrey", 72_500, 1_000);
        var fast = FinishedRun("Andrey", 69_300, 2_000);
        await repository.AddAsync(slow);
        await repository.AddAsync(fast);
        var runs = await repository.ListAsync();
        Assert.False(runs.Single(run => run.RunId == slow.RunId).IsPersonalBest);
        Assert.True(runs.Single(run => run.RunId == fast.RunId).IsPersonalBest);
    }

    public static async Task PersonalBestIgnoresNonFinishedRuns()
    {
        var repository = new InMemoryRunRepository();
        var finished = FinishedRun("Max", 80_000, 1_000);
        await repository.AddAsync(finished);
        await repository.AddAsync(new RunRecord { Rider = "Max", StartTimestampMs = 2_000, Status = RunStatus.Dnf });
        await repository.AddAsync(new RunRecord { Rider = "Max", StartTimestampMs = 3_000, Status = RunStatus.Riding, ResultMs = 70_000 });
        var runs = await repository.ListAsync();
        Assert.True(runs.Single(run => run.RunId == finished.RunId).IsPersonalBest);
        Assert.False(runs.Where(run => run.RunId != finished.RunId).Any(run => run.IsPersonalBest));
    }

    public static async Task PersonalBestIndependentPerRider()
    {
        var repository = new InMemoryRunRepository();
        var andrey = FinishedRun("Andrey", 69_300, 1_000);
        var max = FinishedRun("Max", 80_000, 2_000);
        await repository.AddAsync(andrey);
        await repository.AddAsync(max);
        var runs = await repository.ListAsync();
        Assert.True(runs.Single(run => run.RunId == andrey.RunId).IsPersonalBest);
        Assert.True(runs.Single(run => run.RunId == max.RunId).IsPersonalBest);
    }

    public static async Task SingleFinishedRunIsPersonalBest()
    {
        var repository = new InMemoryRunRepository();
        var only = FinishedRun("Anna", 75_000, 1_000);
        await repository.AddAsync(only);
        var runs = await repository.ListAsync();
        Assert.True(runs.Single().IsPersonalBest);
    }

    private static async Task<RunRecord> StartAndWaitForRiding(Fixture fixture, string rider)
    {
        var run = await fixture.Upper.StartRunAsync(rider);
        await WaitUntil(() => fixture.Upper.State == UpperStationState.Riding);
        return run;
    }

    private static RunRecord FinishedRun(string rider, long resultMs, long startTimestampMs) => new()
    {
        Rider = rider,
        StartTimestampMs = startTimestampMs,
        FinishTimestampMs = startTimestampMs + resultMs,
        ResultMs = resultMs,
        Status = RunStatus.Finished
    };

    private static async Task WaitUntil(Func<bool> predicate, int timeoutMs = 1000)
    {
        var elapsed = Stopwatch.StartNew();
        while (!predicate())
        {
            if (elapsed.ElapsedMilliseconds > timeoutMs)
            {
                throw new InvalidOperationException("Timed out waiting for condition.");
            }

            await Task.Delay(5);
        }
    }
}

sealed class Fixture
{
    public required ManualClockService UpperClock { get; init; }
    public required ManualClockService LowerClock { get; init; }
    public required InMemoryRadioTransport Radio { get; init; }
    public required InMemoryRunRepository Repository { get; init; }
    public required ConsoleBuzzerService Buzzer { get; init; }
    public required UpperStationService Upper { get; init; }
    public required LowerStationService Lower { get; init; }

    public static Fixture Create(long startMs = 1_000_000, long? lowerMs = null, int countdownDelayMs = 0, int goDisplayDelayMs = 0)
    {
        var upperClock = new ManualClockService(startMs);
        var lowerClock = new ManualClockService(lowerMs ?? startMs);
        var radio = new InMemoryRadioTransport();
        var repository = new InMemoryRunRepository();
        var buzzer = new ConsoleBuzzerService();
        var lower = new LowerStationService(lowerClock, radio);
        var upper = new UpperStationService(upperClock, buzzer, radio, repository) { CountdownStepDelayMs = countdownDelayMs, GoDisplayDelayMs = goDisplayDelayMs };
        return new Fixture
        {
            UpperClock = upperClock,
            LowerClock = lowerClock,
            Radio = radio,
            Repository = repository,
            Buzzer = buzzer,
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

    public static void SequenceEqual<T>(IReadOnlyList<T> expected, IReadOnlyList<T> actual)
    {
        if (!expected.SequenceEqual(actual))
        {
            throw new InvalidOperationException($"Expected [{string.Join(", ", expected)}], got [{string.Join(", ", actual)}].");
        }
    }

    public static void True(bool actual)
    {
        if (!actual)
        {
            throw new InvalidOperationException("Expected true, got false.");
        }
    }

    public static void False(bool actual)
    {
        if (actual)
        {
            throw new InvalidOperationException("Expected false, got true.");
        }
    }

    public static void NotNull(object? actual)
    {
        if (actual is null)
        {
            throw new InvalidOperationException("Expected non-null value.");
        }
    }

    public static async Task ThrowsAsync<TException>(Func<Task> action, string? message = null)
        where TException : Exception
    {
        try
        {
            await action();
        }
        catch (TException ex)
        {
            if (message is not null && ex.Message != message)
            {
                throw new InvalidOperationException($"Expected exception message '{message}', got '{ex.Message}'.");
            }

            return;
        }

        throw new InvalidOperationException($"Expected exception {typeof(TException).Name}.");
    }
}

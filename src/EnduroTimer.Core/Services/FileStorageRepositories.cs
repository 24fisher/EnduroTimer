using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;

namespace EnduroTimer.Core.Services;

public sealed class DataDirectory
{
    public DataDirectory(string path)
    {
        Path = path;
        Directory.CreateDirectory(Path);
    }

    public string Path { get; }
    public string File(string name) => System.IO.Path.Combine(Path, name);
}

internal static class JsonFile
{
    public static readonly JsonSerializerOptions Options = CreateOptions(writeIndented: true);

    public static JsonSerializerOptions CreateOptions(bool writeIndented)
    {
        var options = new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = writeIndented };
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }

    public static async Task<T> ReadAsync<T>(string path, T fallback, CancellationToken ct)
    {
        if (!System.IO.File.Exists(path)) return fallback;
        await using var stream = System.IO.File.OpenRead(path);
        return await JsonSerializer.DeserializeAsync<T>(stream, Options, ct) ?? fallback;
    }

    public static async Task WriteAsync<T>(string path, T value, CancellationToken ct)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        var temp = path + ".tmp";
        await using (var stream = System.IO.File.Create(temp))
        {
            await JsonSerializer.SerializeAsync(stream, value, Options, ct);
        }
        if (System.IO.File.Exists(path)) System.IO.File.Delete(path);
        System.IO.File.Move(temp, path);
    }
}

public sealed class FileSystemSettingsRepository(DataDirectory data) : ISystemSettingsRepository
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string Path => data.File("settings.json");

    public async Task<SystemSettings> GetAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var settings = await JsonFile.ReadAsync(Path, new SystemSettings(), cancellationToken);
            settings.TrailName = string.IsNullOrWhiteSpace(settings.TrailName) ? null : settings.TrailName.Trim();
            settings.GroupStartIntervalSeconds = Math.Max(3, settings.GroupStartIntervalSeconds);
            return settings;
        }
        finally { _gate.Release(); }
    }

    public async Task<SystemSettings> SaveAsync(SystemSettings settings, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            settings.TrailName = string.IsNullOrWhiteSpace(settings.TrailName) ? null : settings.TrailName.Trim();
            settings.GroupStartIntervalSeconds = Math.Max(3, settings.GroupStartIntervalSeconds);
            await JsonFile.WriteAsync(Path, settings, cancellationToken);
            return settings;
        }
        finally { _gate.Release(); }
    }
}


public sealed class FileTrailRepository(DataDirectory data, IClockService clock) : ITrailRepository
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string Path => data.File("trails.json");

    public async Task<IReadOnlyList<Trail>> ListAsync(bool includeInactive = true, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var trails = await EnsureAnyLockedAsync(null, cancellationToken);
            return trails.Where(t => includeInactive || t.IsActive).Select(Clone).ToList();
        }
        finally { _gate.Release(); }
    }

    public async Task<Trail?> GetAsync(Guid trailId, CancellationToken cancellationToken = default) =>
        (await ListAsync(true, cancellationToken)).FirstOrDefault(t => t.TrailId == trailId);

    public async Task<Trail> AddAsync(string displayName, CancellationToken cancellationToken = default)
    {
        Validate(displayName);
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var trails = await EnsureAnyLockedAsync(null, cancellationToken);
            EnsureUniqueActiveName(trails, displayName, null);
            var trail = new Trail { DisplayName = displayName.Trim(), IsActive = true, CreatedAtMs = clock.GetUnixTimeMilliseconds() };
            trails.Add(trail);
            await WriteAllAsync(trails, cancellationToken);
            return Clone(trail);
        }
        finally { _gate.Release(); }
    }

    public async Task<Trail> UpdateAsync(Guid trailId, string displayName, bool isActive, CancellationToken cancellationToken = default)
    {
        Validate(displayName);
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var trails = await EnsureAnyLockedAsync(null, cancellationToken);
            var trail = trails.FirstOrDefault(t => t.TrailId == trailId) ?? throw new KeyNotFoundException("Trail not found.");
            if (isActive) EnsureUniqueActiveName(trails, displayName, trailId);
            trail.DisplayName = displayName.Trim();
            trail.IsActive = isActive;
            await WriteAllAsync(trails, cancellationToken);
            return Clone(trail);
        }
        finally { _gate.Release(); }
    }

    public async Task DeactivateAsync(Guid trailId, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var trails = await EnsureAnyLockedAsync(null, cancellationToken);
            var trail = trails.FirstOrDefault(t => t.TrailId == trailId) ?? throw new KeyNotFoundException("Trail not found.");
            trail.IsActive = false;
            if (trails.All(t => !t.IsActive)) trails.Add(DefaultTrail());
            await WriteAllAsync(trails, cancellationToken);
        }
        finally { _gate.Release(); }
    }

    public async Task<Trail> EnsureDefaultAsync(string? legacyTrailName = null, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var trails = await EnsureAnyLockedAsync(legacyTrailName, cancellationToken);
            return Clone(trails.First(t => t.IsActive));
        }
        finally { _gate.Release(); }
    }

    private async Task<List<Trail>> EnsureAnyLockedAsync(string? legacyTrailName, CancellationToken ct)
    {
        var trails = await ReadAllAsync(ct);
        if (trails.Count == 0)
        {
            trails.Add(DefaultTrail(legacyTrailName));
            await WriteAllAsync(trails, ct);
        }
        if (trails.All(t => !t.IsActive))
        {
            trails.Add(DefaultTrail());
            await WriteAllAsync(trails, ct);
        }
        return trails;
    }

    private Trail DefaultTrail(string? legacyTrailName = null) => new()
    {
        DisplayName = string.IsNullOrWhiteSpace(legacyTrailName) ? RunRecord.DefaultTrailName : legacyTrailName.Trim(),
        IsActive = true,
        CreatedAtMs = clock.GetUnixTimeMilliseconds()
    };

    private Task<List<Trail>> ReadAllAsync(CancellationToken ct) => JsonFile.ReadAsync(Path, new List<Trail>(), ct);
    private Task WriteAllAsync(List<Trail> trails, CancellationToken ct) => JsonFile.WriteAsync(Path, trails, ct);
    private static void Validate(string displayName) { if (string.IsNullOrWhiteSpace(displayName)) throw new ArgumentException("Display name is required"); }
    private static void EnsureUniqueActiveName(IEnumerable<Trail> trails, string displayName, Guid? except)
    {
        if (trails.Any(t => t.IsActive && t.TrailId != except && string.Equals(t.DisplayName.Trim(), displayName.Trim(), StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidOperationException("Active trail with this display name already exists");
        }
    }
    private static Trail Clone(Trail t) => new() { TrailId = t.TrailId, DisplayName = t.DisplayName, IsActive = t.IsActive, CreatedAtMs = t.CreatedAtMs };
}

public sealed class FileGroupQueueRepository(DataDirectory data) : IGroupQueueRepository
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string Path => data.File("group_queue.json");

    public async Task<PersistedGroupQueue> GetAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try { return await JsonFile.ReadAsync(Path, new PersistedGroupQueue(), cancellationToken); }
        finally { _gate.Release(); }
    }

    public async Task SaveAsync(PersistedGroupQueue queue, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            queue.Position = Math.Max(0, queue.Position);
            await JsonFile.WriteAsync(Path, queue, cancellationToken);
        }
        finally { _gate.Release(); }
    }
}

public sealed class FileRegisteredRiderRepository(DataDirectory data, IClockService clock) : IRegisteredRiderRepository
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string Path => data.File("riders.json");

    public async Task<IReadOnlyList<RegisteredRider>> ListAsync(bool includeInactive = true, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var riders = await ReadAllAsync(cancellationToken);
            return riders.Where(r => includeInactive || r.IsActive).Select(Clone).ToList();
        }
        finally { _gate.Release(); }
    }

    public async Task<RegisteredRider?> GetAsync(Guid riderId, CancellationToken cancellationToken = default) =>
        (await ListAsync(true, cancellationToken)).FirstOrDefault(r => r.RiderId == riderId);

    public async Task<RegisteredRider?> GetByRfidTagAsync(string tagId, CancellationToken cancellationToken = default) =>
        (await ListAsync(false, cancellationToken)).FirstOrDefault(r => string.Equals(r.RfidTagId, Normalize(tagId), StringComparison.OrdinalIgnoreCase));

    public async Task<RegisteredRider> AddAsync(string displayName, string? rfidTagId, CancellationToken cancellationToken = default)
    {
        Validate(displayName);
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var riders = await ReadAllAsync(cancellationToken);
            EnsureUniqueRfid(riders, rfidTagId, null);
            var rider = new RegisteredRider { DisplayName = displayName.Trim(), RfidTagId = Normalize(rfidTagId), IsActive = true, CreatedAtMs = clock.GetUnixTimeMilliseconds() };
            riders.Add(rider);
            await WriteAllAsync(riders, cancellationToken);
            return Clone(rider);
        }
        finally { _gate.Release(); }
    }

    public async Task<RegisteredRider> UpdateAsync(Guid riderId, string displayName, string? rfidTagId, bool isActive, CancellationToken cancellationToken = default)
    {
        Validate(displayName);
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var riders = await ReadAllAsync(cancellationToken);
            var rider = riders.FirstOrDefault(r => r.RiderId == riderId) ?? throw new KeyNotFoundException("Rider not found.");
            if (isActive) EnsureUniqueRfid(riders, rfidTagId, riderId);
            rider.DisplayName = displayName.Trim();
            rider.RfidTagId = Normalize(rfidTagId);
            rider.IsActive = isActive;
            await WriteAllAsync(riders, cancellationToken);
            return Clone(rider);
        }
        finally { _gate.Release(); }
    }

    public async Task DeactivateAsync(Guid riderId, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var riders = await ReadAllAsync(cancellationToken);
            var rider = riders.FirstOrDefault(r => r.RiderId == riderId) ?? throw new KeyNotFoundException("Rider not found.");
            rider.IsActive = false;
            await WriteAllAsync(riders, cancellationToken);
        }
        finally { _gate.Release(); }
    }

    private Task<List<RegisteredRider>> ReadAllAsync(CancellationToken ct) => JsonFile.ReadAsync(Path, new List<RegisteredRider>(), ct);
    private Task WriteAllAsync(List<RegisteredRider> riders, CancellationToken ct) => JsonFile.WriteAsync(Path, riders, ct);
    private static void Validate(string displayName) { if (string.IsNullOrWhiteSpace(displayName)) throw new ArgumentException("Display name is required"); }
    private static string? Normalize(string? value) => string.IsNullOrWhiteSpace(value) ? null : value.Trim();
    private static void EnsureUniqueRfid(IEnumerable<RegisteredRider> riders, string? tag, Guid? except) { var normalized = Normalize(tag); if (normalized is not null && riders.Any(r => r.IsActive && r.RiderId != except && string.Equals(r.RfidTagId, normalized, StringComparison.OrdinalIgnoreCase))) throw new InvalidOperationException("Active rider with this RFID tag already exists"); }
    private static RegisteredRider Clone(RegisteredRider r) => new() { RiderId = r.RiderId, DisplayName = r.DisplayName, RfidTagId = r.RfidTagId, IsActive = r.IsActive, CreatedAtMs = r.CreatedAtMs };
}

public sealed class FileRunRepository(DataDirectory data) : IRunRepository
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string JsonlPath => data.File("runs.jsonl");
    private string CsvPath => data.File("runs.csv");

    public async Task AddAsync(RunRecord run, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var runs = await ReadAllAsync(cancellationToken);
            runs.RemoveAll(r => r.RunId == run.RunId);
            runs.Add(run);
            await RewriteAsync(runs, cancellationToken);
        }
        finally { _gate.Release(); }
    }

    public async Task<RunRecord?> GetAsync(Guid runId, CancellationToken cancellationToken = default) =>
        (await ListAsync(int.MaxValue, cancellationToken)).FirstOrDefault(r => r.RunId == runId);

    public async Task<IReadOnlyList<RunRecord>> ListAsync(int take = 50, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var runs = await ReadAllAsync(cancellationToken);
            var personalBestIds = runs.Where(r => r.Status == RunStatus.Finished && r.ResultMs is not null)
                .GroupBy(r => r.RiderId?.ToString() ?? r.Rider, StringComparer.OrdinalIgnoreCase)
                .Select(g => g.OrderBy(r => r.ResultMs!.Value).ThenBy(r => r.StartTimestampMs).First().RunId).ToHashSet();
            return runs.OrderByDescending(r => r.StartTimestampMs).Take(take).Select(r => Clone(r, personalBestIds.Contains(r.RunId))).ToList();
        }
        finally { _gate.Release(); }
    }

    public async Task UpdateAsync(RunRecord run, CancellationToken cancellationToken = default) => await AddAsync(run, cancellationToken);

    public async Task ClearAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (System.IO.File.Exists(JsonlPath)) System.IO.File.Delete(JsonlPath);
            await System.IO.File.WriteAllBytesAsync(CsvPath, CsvBomWithHeader(), cancellationToken);
        }
        finally { _gate.Release(); }
    }

    private async Task<List<RunRecord>> ReadAllAsync(CancellationToken ct)
    {
        var runs = new List<RunRecord>();
        if (!System.IO.File.Exists(JsonlPath)) return runs;
        foreach (var line in await System.IO.File.ReadAllLinesAsync(JsonlPath, ct))
        {
            if (string.IsNullOrWhiteSpace(line)) continue;
            var run = JsonSerializer.Deserialize<RunRecord>(line, JsonFile.Options);
            if (run is not null) runs.Add(run);
        }
        return runs;
    }

    private async Task RewriteAsync(List<RunRecord> runs, CancellationToken ct)
    {
        Directory.CreateDirectory(data.Path);
        var compact = JsonFile.CreateOptions(writeIndented: false);
        await System.IO.File.WriteAllLinesAsync(JsonlPath, runs.OrderBy(r => r.CreatedAtMs).Select(r => JsonSerializer.Serialize(r, compact)), new UTF8Encoding(false), ct);
        var csv = CsvBomWithHeader().Concat(Encoding.UTF8.GetBytes(string.Concat(runs.Where(r => r.Status is RunStatus.Finished or RunStatus.Dnf).OrderBy(r => r.StartTimestampMs).Select(ToCsvLine)))).ToArray();
        await System.IO.File.WriteAllBytesAsync(CsvPath, csv, ct);
    }

    private static byte[] CsvBomWithHeader() => Encoding.UTF8.GetPreamble().Concat(Encoding.UTF8.GetBytes("runId;riderId;riderName;Trail ID;Trail Name;operationMode;queuePosition;startTimestampMs;finishTimestampMs;resultMs;resultFormatted;status;isPersonalBest;createdAtMs\n")).ToArray();
    private static string ToCsvLine(RunRecord r) => string.Join(';', new[] { r.RunId.ToString(), r.RiderId?.ToString() ?? string.Empty, Escape(r.RiderName), r.TrailId?.ToString() ?? string.Empty, Escape(TrailNameOf(r)), r.OperationMode.ToString(), r.QueuePosition?.ToString(CultureInfo.InvariantCulture) ?? string.Empty, r.StartTimestampMs.ToString(CultureInfo.InvariantCulture), r.FinishTimestampMs?.ToString(CultureInfo.InvariantCulture) ?? string.Empty, r.ResultMs?.ToString(CultureInfo.InvariantCulture) ?? string.Empty, r.ResultFormatted, r.Status.ToString(), r.IsPersonalBest ? "true" : "false", r.CreatedAtMs.ToString(CultureInfo.InvariantCulture) }) + "\n";
    private static string TrailNameOf(RunRecord r) => (!string.IsNullOrWhiteSpace(r.TrailNameSnapshot) && (r.TrailNameSnapshot != RunRecord.DefaultTrailName || string.IsNullOrWhiteSpace(r.TrailName) || r.TrailName == RunRecord.DefaultTrailName)) ? r.TrailNameSnapshot : (string.IsNullOrWhiteSpace(r.TrailName) ? RunRecord.DefaultTrailName : r.TrailName);
    private static string Escape(string? value) { var v = value ?? string.Empty; return v.Contains('"') || v.Contains('\n') || v.Contains('\r') || v.Contains(';') ? $"\"{v.Replace("\"", "\"\"")}\"" : v; }
    private static RunRecord Clone(RunRecord r, bool personalBest) => new() { RunId = r.RunId, RiderId = r.RiderId, Rider = r.Rider, OperationMode = r.OperationMode, QueuePosition = r.QueuePosition, SequenceNumber = r.SequenceNumber, TrailId = r.TrailId, TrailNameSnapshot = TrailNameOf(r), TrailName = TrailNameOf(r), StartTimestampMs = r.StartTimestampMs, FinishTimestampMs = r.FinishTimestampMs, ResultMs = r.ResultMs, Status = r.Status, IsPersonalBest = personalBest, CreatedAtMs = r.CreatedAtMs };
}

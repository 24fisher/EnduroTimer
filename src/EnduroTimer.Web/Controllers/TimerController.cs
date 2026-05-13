using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Models;
using EnduroTimer.Core.Services;
using Microsoft.AspNetCore.Mvc;

namespace EnduroTimer.Web.Controllers;

[ApiController]
[Route("api")]
public sealed class TimerController : ControllerBase
{
    private readonly EnduroTimerSystem _system;
    private readonly UpperStationService _upper;
    private readonly IFinishSensorService _finishSensor;
    private readonly IRunRepository _runs;
    private readonly IRegisteredRiderRepository _riders;
    private readonly IRfidReaderService _rfid;
    private readonly ISystemSettingsRepository _settings;

    public TimerController(EnduroTimerSystem system, UpperStationService upper, IFinishSensorService finishSensor, IRunRepository runs, IRegisteredRiderRepository riders, IRfidReaderService rfid, ISystemSettingsRepository settings)
    {
        _system = system;
        _upper = upper;
        _finishSensor = finishSensor;
        _runs = runs;
        _riders = riders;
        _rfid = rfid;
        _settings = settings;
    }

    [HttpGet("status")]
    public async Task<ActionResult<SystemStatus>> GetStatus(CancellationToken cancellationToken) => Ok(await _system.GetStatusAsync(cancellationToken));

    [HttpGet("settings")]
    public async Task<ActionResult<SystemSettings>> GetSettings(CancellationToken cancellationToken) => Ok(await _settings.GetAsync(cancellationToken));

    [HttpPost("settings")]
    public async Task<ActionResult<SystemSettings>> SaveSettings([FromBody] SystemSettings request, CancellationToken cancellationToken) => Ok(await _settings.SaveAsync(request, cancellationToken));

    [HttpPost("time/sync")]
    public async Task<ActionResult<SystemStatus>> SyncTime(CancellationToken cancellationToken)
    {
        await _upper.SyncTimeAsync(cancellationToken);
        return Ok(await _system.GetStatusAsync(cancellationToken));
    }

    [HttpPost("time/simulate-offset")]
    public async Task<ActionResult<SystemStatus>> SimulateOffset([FromBody] TimeOffsetRequest request, CancellationToken cancellationToken)
    {
        _upper.SimulateRtcOffset(request.RtcOffsetMs);
        return Ok(await _system.GetStatusAsync(cancellationToken));
    }

    [HttpPost("runs/start")]
    public async Task<ActionResult<RunRecord>> StartRun([FromBody] StartRunRequest? request, CancellationToken cancellationToken)
    {
        try { return Ok(await _system.StartRunAsync(null, request?.TrailName ?? request?.TrackName, cancellationToken)); }
        catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); }
        catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); }
    }

    [HttpPost("finish/simulate")]
    public async Task<ActionResult<SystemStatus>> SimulateFinish(CancellationToken cancellationToken)
    {
        try
        {
            if (_system.OperationMode == SystemOperationMode.GroupQueue)
            {
                await _system.FinishNextRunAsync(cancellationToken);
            }
            else
            {
                if (_system.Lower.State != LowerStationState.WaitFinish || _upper.ActiveRun is null) return Conflict(new { error = "Finish station is not waiting for a run" });
                await _finishSensor.TriggerAsync(cancellationToken);
            }

            return Ok(await _system.GetStatusAsync(cancellationToken));
        }
        catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); }
    }

    [HttpGet("runs")]
    public async Task<ActionResult<IReadOnlyList<RunRecord>>> GetRuns(CancellationToken cancellationToken) => Ok(await _runs.ListAsync(50, cancellationToken));

    [HttpGet("runs/{id:guid}")]
    public async Task<ActionResult<RunRecord>> GetRun(Guid id, CancellationToken cancellationToken) => await _runs.GetAsync(id, cancellationToken) is { } run ? Ok(run) : NotFound();

    [HttpPost("runs/{id:guid}/dnf")]
    public async Task<IActionResult> MarkDnf(Guid id, CancellationToken cancellationToken) { try { await _system.MarkDnfAsync(id, cancellationToken); return NoContent(); } catch (KeyNotFoundException) { return NotFound(); } }

    [HttpPost("runs/clear")]
    [HttpPost("system/clear-runs")]
    public async Task<IActionResult> ClearRuns(CancellationToken cancellationToken) { await _runs.ClearAsync(cancellationToken); return NoContent(); }

    [HttpGet("riders")]
    public async Task<ActionResult<IReadOnlyList<RiderListItemDto>>> GetRiders(CancellationToken cancellationToken)
    {
        var riders = await _riders.ListAsync(true, cancellationToken);
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken);
        return Ok(riders.Select(r =>
        {
            var riderRuns = runs.Where(run => run.RiderId == r.RiderId || (run.RiderId is null && string.Equals(run.RiderName, r.DisplayName, StringComparison.OrdinalIgnoreCase))).ToList();
            var best = riderRuns.Where(run => run.Status == RunStatus.Finished && run.ResultMs is not null).MinBy(run => run.ResultMs);
            return new RiderListItemDto(r.RiderId, r.DisplayName, r.RfidTagId, r.IsActive, r.CreatedAtMs, riderRuns.Count, best?.ResultMs, best is null ? null : TimeFormatter.FormatResult(best.ResultMs));
        }).ToList());
    }

    [HttpPost("riders")]
    public async Task<ActionResult<RegisteredRider>> AddRider([FromBody] RiderRequest request, CancellationToken cancellationToken)
    { try { return Ok(await _riders.AddAsync(request.DisplayName, request.RfidTagId, cancellationToken)); } catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); } catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); } }

    [HttpPut("riders/{id:guid}")]
    public async Task<ActionResult<RegisteredRider>> UpdateRider(Guid id, [FromBody] RiderRequest request, CancellationToken cancellationToken)
    { try { return Ok(await _riders.UpdateAsync(id, request.DisplayName, request.RfidTagId, request.IsActive ?? true, cancellationToken)); } catch (KeyNotFoundException) { return NotFound(); } catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); } catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); } }

    [HttpPost("riders/{id:guid}/deactivate")]
    public async Task<IActionResult> DeactivateRider(Guid id, CancellationToken cancellationToken) { try { await _riders.DeactivateAsync(id, cancellationToken); return NoContent(); } catch (KeyNotFoundException) { return NotFound(); } }

    [HttpPost("mode")]
    public async Task<ActionResult<SystemStatus>> SetMode([FromBody] ModeRequest request, CancellationToken cancellationToken) { await _system.SetModeAsync(request.OperationMode, cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    [HttpPost("encoder/left")]
    public async Task<ActionResult<SystemStatus>> EncoderLeft(CancellationToken cancellationToken) { await _system.EncoderMoveAsync(-1, cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }
    [HttpPost("encoder/right")]
    public async Task<ActionResult<SystemStatus>> EncoderRight(CancellationToken cancellationToken) { await _system.EncoderMoveAsync(1, cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }
    [HttpPost("encoder/press")]
    public async Task<ActionResult<SystemStatus>> EncoderPress(CancellationToken cancellationToken) { await _system.EncoderPressAsync(cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    [HttpGet("group-queue")]
    public async Task<ActionResult<GroupQueueState>> GetQueue(CancellationToken cancellationToken) => Ok(await _system.GetGroupQueueAsync(cancellationToken));
    [HttpPost("group-queue")]
    public async Task<ActionResult<GroupQueueState>> SetQueue([FromBody] GroupQueueRequest request, CancellationToken cancellationToken) { try { return Ok(await _system.SetGroupQueueAsync(request.RiderIds, cancellationToken)); } catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); } }
    [HttpPost("group-queue/next")]
    public async Task<ActionResult<GroupQueueState>> QueueNext(CancellationToken cancellationToken) => Ok(await _system.MoveGroupQueueNextAsync(cancellationToken));
    [HttpPost("group-queue/reset")]
    public async Task<ActionResult<GroupQueueState>> QueueReset(CancellationToken cancellationToken) => Ok(await _system.ResetGroupQueueAsync(cancellationToken));
    [HttpPost("group-queue/move-up")]
    public async Task<ActionResult<GroupQueueState>> QueueMoveUp([FromBody] QueueIndexRequest request, CancellationToken cancellationToken) => Ok(await _system.MoveGroupQueueItemAsync(request.Index, -1, cancellationToken));
    [HttpPost("group-queue/move-down")]
    public async Task<ActionResult<GroupQueueState>> QueueMoveDown([FromBody] QueueIndexRequest request, CancellationToken cancellationToken) => Ok(await _system.MoveGroupQueueItemAsync(request.Index, 1, cancellationToken));
    [HttpPost("group-queue/remove")]
    public async Task<ActionResult<GroupQueueState>> QueueRemove([FromBody] QueueIndexRequest request, CancellationToken cancellationToken) => Ok(await _system.RemoveGroupQueueAtAsync(request.Index, cancellationToken));
    [HttpPost("group-queue/start-session")]
    public async Task<ActionResult<SystemStatus>> StartQueueSession([FromBody] StartRunRequest? request, CancellationToken cancellationToken) { try { await _system.StartGroupQueueSessionAsync(request?.TrailName ?? request?.TrackName, cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); } catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); } }
    [HttpPost("group-queue/stop-session")]
    public async Task<ActionResult<SystemStatus>> StopQueueSession(CancellationToken cancellationToken) { await _system.StopGroupQueueSessionAsync(cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    [HttpPost("rfid/simulate")]
    public async Task<ActionResult<object>> SimulateRfid([FromBody] RfidSimulateRequest request, CancellationToken cancellationToken)
    {
        var read = await _system.SimulateRfidAsync(request.TagId, cancellationToken);
        var rider = await _riders.GetByRfidTagAsync(request.TagId, cancellationToken);
        return Ok(new { tagId = read.TagId, readAtMs = read.ReadAtMs, matchedRiderId = rider?.RiderId, matchedRiderName = rider?.DisplayName, status = await _system.GetStatusAsync(cancellationToken) });
    }

    [HttpGet("rfid/last")]
    public async Task<ActionResult<RfidReadResult?>> GetLastRfid(CancellationToken cancellationToken) => Ok(await _rfid.GetLastReadAsync(cancellationToken));

    [HttpGet("statistics/riders")]
    public async Task<ActionResult<IReadOnlyList<RiderStatisticsDto>>> GetRiderStatistics(CancellationToken cancellationToken) => Ok(await BuildRiderStatisticsAsync(cancellationToken));

    [HttpGet("export/runs.csv")]
    public async Task<IActionResult> ExportRunsCsv(CancellationToken cancellationToken) => CsvFile("enduro-runs.csv", BuildRunsCsv(await _runs.ListAsync(int.MaxValue, cancellationToken)));

    [HttpGet("export/riders.csv")]
    public async Task<IActionResult> ExportRidersCsv(CancellationToken cancellationToken) => CsvFile("enduro-riders.csv", BuildRidersCsv(await _riders.ListAsync(true, cancellationToken)));

    [HttpGet("export/statistics.csv")]
    public async Task<IActionResult> ExportStatisticsCsv(CancellationToken cancellationToken) => CsvFile("enduro-statistics.csv", BuildStatisticsCsv(await BuildRiderStatisticsAsync(cancellationToken)));

    [HttpGet("export/backup.json")]
    public async Task<IActionResult> ExportBackup(CancellationToken cancellationToken)
    {
        var backup = new { settings = await _settings.GetAsync(cancellationToken), riders = await _riders.ListAsync(true, cancellationToken), runs = await _runs.ListAsync(int.MaxValue, cancellationToken), groupQueue = await _system.GetGroupQueueAsync(cancellationToken) };
        return File(JsonSerializer.SerializeToUtf8Bytes(backup, BackupJsonOptions()), "application/json; charset=utf-8", "enduro-backup.json");
    }

    [HttpPost("system/reset")]
    public async Task<ActionResult<SystemStatus>> Reset(CancellationToken cancellationToken) { await _system.ResetAsync(cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    private static JsonSerializerOptions BackupJsonOptions()
    {
        var options = new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = true };
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }

    private async Task<IReadOnlyList<RiderStatisticsDto>> BuildRiderStatisticsAsync(CancellationToken cancellationToken)
    {
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken);
        var riders = await _riders.ListAsync(true, cancellationToken);
        var stats = new List<RiderStatisticsDto>();
        var handled = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var rider in riders)
        {
            stats.Add(BuildStat(rider.RiderId, rider.DisplayName, runs.Where(run => run.RiderId == rider.RiderId || (run.RiderId is null && string.Equals(run.RiderName, rider.DisplayName, StringComparison.OrdinalIgnoreCase))).ToList()));
            handled.Add(rider.DisplayName);
        }
        stats.AddRange(runs.Where(r => r.RiderId is null && !handled.Contains(r.RiderName)).GroupBy(r => r.RiderName, StringComparer.OrdinalIgnoreCase).Select(g => BuildStat(null, g.Key, g.ToList())));
        return stats.OrderBy(s => s.BestResultMs is null).ThenBy(s => s.BestResultMs).ThenBy(s => s.RiderName).ToList();
    }

    private static RiderStatisticsDto BuildStat(Guid? riderId, string riderName, IReadOnlyList<RunRecord> group)
    {
        var ordered = group.OrderByDescending(run => run.StartTimestampMs).ToList();
        var finished = group.Where(run => run.Status == RunStatus.Finished && run.ResultMs is not null).OrderBy(run => run.ResultMs).ThenBy(run => run.StartTimestampMs).ToList();
        var best = finished.FirstOrDefault();
        var last = ordered.FirstOrDefault();
        var lastFinished = ordered.FirstOrDefault(run => run.Status == RunStatus.Finished && run.ResultMs is not null);
        var average = finished.Count == 0 ? null : (long?)Math.Round(finished.Average(run => run.ResultMs!.Value));
        return new RiderStatisticsDto(riderId, riderName, group.Count, finished.Count, group.Count(run => run.Status == RunStatus.Dnf), best?.ResultMs, best is null ? null : TimeFormatter.FormatResult(best.ResultMs), average, average is null ? null : TimeFormatter.FormatResult(average), lastFinished?.ResultMs, lastFinished is null ? null : TimeFormatter.FormatResult(lastFinished.ResultMs), last?.StartTimestampMs, best?.TrailName ?? string.Empty);
    }

    private static IActionResult CsvFile(string fileName, string csv) => new FileContentResult(Encoding.UTF8.GetPreamble().Concat(Encoding.UTF8.GetBytes(csv)).ToArray(), "text/csv; charset=utf-8") { FileDownloadName = fileName };
    private static string BuildRunsCsv(IEnumerable<RunRecord> runs) => Csv(new[] { "runId", "riderId", "riderName", "trailName", "operationMode", "queuePosition", "sequenceNumber", "startTimestampMs", "finishTimestampMs", "resultMs", "resultFormatted", "status", "isPersonalBest", "createdAtMs" }, runs.OrderByDescending(r => r.StartTimestampMs).Select(r => new[] { r.RunId.ToString(), r.RiderId?.ToString() ?? string.Empty, r.RiderName, r.TrailName, r.OperationMode.ToString(), r.QueuePosition?.ToString() ?? string.Empty, r.SequenceNumber.ToString(), r.StartTimestampMs.ToString(), r.FinishTimestampMs?.ToString() ?? string.Empty, r.ResultMs?.ToString() ?? string.Empty, r.ResultFormatted, r.Status.ToString(), r.IsPersonalBest.ToString(), r.CreatedAtMs.ToString() }));
    private static string BuildRidersCsv(IEnumerable<RegisteredRider> riders) => Csv(new[] { "riderId", "displayName", "rfidTagId", "isActive", "createdAtMs" }, riders.Select(r => new[] { r.RiderId.ToString(), r.DisplayName, r.RfidTagId ?? string.Empty, r.IsActive.ToString(), r.CreatedAtMs.ToString() }));
    private static string BuildStatisticsCsv(IEnumerable<RiderStatisticsDto> stats) => Csv(new[] { "riderId", "riderName", "totalRuns", "finishedRuns", "dnfRuns", "bestResultMs", "bestResultFormatted", "averageResultMs", "averageResultFormatted", "lastResultMs", "lastResultFormatted", "bestTrailName" }, stats.Select(s => new[] { s.RiderId?.ToString() ?? string.Empty, s.RiderName, s.TotalRuns.ToString(), s.FinishedRuns.ToString(), s.DnfRuns.ToString(), s.BestResultMs?.ToString() ?? string.Empty, s.BestResultFormatted ?? string.Empty, s.AverageResultMs?.ToString() ?? string.Empty, s.AverageResultFormatted ?? string.Empty, s.LastResultMs?.ToString() ?? string.Empty, s.LastResultFormatted ?? string.Empty, s.BestTrailName }));
    private static string Csv(string[] header, IEnumerable<string[]> rows) => string.Join('\n', new[] { string.Join(';', header.Select(Escape)) }.Concat(rows.Select(row => string.Join(';', row.Select(Escape))))) + "\n";
    private static string Escape(string? value) { var v = value ?? string.Empty; return v.Contains('"') || v.Contains('\n') || v.Contains('\r') || v.Contains(';') ? $"\"{v.Replace("\"", "\"\"")}\"" : v; }
}

public sealed record StartRunRequest(string? TrailName = null, string? TrackName = null);
public sealed record RiderRequest(string DisplayName, string? RfidTagId = null, bool? IsActive = true);
public sealed record ModeRequest(SystemOperationMode OperationMode);
public sealed record GroupQueueRequest(IReadOnlyList<Guid> RiderIds);
public sealed record QueueIndexRequest(int Index);
public sealed record RfidSimulateRequest(string TagId);
public sealed record TimeOffsetRequest(long RtcOffsetMs);
public sealed record RiderListItemDto(Guid RiderId, string DisplayName, string? RfidTagId, bool IsActive, long CreatedAtMs, int RunCount, long? BestResultMs, string? BestResultFormatted);
public sealed record RiderStatisticsDto(Guid? RiderId, string RiderName, int TotalRuns, int FinishedRuns, int DnfRuns, long? BestResultMs, string? BestResultFormatted, long? AverageResultMs, string? AverageResultFormatted, long? LastResultMs, string? LastResultFormatted, long? LastRunAt, string BestTrailName);

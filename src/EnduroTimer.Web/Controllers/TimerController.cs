using System.IO.Compression;
using System.Text;
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
    private readonly IConfiguration _configuration;
    private readonly ILogger<TimerController> _logger;

    public TimerController(EnduroTimerSystem system, UpperStationService upper, IFinishSensorService finishSensor, IRunRepository runs, IRegisteredRiderRepository riders, IRfidReaderService rfid, IConfiguration configuration, ILogger<TimerController> logger)
    {
        _system = system; _upper = upper; _finishSensor = finishSensor; _runs = runs; _riders = riders; _rfid = rfid; _configuration = configuration; _logger = logger;
    }

    [HttpGet("status")]
    public async Task<ActionResult<SystemStatus>> GetStatus(CancellationToken cancellationToken) => Ok(await _system.GetStatusAsync(cancellationToken));

    [HttpPost("time/sync")]
    public async Task<ActionResult<SystemStatus>> SyncTime(CancellationToken cancellationToken) { await _upper.SyncTimeAsync(cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    [HttpPost("runs/start")]
    public async Task<ActionResult<RunRecord>> StartRun([FromBody] StartRunRequest request, CancellationToken cancellationToken)
    {
        try
        {
            var status = await _system.GetStatusAsync(cancellationToken); if (!status.CanStartRun) return Conflict(new { error = status.StartBlockedReason ?? "Start is blocked" });
            return Ok(await _system.StartRunAsync(request.RiderName ?? request.Rider ?? request.RiderNumber, request.TrailName ?? request.TrackName, cancellationToken));
        }
        catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); }
        catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); }
    }

    [HttpPost("finish/simulate")]
    public async Task<ActionResult<SystemStatus>> SimulateFinish(CancellationToken cancellationToken) { await _finishSensor.TriggerAsync(cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    [HttpGet("runs")]
    public async Task<ActionResult<IReadOnlyList<RunRecord>>> GetRuns(CancellationToken cancellationToken) => Ok(await _runs.ListAsync(50, cancellationToken));

    [HttpGet("runs/{id:guid}")]
    public async Task<ActionResult<RunRecord>> GetRun(Guid id, CancellationToken cancellationToken) => await _runs.GetAsync(id, cancellationToken) is { } run ? Ok(run) : NotFound();

    [HttpPost("runs/{id:guid}/dnf")]
    public async Task<IActionResult> MarkDnf(Guid id, CancellationToken cancellationToken) { try { await _upper.MarkDnfAsync(id, cancellationToken); return NoContent(); } catch (KeyNotFoundException) { return NotFound(); } }

    [HttpGet("riders")]
    public async Task<ActionResult<IReadOnlyList<RiderListItemDto>>> GetRiders(CancellationToken cancellationToken)
    {
        var riders = await _riders.ListAsync(true, cancellationToken); var runs = await _runs.ListAsync(int.MaxValue, cancellationToken);
        return Ok(riders.Select(r => { var riderRuns = runs.Where(run => run.RiderId == r.RiderId || (run.RiderId is null && string.Equals(run.RiderName, r.DisplayName, StringComparison.OrdinalIgnoreCase))).ToList(); var best = riderRuns.Where(run => run.Status == RunStatus.Finished && run.ResultMs is not null).MinBy(run => run.ResultMs); return new RiderListItemDto(r.RiderId, r.DisplayName, r.RfidTagId, r.IsActive, r.CreatedAtMs, riderRuns.Count, best?.ResultMs, best is null ? null : TimeFormatter.FormatResult(best.ResultMs)); }).ToList());
    }

    [HttpPost("riders")]
    public async Task<ActionResult<RegisteredRider>> AddRider([FromBody] RiderRequest request, CancellationToken cancellationToken)
    { try { return Ok(await _riders.AddAsync(request.DisplayName, request.RfidTagId, cancellationToken)); } catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); } catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); } }

    [HttpPut("riders/{id:guid}")]
    public async Task<ActionResult<RegisteredRider>> UpdateRider(Guid id, [FromBody] RiderRequest request, CancellationToken cancellationToken)
    { try { return Ok(await _riders.UpdateAsync(id, request.DisplayName, request.RfidTagId, request.IsActive ?? true, cancellationToken)); } catch (KeyNotFoundException) { return NotFound(); } catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); } catch (InvalidOperationException ex) { return Conflict(new { error = ex.Message }); } }

    [HttpDelete("riders/{id:guid}")]
    public async Task<IActionResult> DeleteRider(Guid id, CancellationToken cancellationToken) { try { await _riders.DeactivateAsync(id, cancellationToken); return NoContent(); } catch (KeyNotFoundException) { return NotFound(); } }
    [HttpPost("riders/{id:guid}/deactivate")]
    public Task<IActionResult> DeactivateRider(Guid id, CancellationToken cancellationToken) => DeleteRider(id, cancellationToken);

    [HttpGet("mode")]
    public ActionResult<object> GetMode() => Ok(new { operationMode = _system.OperationMode });
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
    public async Task<ActionResult<GroupQueueState>> SetQueue([FromBody] GroupQueueRequest request, CancellationToken cancellationToken) { try { return Ok(await _system.SetGroupQueueAsync(request.RiderIds, request.LoopGroupQueue, cancellationToken)); } catch (ArgumentException ex) { return BadRequest(new { error = ex.Message }); } }
    [HttpPost("group-queue/next")]
    public async Task<ActionResult<GroupQueueState>> QueueNext(CancellationToken cancellationToken) => Ok(await _system.MoveGroupQueueNextAsync(cancellationToken));
    [HttpPost("group-queue/reset")]
    public async Task<ActionResult<GroupQueueState>> QueueReset(CancellationToken cancellationToken) => Ok(await _system.ResetGroupQueueAsync(cancellationToken));
    [HttpDelete("group-queue/{index:int}")]
    public async Task<ActionResult<GroupQueueState>> QueueRemove(int index, CancellationToken cancellationToken) => Ok(await _system.RemoveGroupQueueAtAsync(index, cancellationToken));
    [HttpPost("group-queue/remove")]
    public async Task<ActionResult<GroupQueueState>> QueueRemovePost([FromBody] RemoveQueueItemRequest request, CancellationToken cancellationToken) => Ok(await _system.RemoveGroupQueueAtAsync(request.Index, cancellationToken));

    [HttpPost("rfid/simulate")]
    public async Task<ActionResult<object>> SimulateRfid([FromBody] RfidSimulateRequest request, CancellationToken cancellationToken)
    {
        var read = await _system.SimulateRfidAsync(request.TagId, cancellationToken); var rider = await _riders.GetByRfidTagAsync(request.TagId, cancellationToken);
        return Ok(new { tagId = read.TagId, readAtMs = read.ReadAtMs, matchedRiderId = rider?.RiderId, matchedRiderName = rider?.DisplayName, status = await _system.GetStatusAsync(cancellationToken) });
    }

    [HttpGet("rfid/last")]
    public async Task<ActionResult<RfidReadResult?>> GetLastRfid(CancellationToken cancellationToken) => Ok(await _rfid.GetLastReadAsync(cancellationToken));

    [HttpGet("statistics/riders")]
    public async Task<ActionResult<IReadOnlyList<RiderStatisticsDto>>> GetRiderStatistics(CancellationToken cancellationToken) => Ok(await BuildRiderStatisticsAsync(cancellationToken));

    [HttpGet("export/results.xlsx")]
    public async Task<IActionResult> ExportExcel(CancellationToken cancellationToken)
    {
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken); var riders = await _riders.ListAsync(true, cancellationToken); var statistics = await BuildRiderStatisticsAsync(cancellationToken);
        return File(BuildExcel(runs, riders, statistics), "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "enduro-timer-results.xlsx");
    }

    [HttpGet("export/results.pdf")]
    public async Task<IActionResult> ExportPdf(CancellationToken cancellationToken)
    {
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken); var riders = await _riders.ListAsync(true, cancellationToken); var statistics = await BuildRiderStatisticsAsync(cancellationToken);
        var fontPath = _configuration["Pdf:FontPath"]; var fontFamily = _configuration["Pdf:FontFamily"] ?? "NotoSans";
        if (string.IsNullOrWhiteSpace(fontPath) || !System.IO.File.Exists(fontPath)) _logger.LogWarning("PDF Cyrillic font path is not configured. Cyrillic text may render incorrectly.");
        return File(BuildPdf(runs, riders, statistics, fontPath, fontFamily), "application/pdf", "enduro-timer-results.pdf");
    }

    [HttpPost("system/reset")]
    public async Task<ActionResult<SystemStatus>> Reset(CancellationToken cancellationToken) { await _system.ResetAsync(cancellationToken); return Ok(await _system.GetStatusAsync(cancellationToken)); }

    private async Task<IReadOnlyList<RiderStatisticsDto>> BuildRiderStatisticsAsync(CancellationToken cancellationToken)
    {
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken); var riders = await _riders.ListAsync(true, cancellationToken); var stats = new List<RiderStatisticsDto>(); var handled = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var rider in riders) { stats.Add(BuildStat(rider.RiderId, rider.DisplayName, runs.Where(run => run.RiderId == rider.RiderId || (run.RiderId is null && string.Equals(run.RiderName, rider.DisplayName, StringComparison.OrdinalIgnoreCase))).ToList())); handled.Add(rider.DisplayName); }
        stats.AddRange(runs.Where(r => r.RiderId is null && !handled.Contains(r.RiderName)).GroupBy(r => r.RiderName, StringComparer.OrdinalIgnoreCase).Select(g => BuildStat(null, g.Key, g.ToList())));
        return stats.OrderBy(s => s.BestResultMs is null).ThenBy(s => s.BestResultMs).ThenBy(s => s.RiderName).ToList();
    }

    private static RiderStatisticsDto BuildStat(Guid? riderId, string riderName, IReadOnlyList<RunRecord> group)
    {
        var ordered = group.OrderByDescending(run => run.StartTimestampMs).ToList(); var finished = group.Where(run => run.Status == RunStatus.Finished && run.ResultMs is not null).OrderBy(run => run.ResultMs).ThenBy(run => run.StartTimestampMs).ToList(); var best = finished.FirstOrDefault(); var last = ordered.FirstOrDefault(); var lastFinished = ordered.FirstOrDefault(run => run.Status == RunStatus.Finished && run.ResultMs is not null); var average = finished.Count == 0 ? null : (long?)Math.Round(finished.Average(run => run.ResultMs!.Value));
        return new RiderStatisticsDto(riderId, riderName, group.Count, finished.Count, group.Count(run => run.Status == RunStatus.Dnf), best?.ResultMs, best is null ? null : TimeFormatter.FormatResult(best.ResultMs), average, average is null ? null : TimeFormatter.FormatResult(average), lastFinished?.ResultMs, lastFinished is null ? null : TimeFormatter.FormatResult(lastFinished.ResultMs), last?.StartTimestampMs, best?.TrailName ?? string.Empty, best is null ? 0 : 1);
    }

    private static byte[] BuildExcel(IReadOnlyList<RunRecord> runs, IReadOnlyList<RegisteredRider> riders, IReadOnlyList<RiderStatisticsDto> statistics)
    {
        using var stream = new MemoryStream(); using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
        {
            AddZipEntry(archive, "[Content_Types].xml", """
                <?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet3.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>
                """);
            AddZipEntry(archive, "_rels/.rels", """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>""");
            AddZipEntry(archive, "xl/_rels/workbook.xml.rels", """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet3.xml"/><Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>""");
            AddZipEntry(archive, "xl/workbook.xml", """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Results" sheetId="1" r:id="rId1"/><sheet name="Rider Statistics" sheetId="2" r:id="rId2"/><sheet name="Riders" sheetId="3" r:id="rId3"/></sheets></workbook>""");
            AddZipEntry(archive, "xl/styles.xml", """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font></fonts><fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF92D050"/><bgColor indexed="64"/></patternFill></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/><xf numFmtId="0" fontId="0" fillId="2" borderId="0" xfId="0" applyFill="1"/></cellXfs></styleSheet>""");
            AddZipEntry(archive, "xl/worksheets/sheet1.xml", BuildWorksheet(new[] { "Run ID", "Rider ID", "Rider", "Trail", "Mode", "Queue Position", "Status", "Start Time", "Finish Time", "Result", "Result Ms", "Personal Best" }, runs.OrderByDescending(run => run.StartTimestampMs).Select(run => new[] { run.RunId.ToString(), run.RiderId?.ToString() ?? string.Empty, run.RiderName, run.TrailName, run.OperationMode.ToString(), run.QueuePosition?.ToString() ?? string.Empty, run.Status.ToString(), FormatTimestamp(run.StartTimestampMs), FormatTimestamp(run.FinishTimestampMs), TimeFormatter.FormatResult(run.ResultMs), run.ResultMs?.ToString() ?? string.Empty, run.IsPersonalBest ? "Yes" : "No" }), runs.Select(r => r.IsPersonalBest).ToList()));
            AddZipEntry(archive, "xl/worksheets/sheet2.xml", BuildWorksheet(new[] { "Rider ID", "Rider", "Total Runs", "Finished Runs", "DNF Runs", "Best Result", "Average Result", "Last Result", "Best Trail", "Last Run At" }, statistics.Select(stat => new[] { stat.RiderId?.ToString() ?? string.Empty, stat.RiderName, stat.TotalRuns.ToString(), stat.FinishedRuns.ToString(), stat.DnfRuns.ToString(), stat.BestResultFormatted ?? string.Empty, stat.AverageResultFormatted ?? string.Empty, stat.LastResultFormatted ?? string.Empty, stat.BestTrailName, FormatTimestamp(stat.LastRunAt) }), statistics.Select(s => s.BestResultMs is not null).ToList()));
            AddZipEntry(archive, "xl/worksheets/sheet3.xml", BuildWorksheet(new[] { "Rider ID", "Display Name", "RFID Tag", "Active" }, riders.Select(r => new[] { r.RiderId.ToString(), r.DisplayName, r.RfidTagId ?? string.Empty, r.IsActive ? "Yes" : "No" }), riders.Select(r => false).ToList()));
        }
        return stream.ToArray();
    }

    private static string BuildWorksheet(string[] headers, IEnumerable<string[]> rows, IReadOnlyList<bool> greenRows) { var sb = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><cols>"); for (var i = 1; i <= headers.Length; i++) sb.Append($"<col min=\"{i}\" max=\"{i}\" width=\"18\" customWidth=\"1\"/>"); sb.Append("</cols><sheetData>"); AppendRow(sb, 1, headers, 1); var rowIndex = 2; var greenIndex = 0; foreach (var row in rows) AppendRow(sb, rowIndex++, row, greenRows.Count > greenIndex && greenRows[greenIndex++] ? 2 : 0); sb.Append("</sheetData></worksheet>"); return sb.ToString(); }
    private static void AppendRow(StringBuilder sb, int rowIndex, IReadOnlyList<string> values, int style) { sb.Append($"<row r=\"{rowIndex}\">"); for (var i = 0; i < values.Count; i++) sb.Append($"<c r=\"{ColumnName(i + 1)}{rowIndex}\" t=\"inlineStr\" s=\"{style}\"><is><t>{SecurityElementEscape(values[i])}</t></is></c>"); sb.Append("</row>"); }

    private static byte[] BuildPdf(IReadOnlyList<RunRecord> runs, IReadOnlyList<RegisteredRider> riders, IReadOnlyList<RiderStatisticsDto> statistics, string? fontPath, string fontFamily)
    {
        var lines = new List<string> { "Enduro Timer Results", $"Generated: {DateTimeOffset.UtcNow:yyyy-MM-dd HH:mm:ss} UTC", string.Empty, "Results", "Run ID | Rider ID | Rider | Trail | Mode | Queue | Status | Result | PB" };
        lines.AddRange(runs.OrderByDescending(run => run.StartTimestampMs).Select(run => $"{run.RunId.ToString()[..8]} | {Short(run.RiderId)} | {run.RiderName} | {run.TrailName} | {run.OperationMode} | {run.QueuePosition?.ToString() ?? "-"} | {run.Status} | {TimeFormatter.FormatResult(run.ResultMs)} | {(run.IsPersonalBest ? "Yes" : "No")}"));
        lines.AddRange(new[] { string.Empty, "Rider Statistics", "Rider ID | Rider | Total | Finished | DNF | Best | Average | Best Trail" });
        lines.AddRange(statistics.Select(stat => $"{Short(stat.RiderId)} | {stat.RiderName} | {stat.TotalRuns} | {stat.FinishedRuns} | {stat.DnfRuns} | {stat.BestResultFormatted ?? "-"} | {stat.AverageResultFormatted ?? "-"} | {stat.BestTrailName}"));
        lines.AddRange(new[] { string.Empty, "Riders", "Rider ID | Display Name | RFID Tag | Active" });
        lines.AddRange(riders.Select(r => $"{r.RiderId.ToString()[..8]} | {r.DisplayName} | {r.RfidTagId ?? "-"} | {r.IsActive}"));
        return !string.IsNullOrWhiteSpace(fontPath) && System.IO.File.Exists(fontPath) ? SimplePdfWriter.WriteWithExternalFont(lines, System.IO.File.ReadAllBytes(fontPath), fontFamily) : SimplePdfWriter.Write(lines);
    }

    private static void AddZipEntry(ZipArchive archive, string name, string content) { var entry = archive.CreateEntry(name, CompressionLevel.Fastest); using var writer = new StreamWriter(entry.Open(), new UTF8Encoding(false)); writer.Write(content.TrimStart()); }
    private static string ColumnName(int column) { var name = string.Empty; while (column > 0) { column--; name = (char)('A' + column % 26) + name; column /= 26; } return name; }
    private static string Short(Guid? id) => id.HasValue ? id.Value.ToString()[..8] : string.Empty;
    private static string SecurityElementEscape(string? value) => System.Security.SecurityElement.Escape(value ?? string.Empty) ?? string.Empty;
    private static string FormatTimestamp(long? timestampMs) => timestampMs is null or 0 ? string.Empty : DateTimeOffset.FromUnixTimeMilliseconds(timestampMs.Value).ToString("yyyy-MM-dd HH:mm:ss.fff");
}

public sealed record StartRunRequest(string? Rider = null, string? RiderName = null, string? RiderNumber = null, string? TrailName = null, string? TrackName = null);
public sealed record RiderRequest(string DisplayName, string? RfidTagId = null, bool? IsActive = true);
public sealed record ModeRequest(SystemOperationMode OperationMode);
public sealed record GroupQueueRequest(IReadOnlyList<Guid> RiderIds, bool LoopGroupQueue = true);
public sealed record RemoveQueueItemRequest(int Index);
public sealed record RfidSimulateRequest(string TagId);
public sealed record RiderListItemDto(Guid RiderId, string DisplayName, string? RfidTagId, bool IsActive, long CreatedAtMs, int RunCount, long? BestResultMs, string? BestResultFormatted);

public sealed record RiderStatisticsDto(Guid? RiderId, string RiderName, int TotalRuns, int FinishedRuns, int DnfRuns, long? BestResultMs, string? BestResultFormatted, long? AverageResultMs, string? AverageResultFormatted, long? LastResultMs, string? LastResultFormatted, long? LastRunAt, string BestTrailName, int PersonalBestCount);

internal static class SimplePdfWriter
{
    public static byte[] Write(IReadOnlyList<string> lines) => WriteCore(lines, null, "Helvetica");
    public static byte[] WriteWithExternalFont(IReadOnlyList<string> lines, byte[] fontBytes, string fontFamily) => WriteCore(lines, fontBytes, string.IsNullOrWhiteSpace(fontFamily) ? "ExternalFont" : fontFamily);

    private static byte[] WriteCore(IReadOnlyList<string> lines, byte[]? fontBytes, string fontFamily)
    {
        var objects = new List<byte[]>(); void AddText(string s) => objects.Add(Encoding.ASCII.GetBytes(s));
        AddText("<< /Type /Catalog /Pages 2 0 R >>"); AddText("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
        if (fontBytes is null)
        {
            AddText("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>"); AddText("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"); AddText(ContentStream(lines, false));
        }
        else
        {
            AddText("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /Font << /F1 4 0 R >> >> /Contents 9 0 R >>");
            AddText("<< /Type /Font /Subtype /Type0 /BaseFont /" + PdfName(fontFamily) + " /Encoding /Identity-H /DescendantFonts [5 0 R] /ToUnicode 8 0 R >>");
            AddText("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + PdfName(fontFamily) + " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor 6 0 R /CIDToGIDMap /Identity /DW 600 >>");
            AddText("<< /Type /FontDescriptor /FontName /" + PdfName(fontFamily) + " /Flags 32 /Ascent 900 /Descent -250 /CapHeight 700 /ItalicAngle 0 /StemV 80 /FontBBox [-1000 -300 2000 1000] /FontFile2 7 0 R >>");
            objects.Add(StreamObject(fontBytes)); AddText(ToUnicodeCMap()); AddText(ContentStream(lines, true));
        }
        using var stream = new MemoryStream(); using var writer = new StreamWriter(stream, new UTF8Encoding(false), 1024, true); writer.Write("%PDF-1.4\n"); writer.Flush(); var offsets = new List<long> { 0 };
        for (var i = 0; i < objects.Count; i++) { offsets.Add(stream.Position); writer.Write($"{i + 1} 0 obj\n"); writer.Flush(); stream.Write(objects[i]); writer.Write("\nendobj\n"); writer.Flush(); }
        var xref = stream.Position; writer.Write($"xref\n0 {objects.Count + 1}\n0000000000 65535 f \n"); foreach (var offset in offsets.Skip(1)) writer.Write($"{offset:0000000000} 00000 n \n"); writer.Write($"trailer << /Size {objects.Count + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF"); writer.Flush(); return stream.ToArray();
    }

    private static byte[] StreamObject(byte[] bytes) { var prefix = Encoding.ASCII.GetBytes($"<< /Length {bytes.Length} >>\nstream\n"); var suffix = Encoding.ASCII.GetBytes("\nendstream"); var result = new byte[prefix.Length + bytes.Length + suffix.Length]; Buffer.BlockCopy(prefix, 0, result, 0, prefix.Length); Buffer.BlockCopy(bytes, 0, result, prefix.Length, bytes.Length); Buffer.BlockCopy(suffix, 0, result, prefix.Length + bytes.Length, suffix.Length); return result; }
    private static string ContentStream(IReadOnlyList<string> lines, bool unicode) { var content = new StringBuilder("BT /F1 8 Tf 36 560 Td 10 TL "); foreach (var line in lines.Take(52)) content.Append(unicode ? $"<{ToUtf16Hex(line.Length > 150 ? line[..150] : line)}> Tj T* " : $"({EscapePdf(line.Length > 145 ? line[..145] : line)}) Tj T* "); content.Append("ET"); var bytes = Encoding.UTF8.GetBytes(content.ToString()); return $"<< /Length {bytes.Length} >>\nstream\n{content}\nendstream"; }
    private static string ToUnicodeCMap() { const string cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n1 beginbfrange\n<0000> <FFFF> <0000>\nendbfrange\nendcmap\nCMapName currentdict /CMap defineresource pop\nend end"; return $"<< /Length {Encoding.ASCII.GetByteCount(cmap)} >>\nstream\n{cmap}\nendstream"; }
    private static string ToUtf16Hex(string value) => string.Concat(Encoding.BigEndianUnicode.GetBytes(value).Select(b => b.ToString("X2")));
    private static string EscapePdf(string value) => value.Replace("\\", "\\\\").Replace("(", "\\(").Replace(")", "\\)");
    private static string PdfName(string value) => new(value.Where(char.IsLetterOrDigit).DefaultIfEmpty('F').ToArray());
}

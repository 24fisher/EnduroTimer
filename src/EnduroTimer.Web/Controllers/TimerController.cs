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

    public TimerController(EnduroTimerSystem system, UpperStationService upper, IFinishSensorService finishSensor, IRunRepository runs)
    {
        _system = system;
        _upper = upper;
        _finishSensor = finishSensor;
        _runs = runs;
    }

    [HttpGet("status")]
    public async Task<ActionResult<SystemStatus>> GetStatus(CancellationToken cancellationToken) =>
        Ok(await _system.GetStatusAsync(cancellationToken));

    [HttpPost("time/sync")]
    public async Task<ActionResult<SystemStatus>> SyncTime(CancellationToken cancellationToken)
    {
        await _upper.SyncTimeAsync(cancellationToken);
        return Ok(await _system.GetStatusAsync(cancellationToken));
    }

    [HttpPost("runs/start")]
    public async Task<ActionResult<RunRecord>> StartRun([FromBody] StartRunRequest request, CancellationToken cancellationToken)
    {
        try
        {
            var riderName = request.RiderName ?? request.Rider ?? request.RiderNumber;
            if (string.IsNullOrWhiteSpace(riderName))
            {
                throw new ArgumentException("Rider name is required", nameof(request));
            }

            var status = await _system.GetStatusAsync(cancellationToken);
            if (!status.CanStartRun)
            {
                return Conflict(new { error = status.StartBlockedReason ?? "Start is blocked" });
            }

            return Ok(await _upper.StartRunAsync(riderName, request.TrailName ?? request.TrackName, cancellationToken));
        }
        catch (InvalidOperationException ex) when (ex.Message == "Run already active" || ex.Message == "Time synchronization required before starting a run")
        {
            return Conflict(new { error = ex.Message });
        }
        catch (ArgumentException ex)
        {
            return BadRequest(new { error = ex.Message });
        }
    }

    [HttpPost("finish/simulate")]
    public async Task<ActionResult<SystemStatus>> SimulateFinish(CancellationToken cancellationToken)
    {
        await _finishSensor.TriggerAsync(cancellationToken);
        return Ok(await _system.GetStatusAsync(cancellationToken));
    }

    [HttpGet("runs")]
    public async Task<ActionResult<IReadOnlyList<RunRecord>>> GetRuns(CancellationToken cancellationToken) =>
        Ok(await _runs.ListAsync(50, cancellationToken));

    [HttpGet("runs/{id:guid}")]
    public async Task<ActionResult<RunRecord>> GetRun(Guid id, CancellationToken cancellationToken)
    {
        var run = await _runs.GetAsync(id, cancellationToken);
        return run is null ? NotFound() : Ok(run);
    }

    [HttpPost("runs/{id:guid}/dnf")]
    public async Task<IActionResult> MarkDnf(Guid id, CancellationToken cancellationToken)
    {
        try
        {
            await _upper.MarkDnfAsync(id, cancellationToken);
            return NoContent();
        }
        catch (KeyNotFoundException)
        {
            return NotFound();
        }
    }

    [HttpGet("statistics/riders")]
    public async Task<ActionResult<IReadOnlyList<RiderStatisticsDto>>> GetRiderStatistics(CancellationToken cancellationToken) =>
        Ok(BuildRiderStatistics(await _runs.ListAsync(int.MaxValue, cancellationToken)));

    [HttpGet("export/results.xlsx")]
    public async Task<IActionResult> ExportExcel(CancellationToken cancellationToken)
    {
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken);
        var statistics = BuildRiderStatistics(runs);
        return File(BuildExcel(runs, statistics), "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "enduro-timer-results.xlsx");
    }

    [HttpGet("export/results.pdf")]
    public async Task<IActionResult> ExportPdf(CancellationToken cancellationToken)
    {
        var runs = await _runs.ListAsync(int.MaxValue, cancellationToken);
        var statistics = BuildRiderStatistics(runs);
        return File(BuildPdf(runs, statistics), "application/pdf", "enduro-timer-results.pdf");
    }

    [HttpPost("system/reset")]
    public async Task<ActionResult<SystemStatus>> Reset(CancellationToken cancellationToken)
    {
        await _system.ResetAsync(cancellationToken);
        return Ok(await _system.GetStatusAsync(cancellationToken));
    }

    private static IReadOnlyList<RiderStatisticsDto> BuildRiderStatistics(IReadOnlyList<RunRecord> runs) => runs
        .GroupBy(run => run.RiderName, StringComparer.OrdinalIgnoreCase)
        .Select(group =>
        {
            var ordered = group.OrderByDescending(run => run.StartTimestampMs).ToList();
            var finished = group
                .Where(run => run.Status == RunStatus.Finished && run.ResultMs is not null)
                .OrderBy(run => run.ResultMs!.Value)
                .ThenBy(run => run.StartTimestampMs)
                .ToList();
            var best = finished.FirstOrDefault();
            var last = ordered.FirstOrDefault();
            var lastFinished = ordered.FirstOrDefault(run => run.Status == RunStatus.Finished && run.ResultMs is not null);
            var average = finished.Count == 0 ? null : (long?)Math.Round(finished.Average(run => run.ResultMs!.Value));

            return new RiderStatisticsDto(
                RiderName: group.Key,
                TotalRuns: group.Count(),
                FinishedRuns: finished.Count,
                DnfRuns: group.Count(run => run.Status == RunStatus.Dnf),
                BestResultMs: best?.ResultMs,
                BestResultFormatted: best is null ? null : TimeFormatter.FormatResult(best.ResultMs),
                AverageResultMs: average,
                AverageResultFormatted: average is null ? null : TimeFormatter.FormatResult(average),
                LastResultMs: lastFinished?.ResultMs,
                LastResultFormatted: lastFinished is null ? null : TimeFormatter.FormatResult(lastFinished.ResultMs),
                LastRunAt: last?.StartTimestampMs,
                BestTrailName: best?.TrailName ?? string.Empty,
                PersonalBestCount: best is null ? 0 : 1);
        })
        .OrderBy(dto => dto.BestResultMs is null)
        .ThenBy(dto => dto.BestResultMs)
        .ThenByDescending(dto => dto.FinishedRuns)
        .ToList();

    private static byte[] BuildExcel(IReadOnlyList<RunRecord> runs, IReadOnlyList<RiderStatisticsDto> statistics)
    {
        using var stream = new MemoryStream();
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
        {
            AddZipEntry(archive, "[Content_Types].xml", """
                <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                <Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
                  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
                  <Default Extension="xml" ContentType="application/xml"/>
                  <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
                  <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
                  <Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
                  <Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
                </Types>
                """);
            AddZipEntry(archive, "_rels/.rels", """
                <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
                  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
                </Relationships>
                """);
            AddZipEntry(archive, "xl/_rels/workbook.xml.rels", """
                <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
                  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
                  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>
                  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
                </Relationships>
                """);
            AddZipEntry(archive, "xl/workbook.xml", """
                <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                <workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
                  <sheets><sheet name="Results" sheetId="1" r:id="rId1"/><sheet name="Rider Statistics" sheetId="2" r:id="rId2"/></sheets>
                </workbook>
                """);
            AddZipEntry(archive, "xl/styles.xml", """
                <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                <styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
                  <fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font></fonts>
                  <fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF92D050"/><bgColor indexed="64"/></patternFill></fill></fills>
                  <borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
                  <cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
                  <cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/><xf numFmtId="0" fontId="0" fillId="2" borderId="0" xfId="0" applyFill="1"/></cellXfs>
                </styleSheet>
                """);

            var resultRows = runs.OrderByDescending(run => run.StartTimestampMs).Select(run => new[]
            {
                run.RunId.ToString(), run.RiderName, run.TrailName, run.Status.ToString(), FormatTimestamp(run.StartTimestampMs), FormatTimestamp(run.FinishTimestampMs),
                TimeFormatter.FormatResult(run.ResultMs), run.ResultMs?.ToString() ?? string.Empty, run.IsPersonalBest ? "Yes" : "No"
            });
            AddZipEntry(archive, "xl/worksheets/sheet1.xml", BuildWorksheet(new[] { "Run ID", "Rider", "Trail", "Status", "Start Time", "Finish Time", "Result", "Result Ms", "Personal Best" }, resultRows, runs.Select(r => r.IsPersonalBest).ToList()));

            var statRows = statistics.Select(stat => new[]
            {
                stat.RiderName, stat.TotalRuns.ToString(), stat.FinishedRuns.ToString(), stat.DnfRuns.ToString(), stat.BestResultFormatted ?? string.Empty,
                stat.AverageResultFormatted ?? string.Empty, stat.LastResultFormatted ?? string.Empty, stat.BestTrailName, FormatTimestamp(stat.LastRunAt)
            });
            AddZipEntry(archive, "xl/worksheets/sheet2.xml", BuildWorksheet(new[] { "Rider", "Total Runs", "Finished Runs", "DNF Runs", "Best Result", "Average Result", "Last Result", "Best Trail", "Last Run At" }, statRows, statistics.Select(s => s.BestResultMs is not null).ToList()));
        }

        return stream.ToArray();
    }

    private static string BuildWorksheet(string[] headers, IEnumerable<string[]> rows, IReadOnlyList<bool> greenRows)
    {
        var sb = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><cols>");
        for (var i = 1; i <= headers.Length; i++) sb.Append($"<col min=\"{i}\" max=\"{i}\" width=\"18\" customWidth=\"1\"/>");
        sb.Append("</cols><sheetData>");
        AppendRow(sb, 1, headers, 1);
        var rowIndex = 2;
        var greenIndex = 0;
        foreach (var row in rows)
        {
            AppendRow(sb, rowIndex++, row, greenRows.Count > greenIndex && greenRows[greenIndex++] ? 2 : 0);
        }
        sb.Append("</sheetData></worksheet>");
        return sb.ToString();
    }

    private static void AppendRow(StringBuilder sb, int rowIndex, IReadOnlyList<string> values, int style)
    {
        sb.Append($"<row r=\"{rowIndex}\">");
        for (var i = 0; i < values.Count; i++)
        {
            sb.Append($"<c r=\"{ColumnName(i + 1)}{rowIndex}\" t=\"inlineStr\" s=\"{style}\"><is><t>{SecurityElementEscape(values[i])}</t></is></c>");
        }
        sb.Append("</row>");
    }

    private static byte[] BuildPdf(IReadOnlyList<RunRecord> runs, IReadOnlyList<RiderStatisticsDto> statistics)
    {
        var lines = new List<string>
        {
            "Enduro Timer Results",
            $"Generated: {DateTimeOffset.UtcNow:yyyy-MM-dd HH:mm:ss} UTC",
            string.Empty,
            "Results",
            "Run ID | Rider | Trail | Status | Result | Personal Best"
        };
        lines.AddRange(runs.OrderByDescending(run => run.StartTimestampMs).Select(run => $"{run.RunId.ToString()[..8]} | {run.RiderName} | {run.TrailName} | {run.Status} | {TimeFormatter.FormatResult(run.ResultMs)} | {(run.IsPersonalBest ? "Yes" : "No")}"));
        lines.AddRange(new[] { string.Empty, "Rider Statistics", "Rider | Finished | DNF | Best | Average | Best Trail" });
        lines.AddRange(statistics.Select(stat => $"{stat.RiderName} | {stat.FinishedRuns} | {stat.DnfRuns} | {stat.BestResultFormatted ?? "-"} | {stat.AverageResultFormatted ?? "-"} | {stat.BestTrailName}"));
        return SimplePdfWriter.Write(lines);
    }

    private static void AddZipEntry(ZipArchive archive, string name, string content)
    {
        var entry = archive.CreateEntry(name, CompressionLevel.Fastest);
        using var writer = new StreamWriter(entry.Open(), new UTF8Encoding(false));
        writer.Write(content.TrimStart());
    }

    private static string ColumnName(int column)
    {
        var name = string.Empty;
        while (column > 0)
        {
            column--;
            name = (char)('A' + column % 26) + name;
            column /= 26;
        }
        return name;
    }

    private static string SecurityElementEscape(string? value) => System.Security.SecurityElement.Escape(value ?? string.Empty) ?? string.Empty;
    private static string FormatTimestamp(long? timestampMs) => timestampMs is null or 0 ? string.Empty : DateTimeOffset.FromUnixTimeMilliseconds(timestampMs.Value).ToString("yyyy-MM-dd HH:mm:ss.fff");
}

public sealed record StartRunRequest(string? Rider = null, string? RiderName = null, string? RiderNumber = null, string? TrailName = null, string? TrackName = null);

public sealed record RiderStatisticsDto(
    string RiderName,
    int TotalRuns,
    int FinishedRuns,
    int DnfRuns,
    long? BestResultMs,
    string? BestResultFormatted,
    long? AverageResultMs,
    string? AverageResultFormatted,
    long? LastResultMs,
    string? LastResultFormatted,
    long? LastRunAt,
    string BestTrailName,
    int PersonalBestCount);

internal static class SimplePdfWriter
{
    public static byte[] Write(IReadOnlyList<string> lines)
    {
        var objects = new List<string>();
        objects.Add("<< /Type /Catalog /Pages 2 0 R >>");
        objects.Add("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
        objects.Add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>");
        objects.Add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

        var content = new StringBuilder("BT /F1 9 Tf 36 560 Td 11 TL ");
        foreach (var line in lines.Take(44))
        {
            content.Append('(').Append(EscapePdf(line.Length > 145 ? line[..145] : line)).Append(") Tj T* ");
        }
        content.Append("ET");
        var contentBytes = Encoding.UTF8.GetBytes(content.ToString());
        objects.Add($"<< /Length {contentBytes.Length} >>\nstream\n{content}\nendstream");

        using var stream = new MemoryStream();
        using var writer = new StreamWriter(stream, new UTF8Encoding(false), bufferSize: 1024, leaveOpen: true);
        writer.Write("%PDF-1.4\n");
        var offsets = new List<long> { 0 };
        for (var i = 0; i < objects.Count; i++)
        {
            writer.Flush();
            offsets.Add(stream.Position);
            writer.Write($"{i + 1} 0 obj\n{objects[i]}\nendobj\n");
        }
        writer.Flush();
        var xref = stream.Position;
        writer.Write($"xref\n0 {objects.Count + 1}\n0000000000 65535 f \n");
        foreach (var offset in offsets.Skip(1)) writer.Write($"{offset:0000000000} 00000 n \n");
        writer.Write($"trailer << /Size {objects.Count + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF");
        writer.Flush();
        return stream.ToArray();
    }

    private static string EscapePdf(string value) => value.Replace("\\", "\\\\").Replace("(", "\\(").Replace(")", "\\)");
}

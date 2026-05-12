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
            return Ok(await _upper.StartRunAsync(request.Rider, cancellationToken));
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
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

    [HttpPost("system/reset")]
    public async Task<ActionResult<SystemStatus>> Reset(CancellationToken cancellationToken)
    {
        await _system.ResetAsync(cancellationToken);
        return Ok(await _system.GetStatusAsync(cancellationToken));
    }
}

public sealed record StartRunRequest(string Rider);

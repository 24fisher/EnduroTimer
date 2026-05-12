using System.Text.Json.Nodes;
using EnduroTimer.Core.Models;

namespace EnduroTimer.Core.Protocol;

public sealed record RadioMessage(
    Guid MessageId,
    RadioMessageType Type,
    string StationId,
    Guid? RunId,
    long? TimestampMs,
    JsonObject Payload)
{
    public static RadioMessage Create(
        RadioMessageType type,
        string stationId,
        Guid? runId = null,
        long? timestampMs = null,
        JsonObject? payload = null) =>
        new(Guid.NewGuid(), type, stationId, runId, timestampMs, payload ?? new JsonObject());
}

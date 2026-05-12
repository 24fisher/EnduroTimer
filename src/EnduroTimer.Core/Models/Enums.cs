namespace EnduroTimer.Core.Models;

public enum UpperStationState
{
    Boot,
    Ready,
    Countdown,
    Riding,
    Finished,
    Error
}

public enum LowerStationState
{
    Boot,
    Idle,
    WaitFinish,
    Finished,
    SensorBlocked,
    Offline
}

public enum RunStatus
{
    Pending,
    Riding,
    Finished,
    Dnf
}

public enum RadioMessageType
{
    Ping,
    Pong,
    SyncTime,
    SyncTimeAck,
    RunStart,
    Finish,
    FinishAck,
    Status
}


public enum SystemOperationMode
{
    ManualEncoderSelection,
    GroupQueue
}

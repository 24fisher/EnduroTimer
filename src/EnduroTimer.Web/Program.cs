using System.Text.Json.Serialization;
using EnduroTimer.Core.Abstractions;
using EnduroTimer.Core.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddControllers().AddJsonOptions(options =>
{
    options.JsonSerializerOptions.Converters.Add(new JsonStringEnumConverter());
});
builder.Services.AddEndpointsApiExplorer();
builder.Services.AddSingleton(sp =>
{
    var env = sp.GetRequiredService<IHostEnvironment>();
    var configured = sp.GetRequiredService<IConfiguration>()["Data:Path"] ?? "data";
    var path = Path.IsPathRooted(configured) ? configured : Path.Combine(env.ContentRootPath, configured);
    return new DataDirectory(path);
});
builder.Services.AddSingleton<IClockService>(_ => new SystemClockService());
builder.Services.AddSingleton<IRadioTransport, InMemoryRadioTransport>();
builder.Services.AddSingleton<IRunRepository, FileRunRepository>();
builder.Services.AddSingleton<IRegisteredRiderRepository, FileRegisteredRiderRepository>();
builder.Services.AddSingleton<ISystemSettingsRepository, FileSystemSettingsRepository>();
builder.Services.AddSingleton<IGroupQueueRepository, FileGroupQueueRepository>();
builder.Services.AddSingleton<ILedDisplayService, SimulatedLedDisplayService>();
builder.Services.AddSingleton<IRfidReaderService, SimulatedRfidReaderService>();
builder.Services.AddSingleton<IBuzzerService, ConsoleBuzzerService>();
builder.Services.AddSingleton<LowerStationService>(sp => new LowerStationService(new SystemClockService(offsetMs: 25), sp.GetRequiredService<IRadioTransport>()));
builder.Services.AddSingleton<UpperStationService>();
builder.Services.AddSingleton<IStartButtonService>(sp => sp.GetRequiredService<UpperStationService>());
builder.Services.AddSingleton<IFinishSensorService>(sp => sp.GetRequiredService<LowerStationService>());
builder.Services.AddSingleton<EnduroTimerSystem>();

var app = builder.Build();
app.UseDefaultFiles();
app.UseStaticFiles();
app.MapControllers();
app.MapFallbackToFile("index.html");
app.Run();

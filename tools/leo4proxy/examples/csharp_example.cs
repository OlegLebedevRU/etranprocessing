// ============================================================================
// Leo4 IoT C# Client (Main Application / main_app role)
// Connects to local Mosquitto Bridge (127.0.0.1:1883) via plain TCP (No-SSL)
// Upstream traffic is proxied through Leo4Proxy (SChannel mTLS) to dev.leo4.ru
// ============================================================================

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Packets;
using MQTTnet.Protocol;

namespace Leo4.Client.MainApp;

public class Program
{
    private static readonly string ProxyHttpUrl = Environment.GetEnvironmentVariable("LEO4_PROXY_HTTP") ?? "http://127.0.0.1:18443";
    private static readonly string MqttHost = Environment.GetEnvironmentVariable("MQTT_HOST") ?? "127.0.0.1";
    private static readonly int MqttPort = int.TryParse(Environment.GetEnvironmentVariable("MQTT_PORT"), out var p) ? p : 1883;
    private static string _mqttRole = Environment.GetEnvironmentVariable("MQTT_ROLE") ?? "main_app";
    private static string _mqttUsername = Environment.GetEnvironmentVariable("MQTT_USERNAME") ?? _mqttRole;
    private static readonly string MqttPassword = Environment.GetEnvironmentVariable("MQTT_PASSWORD") ?? "";
    private static readonly int PollIntervalSec = int.TryParse(Environment.GetEnvironmentVariable("MQTT_POLL_INTERVAL_SEC"), out var pi) ? pi : 60;
    private static readonly bool SendTestEvents = Environment.GetEnvironmentVariable("MQTT_SEND_TEST_EVENTS") == "1";
    private static readonly int MaxIterations = int.TryParse(Environment.GetEnvironmentVariable("MQTT_MAX_ITERATIONS"), out var mi) ? mi : 0;

    private static IMqttClient? _mqttClient;
    private static string _deviceSn = "a4b0000773c82116d210826";
    private static readonly HttpClient _httpClient = new() { Timeout = TimeSpan.FromSeconds(5) };

    public static async Task Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;

        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "--role" && i + 1 < args.Length)
            {
                _mqttRole = args[++i];
                _mqttUsername = _mqttRole;
            }
            else if (args[i] == "--once" || args[i] == "-1")
            {
                // MaxIterations = 1
            }
        }

        Console.WriteLine("================================================================");
        Console.WriteLine($"  Leo4 IoT C# Client ({_mqttRole} role)");
        Console.WriteLine("  Target: Mosquitto Bridge (127.0.0.1:1883, No-SSL Plain TCP)");
        Console.WriteLine("================================================================\n");

        using var cts = new CancellationTokenSource();
        Console.CancelKeyPress += (s, e) =>
        {
            e.Cancel = true;
            cts.Cancel();
            Console.WriteLine("\n[SHUTDOWN] Cancellation requested, stopping...");
        };

        // 1. Resolve Device SN & Metadata
        await ResolveDeviceMetadataAsync();

        // 2. Perform Optional Backend License Check via HTTP Proxy
        await CheckLicenseBillingAsync();

        // Determine Presence Topic and Payloads according to AGENTS.md
        var isMainApp = string.Equals(_mqttRole, "main_app", StringComparison.OrdinalIgnoreCase);
        var presenceTopic = isMainApp ? $"dev/{_deviceSn}/app" : $"dev/{_deviceSn}/svc";
        var onlinePayload = isMainApp ? "app_online" : "svc_online";
        var offlinePayload = isMainApp ? "app_offline" : "svc_offline";

        // 3. Connect to MQTT Broker (Mosquitto Bridge on 1883)
        var factory = new MqttFactory();
        _mqttClient = factory.CreateMqttClient();

        var clientId = Environment.GetEnvironmentVariable("MQTT_CLIENT_ID") ?? $"{_deviceSn}_{(isMainApp ? "main_cs" : "extra_cs")}";

        var optionsBuilder = new MqttClientOptionsBuilder()
            .WithTcpServer(MqttHost, MqttPort)
            .WithProtocolVersion(MQTTnet.Formatter.MqttProtocolVersion.V500)
            .WithClientId(clientId)
            .WithCleanSession(true)
            .WithKeepAlivePeriod(TimeSpan.FromSeconds(60))
            .WithTimeout(TimeSpan.FromSeconds(10))
            .WithWillTopic(presenceTopic)
            .WithWillPayload(Encoding.UTF8.GetBytes(offlinePayload))
            .WithWillQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
            .WithWillRetain(true);

        if (!string.IsNullOrEmpty(_mqttUsername))
        {
            optionsBuilder.WithCredentials(_mqttUsername, string.IsNullOrEmpty(MqttPassword) ? null : MqttPassword);
        }

        var clientOptions = optionsBuilder.Build();
        Console.WriteLine($"[MQTT] Configured LWT: {presenceTopic} -> {offlinePayload} (retain=true)");

        _mqttClient.ApplicationMessageReceivedAsync += OnMessageReceivedAsync;

        _mqttClient.ConnectedAsync += async e =>
        {
            Console.WriteLine($"[MQTT] Connected successfully to {MqttHost}:{MqttPort} as '{_mqttUsername}'!");

            // Publish presence: online (retain=true) immediately after CONNACK
            Console.WriteLine($"[PRESENCE] Publishing status: {presenceTopic} = {onlinePayload} (retain=true)");
            var onlineMsg = new MqttApplicationMessageBuilder()
                .WithTopic(presenceTopic)
                .WithPayload(Encoding.UTF8.GetBytes(onlinePayload))
                .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                .WithRetainFlag(true)
                .Build();
            await _mqttClient.PublishAsync(onlineMsg);

            // Subscribe to all inbound server topics for this terminal
            var subTopic = $"srv/{_deviceSn}/#";
            Console.WriteLine($"[MQTT] Subscribing to: {subTopic}");
            await _mqttClient.SubscribeAsync(new MqttTopicFilterBuilder()
                .WithTopic(subTopic)
                .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                .Build());
        };

        _mqttClient.DisconnectedAsync += e =>
        {
            Console.WriteLine($"[MQTT] Disconnected from broker: {e.Reason} ({e.ReasonString})");
            return Task.CompletedTask;
        };

        Console.WriteLine($"[CLIENT] Connecting to Mosquitto Bridge at {MqttHost}:{MqttPort} (user: '{_mqttUsername}')...");
        try
        {
            await _mqttClient.ConnectAsync(clientOptions, cts.Token);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[WARN] Could not connect immediately: {ex.Message}. Background loop will retry.");
        }

        // 4. Main Polling & Task Loop
        int iteration = 0;
        while (!cts.Token.IsCancellationRequested)
        {
            try
            {
                if (!_mqttClient.IsConnected)
                {
                    Console.WriteLine($"[CLIENT] Waiting for broker connection ({MqttHost}:{MqttPort})...");
                    try
                    {
                        await _mqttClient.ConnectAsync(clientOptions, cts.Token);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"[CLIENT] Reconnect attempt failed: {ex.Message}");
                    }
                    await Task.Delay(2000, cts.Token);
                    continue;
                }

                iteration++;

                // Send polling RPC request: dev/<SN>/req
                var zeroCorr = Guid.Empty.ToString();
                var pollMsg = new MqttApplicationMessageBuilder()
                    .WithTopic($"dev/{_deviceSn}/req")
                    .WithPayload(Encoding.UTF8.GetBytes($"poll req = {iteration}"))
                    .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtMostOnce)
                    .WithUserProperty("correlationData", zeroCorr)
                    .WithCorrelationData(Encoding.UTF8.GetBytes(zeroCorr))
                    .Build();

                await _mqttClient.PublishAsync(pollMsg, cts.Token);
                Console.WriteLine($"[POLL] Sent poll request #{iteration} to dev/{_deviceSn}/req");

                // If test event publication is enabled, send sample event
                if (SendTestEvents)
                {
                    await SendSampleEventAsync(iteration, cts.Token);
                }

                if (MaxIterations > 0 && iteration >= MaxIterations)
                {
                    Console.WriteLine($"[INFO] Reached max iterations ({MaxIterations}), stopping.");
                    break;
                }

                await Task.Delay(TimeSpan.FromSeconds(PollIntervalSec), cts.Token);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Error in main loop: {ex.Message}");
                await Task.Delay(3000, cts.Token);
            }
        }

        // Clean disconnect: publish offline presence with retain=true before disconnect
        if (_mqttClient != null && _mqttClient.IsConnected)
        {
            Console.WriteLine($"[PRESENCE] Publishing shutdown status: {presenceTopic} = {offlinePayload} (retain=true)");
            var offlineMsg = new MqttApplicationMessageBuilder()
                .WithTopic(presenceTopic)
                .WithPayload(Encoding.UTF8.GetBytes(offlinePayload))
                .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                .WithRetainFlag(true)
                .Build();
            await _mqttClient.PublishAsync(offlineMsg);
            await Task.Delay(200);

            Console.WriteLine("[SHUTDOWN] Disconnecting from MQTT broker...");
            await _mqttClient.DisconnectAsync(new MqttClientDisconnectOptionsBuilder().Build());
        }
        Console.WriteLine($"[SUCCESS] C# Client ({_mqttRole}) completed.");
    }

    private static async Task ResolveDeviceMetadataAsync()
    {
        Console.WriteLine($"[1] Resolving Device Metadata via Leo4Proxy ({ProxyHttpUrl}/_leo4/info)...");
        try
        {
            var infoJson = await _httpClient.GetStringAsync($"{ProxyHttpUrl}/_leo4/info");
            using var doc = JsonDocument.Parse(infoJson);
            var root = doc.RootElement;
            if (root.TryGetProperty("sn", out var snElem) && !string.IsNullOrEmpty(snElem.GetString()))
            {
                _deviceSn = snElem.GetString()!;
                Console.WriteLine($"    Discovered SN:  {_deviceSn}");
                Console.WriteLine($"    Email:          {root.GetProperty("email").GetString()}");
                Console.WriteLine($"    Status:         {root.GetProperty("status").GetString()}");
                return;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"    [NOTE] Could not query local proxy info ({ex.Message})");
        }

        var envSn = Environment.GetEnvironmentVariable("DEVICE_SN") ?? Environment.GetEnvironmentVariable("MQTT_SN");
        if (!string.IsNullOrEmpty(envSn))
        {
            _deviceSn = envSn;
        }
        Console.WriteLine($"    Using configured Device SN: {_deviceSn}");
    }

    private static async Task CheckLicenseBillingAsync()
    {
        Console.WriteLine($"\n[2] Performing mTLS LicenseBilling check via proxy ({ProxyHttpUrl}/licensebilling/)...");
        try
        {
            var formContent = new FormUrlEncodedContent(new[]
            {
                new KeyValuePair<string, string>("function", "check"),
                new KeyValuePair<string, string>("Signature", "TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=")
            });

            var response = await _httpClient.PostAsync($"{ProxyHttpUrl}/licensebilling/", formContent);
            var body = await response.Content.ReadAsStringAsync();
            Console.WriteLine($"    HTTP Status:    {(int)response.StatusCode}");
            Console.WriteLine($"    Response Body:  {body.Trim()}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"    [WARN] Backend check skipped: {ex.Message}");
        }
    }

    private static async Task SendSampleEventAsync(int iteration, CancellationToken ct)
    {
        var devEventId = 36823 + iteration;
        var corrId = Guid.NewGuid().ToString();
        var nowTs = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        var isoTime = DateTimeOffset.Now.ToString("yyyy-MM-ddTHH:mm:sszzz");

        var payloadObj = new
        {
            __101 = devEventId,
            __102 = isoTime,
            __200 = 888,
            __300 = new[]
            {
                new { __301 = "044AFE42C76781", __302 = 6, __303 = 0 }
            }
        };

        // Serialize with numeric keys: "101", "102", "200", "300"
        var json = JsonSerializer.Serialize(new Dictionary<string, object>
        {
            ["101"] = devEventId,
            ["102"] = isoTime,
            ["200"] = 888,
            ["300"] = new[]
            {
                new Dictionary<string, object> { ["301"] = "044AFE42C76781", ["302"] = 6, ["303"] = 0 }
            }
        });

        var eventMsg = new MqttApplicationMessageBuilder()
            .WithTopic($"dev/{_deviceSn}/evt")
            .WithPayload(Encoding.UTF8.GetBytes(json))
            .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
            .WithRetainFlag(false)
            .WithUserProperty("event_type_code", "888")
            .WithUserProperty("dev_event_id", devEventId.ToString())
            .WithUserProperty("dev_timestamp", nowTs.ToString())
            .WithUserProperty("correlation_id", corrId)
            .Build();

        if (_mqttClient != null && _mqttClient.IsConnected)
        {
            await _mqttClient.PublishAsync(eventMsg, ct);
            Console.WriteLine($"[EVENT] Published event 888 (id={devEventId}, corr={corrId}) to dev/{_deviceSn}/evt");
        }
    }

    private static async Task OnMessageReceivedAsync(MqttApplicationMessageReceivedEventArgs e)
    {
        var topic = e.ApplicationMessage.Topic;
        var payload = Encoding.UTF8.GetString(e.ApplicationMessage.PayloadSegment);
        Console.WriteLine($"\n[INBOUND] Topic: {topic}");
        Console.WriteLine($"          Payload: {payload}");

        // Extract correlation data from properties
        string corrId = Guid.NewGuid().ToString();
        if (e.ApplicationMessage.CorrelationData != null && e.ApplicationMessage.CorrelationData.Length > 0)
        {
            corrId = Encoding.UTF8.GetString(e.ApplicationMessage.CorrelationData);
        }
        else if (e.ApplicationMessage.UserProperties != null)
        {
            foreach (var prop in e.ApplicationMessage.UserProperties)
            {
                if (string.Equals(prop.Name, "correlation_id", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(prop.Name, "correlationData", StringComparison.OrdinalIgnoreCase))
                {
                    corrId = prop.Value;
                    break;
                }
            }
        }

        // Handle server task / RPC message (e.g. srv/<SN>/tsk or srv/<SN>/rsp)
        try
        {
            using var doc = JsonDocument.Parse(payload);
            var root = doc.RootElement;

            int methodCode = 0;
            if (root.TryGetProperty("method_code", out var mcElem))
                methodCode = mcElem.GetInt32();
            else if (root.TryGetProperty("200", out var c200))
                methodCode = c200.GetInt32();

            Console.WriteLine($"[RPC] Handling method_code = {methodCode}, correlation = {corrId}");

            // Execute simple command handling (echo, ping, sysinfo)
            var result = ExecuteTask(methodCode, root);

            // Send response back to dev/<SN>/res
            var responseTopic = $"dev/{_deviceSn}/res";
            var respJson = JsonSerializer.Serialize(result);

            var respMsg = new MqttApplicationMessageBuilder()
                .WithTopic(responseTopic)
                .WithPayload(Encoding.UTF8.GetBytes(respJson))
                .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtMostOnce)
                .WithUserProperty("status_code", "200")
                .WithUserProperty("correlationData", corrId)
                .WithCorrelationData(Encoding.UTF8.GetBytes(corrId))
                .Build();

            if (_mqttClient != null && _mqttClient.IsConnected)
            {
                await _mqttClient.PublishAsync(respMsg);
                Console.WriteLine($"[RPC-RSP] Published response to {responseTopic} (status 200)");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[RPC-WARN] Failed to process payload as JSON task: {ex.Message}");
        }
    }

    private static Dictionary<string, object> ExecuteTask(int methodCode, JsonElement root)
    {
        var res = new Dictionary<string, object>
        {
            ["status"] = "ok",
            ["timestamp"] = DateTimeOffset.UtcNow.ToString("o"),
            ["device_sn"] = _deviceSn
        };

        switch (methodCode)
        {
            case 7001: // Diagnostic exec
                res["command"] = "system_info";
                res["os"] = RuntimeInformation.OSDescription;
                res["arch"] = RuntimeInformation.OSArchitecture.ToString();
                res["machine"] = Environment.MachineName;
                res["processor_count"] = Environment.ProcessorCount;
                res["uptime_sec"] = (int)TimeSpan.FromMilliseconds(Environment.TickCount64).TotalSeconds;
                break;

            case 100: // Ping / Status
            default:
                res["echo"] = "ACK from C# main_app";
                res["method_code"] = methodCode;
                break;
        }

        return res;
    }
}

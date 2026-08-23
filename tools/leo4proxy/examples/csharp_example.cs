// Leo4Proxy C# (.NET / Unity / ASP.NET) Client Example
// Connects to local Leo4Proxy for device discovery and mTLS API proxying

using System;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;

class Program
{
    static async Task Main(string[] args)
    {
        Console.WriteLine("================================================================");
        Console.WriteLine("  Leo4 IoT C# Client via Leo4Proxy (SChannel mTLS)");
        Console.WriteLine("================================================================\n");

        using var http = new HttpClient();

        // 1. Query Device Metadata & SN
        Console.WriteLine("[1] Querying Device Metadata (http://127.0.0.1:18443/_leo4/info)...");
        try
        {
            var infoJson = await http.GetStringAsync("http://127.0.0.1:18443/_leo4/info");
            using var doc = JsonDocument.Parse(infoJson);
            var root = doc.RootElement;
            
            Console.WriteLine($"  Status:     {root.GetProperty("status").GetString()}");
            Console.WriteLine($"  Device SN:  {root.GetProperty("sn").GetString()}");
            Console.WriteLine($"  Email:      {root.GetProperty("email").GetString()}");
            Console.WriteLine($"  Serial:     {root.GetProperty("serial").GetString()}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[ERROR] Failed to query local proxy: {ex.Message}");
            return;
        }

        // 2. Query plain Device SN
        var sn = (await http.GetStringAsync("http://127.0.0.1:18443/_leo4/sn")).Trim();
        Console.WriteLine($"\n[2] Plain Device SN: {sn}");

        // 3. Make mTLS Backend Request via Proxy
        Console.WriteLine("\n[3] Calling LicenseBilling API via local proxy...");
        var formContent = new FormUrlEncodedContent(new[]
        {
            new System.Collections.Generic.KeyValuePair<string, string>("function", "check"),
            new System.Collections.Generic.KeyValuePair<string, string>("Signature", "TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=")
        });

        var response = await http.PostAsync("http://127.0.0.1:18443/licensebilling/", formContent);
        var responseBody = await response.Content.ReadAsStringAsync();

        Console.WriteLine($"  HTTP Status:  {(int)response.StatusCode}");
        Console.WriteLine($"  Response XML: {responseBody.Trim()}");
        Console.WriteLine("\n[SUCCESS] C# demonstration completed.");
    }
}

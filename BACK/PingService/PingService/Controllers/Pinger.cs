using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Threading;

namespace PingService.Controllers
{
    public class Pinger : Base
    {
        readonly Dictionary<DateTime, int> _attempts = new Dictionary<DateTime, int>();

        public override int DelayStart { get { return AppConfig.StartAfter * 1000; } }

        public override int SleepInterval {
            get { return AppConfig.SleepInterval * 1000; }
        }

        public override void SimpleAction()
        {

            Logger.Info("SimpleAction ...");
            if(_attempts.Values.Contains(0))
                _attempts.Clear();

            if (_attempts.Count >= AppConfig.MaxAttemptFault)
            {
                _attempts.Clear();
                Logger.Info("StartInfo Run " + AppConfig.Run+ "Arguments " + AppConfig.Arguments);
                var process = new Process
                {
                    StartInfo =
                    {
                        FileName = AppConfig.Run,
                        Arguments = AppConfig.Arguments,
                        UseShellExecute = false,
                        RedirectStandardOutput = true
                    }
                };
                var result = process.Start();

                Logger.Info("Process Start Result " + result);

                if (result)
                {
                    while (!process.StandardOutput.EndOfStream)
                    {
                        string line = process.StandardOutput.ReadLine();
                        Logger.Info("process: " + line);
                    }
                }
                return;
            }

            var t = DateTime.Now;
            _attempts.Add(t, -1);

            new Thread(() =>
                {
                    ServicePointManager.ServerCertificateValidationCallback = (a, b, c, d) => true;
                    Thread.Sleep(100);
                    try
                    {
                        using (var wc = new WebClient())
                        {
                            Logger.Info("try ping " + AppConfig.PingResource);
                            wc.DownloadString(AppConfig.PingResource);
                        }
                        Logger.Info("ping OK");

                        _attempts[t] = 0;
                    }
                    catch (Exception ex)
                    {
                        Logger.Error(ex);
                    }
                    
                    Logger.Info("ping THREAD DONE.");

                }) {IsBackground = true}.Start();
        }
    }
}

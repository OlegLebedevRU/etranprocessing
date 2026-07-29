using System;
using System.Linq;
using System.Net;
using System.Threading;
using ProcessorService.Data;

namespace ProcessorService.Controllers
{
    public class Sms : Base
    {
        public override void SimpleAction()
        {
            Logger.Info("Sms ...");
            var i = 0;
            using (var dbs = new PostomatsEntities())
            {
                var found = dbs.tb_Messages.Where(p => p.StatusId == 400 || p.StatusId == 420);
                if (found.Any())
                {
                    foreach (var tbMessagese in found)
                    {
                        Logger.Info("Id " + tbMessagese.Id);
                        var messagese = tbMessagese;
                        new Thread(() =>
                        {
                            i++;
                            var result = string.Empty;
                            try
                            {
                                WebClient client = new WebClient();
                                var req = AppConfig.SmsService + "?" + "function=sms&phone=" + messagese.Phone + "&msg=" + messagese.Text;
                                Logger.Info("req " + req);
                                result = client.DownloadString(req);
                                Logger.Info("result " + result);
                            }
                            catch (Exception ex)
                            {
                                Logger.Error(ex);
                            }

                            Logger.Info(" tbMessagese.Id " + messagese.Id + " result" + result);

                            messagese.StatusId = result.ToUpper() == "OK" ? 410 : 420;
                            messagese.StatusComment = result;
                            messagese.StatusDateTime = DateTime.Now;
                            i--;

                        }) { IsBackground = true }.Start();
                        Thread.Sleep(1000);
                    }
                    var start = DateTime.Now;
                    while (i > 0 && DateTime.Now.Subtract(start).TotalSeconds < 60)
                    {
                        Logger.Info("Wait ...");
                        Thread.Sleep(1000);
                    }
                    Logger.Info("i " + i);
                    Logger.Info("Save ...");
                    dbs.SaveChanges();
                    Logger.Info("DONE");
                }
            }
        }
        public override int DelayStart { get { return AppConfig.StartAfter * 1000; } }

        public override int SleepInterval
        {
            get { return AppConfig.SleepInterval * 1000; }
        }
    }
}

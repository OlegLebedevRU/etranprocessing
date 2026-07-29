using System;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Xml;
using PaySystemproxy;
using ProcessorService.Data;

namespace ProcessorService.Controllers
{
    public class Payments : Base
    {
        public override int DelayStart { get { return AppConfig.StartAfter * 1000; } }

        public override int SleepInterval
        {
            get { return AppConfig.SleepInterval * 1000; }
        }

        public override void SimpleAction()
        {
            Logger.Info("Payments ...");
            var i = 0;
            using (var dbs = new PostomatsEntities())
            {
                var found = dbs.tb_Payments.Where(p => p.SourceTypeId == 3 && (p.StatusId == 350 || p.StatusId == 370));
                if (!found.Any()) return;
                foreach (var payment in found)
                {

                    var paymExtId = payment.PaymExtId;
                    try
                    {
                        Logger.Info("paymExtId " + paymExtId);

                        int connectionTimeout = 30;
                        string rek = AppConfig.Rek;
                        var payment1 = payment;
                        var paymSubjTp = dbs.tb_Tsp.First(p => p.Id == payment.SourceId).Code.ToString();
                        var paramTspCode = dbs.tb_TspPayParameters.First(p => p.TspId == payment1.SourceId).Code.ToString();
                        var function = AppConfig.Function;

                        var amount = ((int)payment.Sum * 100).ToString(CultureInfo.InvariantCulture);
                        var totalSum = ((int)payment.Sum).ToString(CultureInfo.InvariantCulture);
                        var Params = paramTspCode + " " + payment.Phone;

                        new Thread(() =>
                        {
                            i++;
                            try
                            {
                                Logger.Info("AppConfig.PaySystem " + AppConfig.PaySystem);
                                Logger.Info(
                                    "function " + function +
                                    " paymExtId " + paymExtId +
                                    " paymSubjTp " + paymSubjTp +
                                    " amount " + amount +
                                    " Params " + Params +
                                    " rek " + rek + " totalSum " + totalSum
                                    );
                                var result = new ProxyInterfaceService(AppConfig.PaySystem).
                                    DoRequest(function, paymExtId, paymSubjTp, amount, Params, connectionTimeout, rek,
                                        totalSum);

                                Logger.Info(" paymExtId " + paymExtId + " result" + result);

                                string statusComment = string.Empty;
                                string status = string.Empty;
                                try
                                {
                                    var doc = new XmlDocument();
                                    doc.LoadXml(result);
                                    var xmlElement = doc.SelectSingleNode("Response/Result");
                                    if (xmlElement != null) status = xmlElement.InnerText;

                                    xmlElement = doc.SelectSingleNode("Response/Description");
                                    if (xmlElement != null)
                                        statusComment = xmlElement.InnerText;

                                }
                                catch (Exception ex)
                                {
                                    Logger.Error(paymExtId, ex);
                                }
                                payment1.StatusId = status.ToUpper() == "OK" ? 360 : 370;
                                payment1.StatusComment = statusComment;
                                payment1.StatusDateTime = DateTime.Now;
                            }
                            catch (Exception ex)
                            {
                                Logger.Error(paymExtId, ex);
                            }
                            i--;
                        }) { IsBackground = true }.Start();
                    }
                    catch (Exception ex)
                    {
                        Logger.Error(paymExtId, ex);
                    }
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
}

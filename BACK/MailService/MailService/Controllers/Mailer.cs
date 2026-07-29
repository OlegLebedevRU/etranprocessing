using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Mail;
using System.Threading;
using MailService.Entity;
using MailService.Enums;

namespace MailService.Controllers
{
    public class Mailer : Base
    {

        public override int DelayStart { get { return 1000; } }

        public Func<IEnumerable<Message>> ListForSend { get; private set; }

        public override int SleepInterval
        {
            get { return AppConfig.MailerCheckListInterval; }
        }

        public Mailer(Func<IEnumerable<Message>> listForSend)
        {
            ListForSend = listForSend;
        }

        public override void SimpleAction()
        {
            //SelfCheck();
            var sendList = ListForSend();
            if (sendList != null)
            {
                Logger.Info("Mail SimpleAction sendList SecurityProtocol" + System.Net.ServicePointManager.SecurityProtocol);
                foreach (var msg in sendList)
                {
                    Logger.Info("Mail SimpleAction new msg from list  " + msg);
                    var msg1 = msg;
                    Thread.Sleep(50);
                    new Thread(() =>
                      {
                          var result = SendMail(msg1);
                          msg1.Sent(result ? MsgState.SentOk : MsgState.SentError);
                      })
                    { IsBackground = true }.Start();

                    if (StopActivity.WaitOne(1000))
                        break;
                }
                Logger.Info("Mail SimpleAction sendList DONE");
            }
            else
            {
                Logger.Info("Mail SimpleAction sendList EMPTY!");
            }
        }


        private bool SendMail(Message msg)
        {
            try
            {
                Logger.Info("Mail SendMail try send mailMsg " + Environment.NewLine + msg.Body);
                using (var mail = new MailMessage())
                {
                    mail.From = new MailAddress(AppConfig.MailFrom);
                    foreach (var recipient in msg.Recipients)
                    {
                        mail.To.Add(recipient);
                    }
                    mail.Subject = msg.Subject;
                    mail.Body = msg.Body;
                    if (msg.Attachment != null && msg.Attachment.Length > 0)
                    {
                        var att = new Attachment(new MemoryStream(msg.Attachment), msg.FileName);
                        mail.Attachments.Add(att);
                    }
                    mail.IsBodyHtml = true;
                    var smtp = new SmtpClient(AppConfig.SmtpAddress, AppConfig.Port);
                    {
                        smtp.Credentials = new NetworkCredential(AppConfig.MailFrom, AppConfig.Password);
                        smtp.EnableSsl = AppConfig.Ssl;
                        smtp.Send(mail);
                    }
                }
                Logger.Info("Mail SendMail send OK mailMsg " + Environment.NewLine + msg.Body);
                return true;
            }
            catch (Exception ex)
            {
                Logger.Error(ex);
            }
            return false;
        }
    }
}

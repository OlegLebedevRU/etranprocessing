using System;
using System.Web.Configuration;
using System.Web.Mail;
using System.Xml;

namespace App_Code
{
    public class Email
    {
        public static string Send(string to, string Message, string Subject)
        {
            XmlDocument xdoc = new XmlDocument();
            string response = "OK";
            string smtpServer = WebConfigurationManager.AppSettings["SMTP_SERVER"];
            string userName = WebConfigurationManager.AppSettings["MAIL_USER_PLATERRA"];
            string password = WebConfigurationManager.AppSettings["MAIL_PASSWD_PLATERRA"];
            MailMessage msg = new MailMessage();
            if (userName.Length > 0)
            {
                msg.Fields.Add("http://schemas.microsoft.com/cdo/configuration/smtpserver", smtpServer);
                msg.Fields.Add("http://schemas.microsoft.com/cdo/configuration/smtpserverport", 25);
                msg.Fields.Add("http://schemas.microsoft.com/cdo/configuration/sendusing", 2);
                msg.Fields.Add("http://schemas.microsoft.com/cdo/configuration/smtpauthenticate", 1);
                msg.Fields.Add("http://schemas.microsoft.com/cdo/configuration/sendusername", userName);
                msg.Fields.Add("http://schemas.microsoft.com/cdo/configuration/sendpassword", password);
            }
            msg.To = to;
            msg.From = WebConfigurationManager.AppSettings["MAIL_FROM_USER_PLATERRA"]; ;
            msg.Subject = Subject;
            msg.BodyEncoding = System.Text.Encoding.GetEncoding(1251);
            msg.Body = Message;
            SmtpMail.SmtpServer = smtpServer;
            try
            {
                SmtpMail.Send(msg);
            }
            catch(Exception ex)
            {
                response = ex.Message;
            }
            xdoc.LoadXml("<?xml version=\"1.0\" encoding=\"windows-1251\"?><response></response>");
            XmlNode root = xdoc.DocumentElement;
            XmlElement elem2 = xdoc.CreateElement("message");
            elem2.InnerText = response;
            root.AppendChild(elem2);

            return xdoc.OuterXml;
            

        }
    }
}
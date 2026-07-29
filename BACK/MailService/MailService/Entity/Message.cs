using System;
using System.Collections.Generic;
using System.Data;
using System.Linq;
using System.Text.RegularExpressions;
using log4net;
using MailService.Enums;

namespace MailService.Entity
{
    public class Message
    {
        static public ILog Logger = LogManager.GetLogger(typeof(Message));

        public int Type { get; private set; }
        public int Id { get; private set; }
        public string Body { get; private set; }
        public string Subject { get; private set; }
        public byte[] Attachment { get; private set; }
        public string FileName { get; private set; }
        public List<string> Recipients { get; private set; }


        public Message(int type,int id, string msg, string subject, byte[] attachment,string fileName, List<string> recipients)
        {
            Type = type;
            Id = id;
            Body = msg;
            Subject = subject;
            Attachment = attachment;
            FileName = fileName;
            Recipients = recipients;
        }

        public const string MatchEmailPattern =
            @"^(([\w-]+\.)+[\w-]+|([a-zA-Z]{1}|[\w-]{2,}))@"
     + @"((([0-1]?[0-9]{1,2}|25[0-5]|2[0-4][0-9])\.([0-1]?
				[0-9]{1,2}|25[0-5]|2[0-4][0-9])\."
     + @"([0-1]?[0-9]{1,2}|25[0-5]|2[0-4][0-9])\.([0-1]?
				[0-9]{1,2}|25[0-5]|2[0-4][0-9])){1}|"
     + @"([a-zA-Z]+[\w-]+\.)+[a-zA-Z]{2,4})$";

        public const string MailsPattern = @"(([a-zA-Z0-9_\-\.]+)@((\[[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.)|(([a-zA-Z0-9\-]+\.)+))([a-zA-Z]{2,4}|[0-9]{1,3})(\]?)(\s*(;|,)\s*|\s*$))*";
        public static List<string> GetMails(string stuff)
        {
            var list = new List<string>();
            try
            {
                //Regex regex = new Regex(@"(?<group1>^([\w\.\-]+)@([\w\-]+)((\.(\w){2,3})+)$)");
                Regex regex = new Regex(MatchEmailPattern);
                var matche = regex.Match(stuff);
                if(matche.Success)

                //if (matches.Count > 0 && matches[0].Groups.Count > 1)
                {
                }
                MatchCollection matches = regex.Matches(stuff);
                if (matches.Count >0)
                {
                    foreach (Match m in matches)
                    {
                        var mail = m.Value;
                            //m.Groups["mail"].Value;
                        list.Add(mail);
                    }

                }
                //foreach (var found in matches.Groups["group1"].Captures)
                //{
                //    Logger.Info("found " + found);
                    
                //}

            }
            catch (Exception ex)
            {
                Logger.Error(ex);
            }
            return list;

        }

        //(?<group1>
        static public IEnumerable<Message> GetMessages()
        {
            var list = new List<Message>();
            try
            {
                Logger.Info("GetMessages....");

                var db = new DBManager(AppConfig.DbConnectionString);
                Logger.Info("GetMessages OK dbConn " + AppConfig.DbConnectionString);

                var result = (DataSet)db.Execute(AppConfig.DbGetMessages
                    , CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, null);

                foreach (DataRow row in result.Tables[0].Rows)
                {
                    var type = (int)row[0];
                    var id = (int) row[1];
                    var body = (string) row[2];
                    var extraInfo = (string)row[3];
                    var attachment = new byte[] {};
                    var fileName = string.Empty;
                    if (row.ItemArray.Count() > 4)
                    {
                        if (!(row[4] is DBNull))
                        {
                            attachment = (byte[]) row[4];
                            fileName = (string) row[5];
                        }
                    }

                    //extraInfo = "1111;dvkosov@gmail.com";
                    var subject = string.Empty;
                    var recipients = new List<string>();
                    try
                    {
                        Logger.Info("... try parse extraInfo " + extraInfo);
                        var coll = extraInfo.Split(';');
                        subject = coll[0];
                        Logger.Info("subject " + subject);
                        var rec = coll[1].Split(',').ToList();
                        foreach (var recipient in rec)
                        {
                            if (!string.IsNullOrEmpty(recipient))
                            {
                                Logger.Info("recipient " + recipient);
                                recipients.Add(recipient);
                            }
                        }

                        if (recipients.Count == 0 && !string.IsNullOrEmpty(coll[1]))
                        {
                            Logger.Info("recipient coll[1] " + coll[1]);
                            recipients.Add(coll[1]);
                        }

                    }
                    catch (Exception ex)
                    {
                        
                        Logger.Error(ex);
                    }

                    if (recipients.Count > 0)
                    {
                        var msg = new Message(type, id, body, subject, attachment, fileName, recipients);
                        list.Add(msg);
                    }
                    else
                    {
                        Logger.Info("!!! ERROR не получены адресаты для id " + id + " extraInfo " + extraInfo);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Error(ex);
            }
            return list;
        }

        public void Sent(MsgState state)
        {
            try
            {
                Logger.Info("Message Try Fix Result DB " + Id + " State  " + state);

                var db = new DBManager(AppConfig.DbConnectionString);
                db.Execute(AppConfig.DbPostResult
                    , CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, null, Type, Id, (int)state);

                Logger.Info("Message Result DB OK " + Id + " State  " + state);
            }
            catch (Exception ex)
            {
                Logger.Error(ex);
            }
        }
    }
}

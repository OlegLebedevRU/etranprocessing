using System.Data.Entity.Validation;
using System.Text;
using DbAttachmentLib;
using DispatcherCommon;
using FoNetLib;
using System;
using System.Collections.Specialized;
using System.Data;
using System.IO;
using System.Net;
using System.Web;
using System.Xml;
using DispatcherCommon.Data;


namespace Dispatcher
{


    public class DbAttachmentParamL : DbAttachmentParam
    {
        public override string ConnectionString
        {
            get
            {
                return GlobalObjectsManager.DbFileStore;
            }
        }

    }


    /// <summary>
    /// Диспетчер сообщений Etran. Принимает и рассылает сообщения по
    /// рабочим серверам.
    /// </summary>
    public class Dispatcher : IHttpHandler
    {
        private const int RESPONSE_CODE_PAGE = 1251;

        public Dispatcher()
        {
        }

        #region IHttpHandler Members
        static public readonly string AppPath = HttpContext.Current.Server.MapPath("~/");


        private string GetRootXml()
        {
            return ("<?xml version='1.0' encoding='windows-1251'?><Response/>");
        }

        /// <summary>
        /// Обработчик HTTP-запроса пратежной систме.
        /// </summary>
        /// <param name="Context">Текущий HTTP-контекст.</param>
        public void ProcessRequest(HttpContext Context)
        {
            XmlDocument xml_doc = new XmlDocument();
            xml_doc.LoadXml(GetRootXml());
            XmlNode node = xml_doc.SelectSingleNode("/Response");
            bool error = false;
            try
            {
                GlobalObjectsManager.Logger.Info(Context.Request.RawUrl);
                Context.Response.Charset = "windows-1251";
                Context.Response.ContentEncoding = System.Text.Encoding.GetEncoding("windows-1251");
                Context.Response.ContentType = "text/xml";


                //Context.Response.Cache.SetNoServerCaching();
                //Context.Response.Cache.SetCacheability(HttpCacheability.NoCache);
                //Context.Response.Cache.SetAllowResponseInBrowserHistory(false);

                //NameValueCollection col = Context.Request.QueryString;
                //Context.Response.ContentType = "text/xml";
                //Context.Response.ContentType = "application/octet-stream";


                string postParams = null;
                NameValueCollection qparams;
                NameValueCollection getQparams = HttpUtility.ParseQueryString(Context.Request.Url.Query);
                if (Context.Request.HttpMethod.ToUpper() == "POST")
                {

                    System.IO.Stream body = Context.Request.InputStream;
                    //System.Text.Encoding encoding = Context.Request.ContentEncoding;
                    //System.IO.StreamReader reader = new System.IO.StreamReader(body, encoding);
                    System.IO.StreamReader reader = new System.IO.StreamReader(body, Encoding.UTF8);
                    postParams = reader.ReadToEnd();
                    body.Close();
                    reader.Close();

                    GlobalObjectsManager.Logger.Info("POST: " + postParams);
                    //qparams = HttpUtility.ParseQueryString(PostParams, Encoding.GetEncoding(1251));
                    //HttpUtility.UrlEncode(
                    qparams = HttpUtility.ParseQueryString(postParams, Encoding.UTF8);
                }
                else
                {
                    //qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.GetEncoding(1251));
                    //qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query, Encoding.UTF8);
                    qparams = HttpUtility.ParseQueryString(Context.Request.Url.Query);
                }


                string function = qparams["function"] ?? getQparams["function"];
                string contentDisposition = qparams["ContentDisposition"] ?? string.Empty;

                // TEST

//                function = "ebs";
//                getQparams.Clear();
//                getQparams.Add("service", "CheckSMSCode");
//                //<PayBillRequest>
//                //<BillId>1</BillId>
//                //<Nominal>200000</Nominal>
//                //</PayBillRequest>

//                postParams = @"
//<CheckSMSCodeRequest>
//  <KioskId>9902</KioskId>
//  <PhoneNumber>9685671664</PhoneNumber>
//  <Code>9520</Code>
//</CheckSMSCodeRequest>
//";

//                <PayBillRequest>
//  <BillId>1</BillId>
//  <Nominal>100000</Nominal>
//</PayBillRequest>


                //                postParams = @"
                //<AuthBillInfoRequest>
                //  <BillCode>3812413541</BillCode>
                //</AuthBillInfoRequest>
                //";
                //                postParams = @"<InvestorInfoRequest>
//  <Phone>9266763193</Phone>
//</InvestorInfoRequest>";

//                    @"<OpenShiftRequest>
//  <KioskId>9910</KioskId>
//  <TokenKey>123</TokenKey>
//</OpenShiftRequest>";
                // TEST

                if (function.ToLower() == "ebs")
                {
                    var serviceName = getQparams["service"];
                    IXmlRequestProcessor rp = new RequestProcessor();
                    Context.Response.Write(rp.DoRequest(serviceName, postParams));
                }
                else if (function.ToLower() == "docnew")
                {
                    //var xslt = Path.Combine(AppPath, @"Resources\ReportProcessor.xslt");
                    //var logo = Path.Combine(AppPath, @"Resources\Toco.jpg");
                    //var data = Path.Combine(AppPath, @"Resources\UserProfile.xml");

                    //DirectoryInfo dirWindowsFolder = Directory.GetParent(Environment.GetFolderPath(Environment.SpecialFolder.System));

                    // Concatenate Fonts folder onto Windows folder.
                    //string strFontsFolder = Path.Combine(dirWindowsFolder.FullName, "Fonts");
                    //string ttf = Path.Combine(strFontsFolder, "ARIAL.TTF");

                    var templateName = qparams["template"] ?? "Veksel";
                    var xslt = Path.Combine(AppPath, @"Resources\" + templateName + ".xslt");
                    if (!File.Exists(xslt))
                        throw new Exception("xslt file not " + templateName + " found.");

                    var xltDocument = new XmlDocument();
                    xltDocument.Load(xslt);

                    var template = xltDocument.OuterXml;
                    //IPdfRaw pdf = new PdfRaw(xslt, logo, ttf);
                    //IPdfRaw pdf = new PdfRaw(xslt, logo, @"%systemroot%\fonts\Tahoma.TTF");
                    //string xmlData = File.ReadAllText(data);

                    for (int i = 0; i < qparams.Keys.Count; i++)
                    {
                        var toFind = "%" + qparams.Keys[i] + "%";
                        template = template.Replace(toFind, qparams[i]);
                    }
                    xltDocument.LoadXml(template);
                    GlobalObjectsManager.Logger.Info(xltDocument.OuterXml);
                    //var bytes = pdf.GetPdfRaw(doc);
                    var pdf = new FonetPdf();
                    var bytes = pdf.MakePdf(xltDocument);


                    var dbService = new DbAttachment(new DbAttachmentParamL());
                    var attachId = Guid.NewGuid();
                    dbService.AddAttachment(attachId, attachId + ".pdf", bytes);

                    XmlElement result1 = xml_doc.CreateElement("Result");
                    result1.InnerText = attachId.ToString();
                    node.AppendChild(result1);
                    Context.Response.Write(xml_doc.OuterXml);

                }
                else if (function.ToLower() == "docget")
                {
                    var attachId = qparams["id"];
                    string fileName;
                    byte[] objData;

                    var dbService = new DbAttachment(new DbAttachmentParamL());
                    var size = dbService.GetAttachment(new Guid(attachId), out fileName, out objData);

                    HttpContext.Current.Response.Clear();
                    HttpContext.Current.Response.ContentType = "application/pdf";

                    HttpContext.Current.Response.AddHeader("Content-Disposition",
                                                           string.IsNullOrEmpty(contentDisposition)
                                                               ? string.Format("attachment;filename=\"{0}\"", fileName)
                                                               : contentDisposition
                        );
                    HttpContext.Current.Response.OutputStream.Write(objData, 0, objData.Length);

                }
                else if (function.ToLower() == "sms")
                {
                    var smsReq = EtranConfigurationManager.EtranConfig + "?" + qparams;
                    GlobalObjectsManager.Logger.Info("sms smsReq " + smsReq);

                    WebClient client = new WebClient();
                    var result = client.DownloadString(smsReq);

                    GlobalObjectsManager.Logger.Info("sms result " + result);
                    XmlElement result1 = xml_doc.CreateElement("Result");
                    result1.InnerText = result;
                    node.AppendChild(result1);
                    Context.Response.Write(xml_doc.OuterXml);

                }
                else if (function.ToLower() == "mail")
                {
                    GlobalObjectsManager.Logger.Info("mail ...");

                    GlobalObjectsManager.Logger.Info("to " + qparams["to"]);
                    
                    var to = qparams["to"];//.Base64Decode();
                    GlobalObjectsManager.Logger.Info("mail to " + to);

                    var subject = qparams["subject"];//.Base64Decode();
                    GlobalObjectsManager.Logger.Info("mail subject " + subject);

                    var msg = qparams["msg"];//.Base64Decode();
                    GlobalObjectsManager.Logger.Info("mail msg " + msg);
                    var  attId = qparams["attId"];//.Base64Decode();
                    GlobalObjectsManager.Logger.Info("mail attId  " + attId);


                    var extraInfo = subject + ";" + to;
                    GlobalObjectsManager.Logger.Info("mail extraInfo " + extraInfo);

                    DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
                    db.Execute("Report_AddNew", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery,
                        null, 1, msg, extraInfo, (string.IsNullOrEmpty(attId)? (object) null : Guid.Parse(attId)));
                    XmlElement result = xml_doc.CreateElement("Result");
                    result.InnerText = "OK";
                    node.AppendChild(result);
                    Context.Response.Write(xml_doc.OuterXml);
                    GlobalObjectsManager.Logger.Info("mail DONE.");
                }
                else
                {
                    XmlElement result = xml_doc.CreateElement("Result");
                    result.InnerText = "Error";
                    node.AppendChild(result);
                    Context.Response.Write(xml_doc.OuterXml);
                }


                //dt_from = DateTime.Parse(col["from"]);
                //dt_to = DateTime.Parse(col["to"]);
                //string file_name = pref + dt_from.ToString("yyyyMMdd") + "-" + dt_to.ToString("yyyyMMdd") + ".xml";

                //Context.Response.AddHeader("Content-Disposition", "attachment; filename=" + file_name);

                //DBManager db = new DBManager(GlobalObjectsManager.DbConnectionString);
                //DataSet ds = (DataSet)db.Execute(sp_name, CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, dt_from, dt_to, iTerminal);
                //XmlDocument xml_doc = new XmlDocument();
                //xml_doc.LoadXml(GetRootXml());
                //XmlNode node = xml_doc.SelectSingleNode("/ROWSET");
                //for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
                //{
                //    NameValueCollection htbl = new NameValueCollection();
                //    htbl.Add("COUNT", (i + 1).ToString());

                //    for (int j = 0; j < ds.Tables[0].Columns.Count; j++)
                //    {
                //        string key = ds.Tables[0].Columns[j].ColumnName;
                //        string val = ds.Tables[0].Rows[i][j].ToString();
                //        htbl.Add(key, val);
                //    }

                //    ////XmlElement Row = XmlClass.CreateElement(xml_doc, "ROW", no);
                //    XmlElement Row = xml_doc.CreateElement("ROW");
                //    node.AppendChild(Row);
                //    XmlClass.InsertInnerText(xml_doc, Row, htbl);
                //}

                //Context.Response.Write(xml_doc.OuterXml);
            }
            catch (DbEntityValidationException e)
            {
                error = true;
                GlobalObjectsManager.Logger.Error(e);
                foreach (var eve in e.EntityValidationErrors)
                {
                    GlobalObjectsManager.Logger.Error(string.Format("Entity of type \"{0}\" in state \"{1}\" has the following validation errors:",
                        eve.Entry.Entity.GetType().Name, eve.Entry.State));
                    foreach (var ve in eve.ValidationErrors)
                    {
                        GlobalObjectsManager.Logger.Error(string.Format("- Property: \"{0}\", Error: \"{1}\"",
                            ve.PropertyName, ve.ErrorMessage));
                    }
                }
            }
            catch (Exception ex)
            {
                error = true;
                GlobalObjectsManager.Logger.Error(ex);
            }
            finally
            {
                if (error)
                {
                    XmlElement result = xml_doc.CreateElement("Result");
                    result.InnerText = "Error";
                    node.AppendChild(result);
                    Context.Response.Write(xml_doc.OuterXml);
                }
                GlobalObjectsManager.Logger.Info("REQUEST PROCESSING DONE");
            }
        }

        /// <summary>
        /// Обработчик определяется как повторно используемый.
        /// </summary>
        public bool IsReusable
        {
            get
            {
                return true;
            }
        }

        #endregion
    }
}

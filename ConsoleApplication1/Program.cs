using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Net;
using System.Reflection;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Web;
using DbAttachmentLib;
using FoNetLib;
using PdfCommon.Interfaces;
using System;
using System.Xml;
using DispatcherCommon;
using Platerra.Terminal.Common.ESV.Classes.Exchange;
using TextSharpLib;

namespace ConsoleApplication1
{


    class Program
    {

        static public string WebRequest(string url, int timeout, string _data, X509Certificate2 cert)
        {
            HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
            if (cert != null)
                wrq.ClientCertificates.Add(cert);

            wrq.Timeout = timeout;

            if (_data == null)
                wrq.Method = "GET";
            else
            {
                wrq.Method = "POST";
                wrq.ContentType = "application/x-www-form-urlencoded";
                byte[] data = Encoding.Default.GetBytes(_data);
                wrq.ContentLength = data.Length;
                Stream newStream = wrq.GetRequestStream();
                newStream.Write(data, 0, data.Length);
                newStream.Close();
            }
            HttpWebResponse hwr = wrq.GetResponse() as HttpWebResponse;
            Stream strm = hwr.GetResponseStream();
            StreamReader reader = new StreamReader(strm, Encoding.GetEncoding(1251));
            return reader.ReadToEnd();
        }

        //public static byte[] CreateBarcodeImage(string barcode)
        //{
        //    var b = new BarcodeLib.Barcode();
        //    var image = b.Encode(BarcodeLib.TYPE.Interleaved2of5, barcode.Trim(), Color.Black, Color.White, 300, 150);
        //    var ms = new MemoryStream();
        //    image.Save(ms, ImageFormat.Gif);
        //    image.Save("image.gif");
        //    return ms.GetBuffer();
        //}

        private static bool CustomValidation(object sender, X509Certificate cert, X509Chain chain, System.Net.Security.SslPolicyErrors error) { return true; }

        public static String ConstructQueryString(NameValueCollection parameters)
        {
            List<String> items = new List<String>();

            foreach (String name in parameters)
                items.Add(String.Concat(name, "=", System.Web.HttpUtility.UrlEncode(parameters[name])));

            return String.Join("&", items.ToArray());
        }


        static void Main(string[] args)
        {
            ServicePointManager.ServerCertificateValidationCallback += new System.Net.Security.RemoteCertificateValidationCallback(CustomValidation);

            try
            {

                var nvc = new NameValueCollection
                {
                    {"template", "agreement1"},
                    {"function", "docnew"},
                    {"param0", "123456"},
                    {"param1", "25.11.2014"},
                    {"param2", "г.Владивосток, ул Ленинского Комсомола, д.205, стр 22"},
                    {"param3", "ООО Центр Развития Технологий"},
                    {"param4", "ИНН 77009456789"},
                    {"param5", "г.Владивосток, ул Ленинского Комсомола, д.205, стр 22"},
                    {"param6", "СТО ТЫСЯЧ ПЯТЬСОТ Рублей"},
                    {"param7", "47 (сорок семь)"},
                    {"param8", "Колобкову Александру Витальевичу"},
                    {"param9", "4509777890"},
                    {"param10", "г.Владивосток, ул Ленинского Комсомола, д.205, стр 22"},
                    {"param11", "22.05.2015"},
                    {"param12", "г.Владивосток, ул Ленинского Комсомола, д.205, стр 22"},
                    {"param13", "Ковбоийский Вениамин Сергеевич"},
                    {"param14", "Qnfewfanfnwedasrqfwnfmasds47"},
                    {"param15", "123456"},
                    {
                        "param16",
                        "02.03.2015=1000руб; 02.03.2015=1000руб;02.03.2015=1000руб;02.03.2015=1000руб; 02.03.2015=1000руб; 02.03.2015=1000руб;"
                    }
                };

//                2017 - 01 - 17 15:43:09,601 - http://portal.e-veksel.ru/Dispatcher/?function=ebs&service=PayBill
//2017 - 01 - 17 15:43:09,601 - <? xml version = "1.0" encoding = "windows-1251" ?>
//       < PayBillRequest >
//         < BillId > 219 </ BillId >
//         < Nominal > 15100 </ Nominal >
//         < PayDateTime > 2017 - 01 - 17T15:43:09.5707304 + 03:00 </ PayDateTime >
//                </ PayBillRequest >

                                WebClient cl = new WebClient();
                var uri = "http://portal.e-veksel.ru/Dispatcher/?function=ebs&service=InvestorInfo";
                //var uri = "http://localhost:1746/?function=ebs&service=EmissionInfo";//PayBill";
                //var uri = "http://localhost:1746/gate.ashx?function=ebs&service=MainPageEmissions";
                //var uri = "https://corepay.ru/Dispatcher/";
                //var data = "function=mail&subject="
                //           + "ebsMailTestAtt"
                //           + "&to=dvkosov@gmail.com"
                //           + "&msg=" + "guiasrhgdsf PLUS ATTACHMENT"

                    //+ "&attId=" + "F50C987D-90D6-44DA-BE3C-FBADC68BD180"
                //var t = cl.UploadValues(uri, nvc);
                //var yy = WebRequest(uri, 30000, HttpUtility.HtmlEncode(ConstructQueryString(nvc)), null);
                //var yy = WebRequest(uri, 30000, ConstructQueryString(nvc), null);
                var r = new MainPageEmissions.MainPageEmissionsRequest() { KioskId = 9904 };
                //var yy = WebRequest(uri, 130000, XmlService.Serialize(r), null);

                var sendTo =
                    @"<InvestorInfoRequest>
  <Phone>9252872741</Phone>
</InvestorInfoRequest>";
//                    @"<EmissionInfoRequest>
//  <EmissionId>56</EmissionId>
//  <IncludeEmissionImage>false</IncludeEmissionImage>
//</EmissionInfoRequest>";
//                    @"
//<PayBillRequest>
//<BillId>219</BillId>
//<Nominal>15100</Nominal>
//<PayDateTime>2017-01-17T15:43:09.5707304+03:00</PayDateTime>
//</PayBillRequest>";

                var yy = WebRequest(uri, 130000, sendTo, null);

                //var obj = XmlService.DeSerialize<MainPageEmissions.MainPageEmissionsResponse>(yy);
                //XmlDocument srvRet = new XmlDocument();
                //var str = System.Text.Encoding.ASCII.GetString(t);
                //var yy = WebRequest(uri, 30000, data, null);

                System.Diagnostics.Debug.WriteLine(yy);
                Console.WriteLine(yy);
                //Console.ReadKey();
                var rr = "AAAAAAAAAAAAAAAA";


                return;


                string fileName = DateTime.Now.ToString("yyyyMMddHHmmssffff") + ".pdf";
                byte[] objData;

                var path = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
                var xslt = Path.Combine(path, @"..\..\Veksel.xslt");
                XmlDocument xltDocument = new XmlDocument();
                xltDocument.Load(xslt);
                var template = xltDocument.OuterXml;
                for (var i = 0; i < nvc.Count; i++)
                {
                    template = template.Replace(nvc.Keys[i], nvc[i]);

                }

                System.Diagnostics.Debug.WriteLine(template);
                var pdf = new FonetPdf();
                xltDocument.LoadXml(template);
                objData = pdf.MakePdf(xltDocument);


                //var path = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
                //var xslt = Path.Combine(path, @"Resources\ReportProcessor.xslt");
                //var logo = Path.Combine(path, @"Resources\Toco.jpg");
                //var data = Path.Combine(path, @"Resources\UserProfile.xml");

                //DirectoryInfo dirWindowsFolder = Directory.GetParent(Environment.GetFolderPath(Environment.SpecialFolder.System));

                //// Concatenate Fonts folder onto Windows folder.
                //string strFontsFolder = Path.Combine(dirWindowsFolder.FullName, "Fonts");
                //string ttf = Path.Combine(strFontsFolder, "ARIAL.TTF");

                //IPdfRaw pdf = new PdfRaw(xslt, logo, ttf);



                ////IPdfRaw pdf = new PdfRaw(xslt, logo, @"%systemroot%\fonts\Tahoma.TTF");
                ////var doc = new XmlDocument();
                ////doc.Load(data);
                ////var bytes = pdf.GetPdfRaw(doc);


                ////var dbService = new DbAttachment(new DbAttachmentParam());
                ////var id = Guid.NewGuid();
                ////dbService.AddAttachment(id, id + ".pdf", bytes);


                ////var size = dbService.GetAttachment(id, out fileName, out objData);

                //fileName = "3242432.pdf";
                ////objData = pdf.CreatePdf(File.ReadAllText(@"C:\_PRINTER\BitTorrent трекер RuTracker_org.htm", Encoding.GetEncoding(1251)));
                //objData = pdf.CreatePdf(File.ReadAllText(@"C:\_PRINTER\logon.html", Encoding.UTF8));





                string strFileToSave = Path.Combine(path, fileName); ;
                var objFileStream = new FileStream(strFileToSave, FileMode.Create, FileAccess.Write);
                objFileStream.Write(objData, 0, objData.Length);

                objFileStream.Close();



            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(ex.Message);
            }

        }
    }
}

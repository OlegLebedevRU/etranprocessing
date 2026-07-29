using System;
using System.IO;
using System.Text;
using System.Web;
using System.Web.UI;
using System.Xml;
using System.Xml.Xsl;
using iTextSharp.text;
using iTextSharp.text.pdf;
using iTextSharp.text.xml;
using TextSharpLib;

namespace ASPNETPdfSample
{
    public partial class ReportPdf : Page
    {
        protected override void OnLoad(EventArgs e)
        {
            //Записываем в Response pdf файл, указываем его MIME тип и имя файла
            HttpContext.Current.Response.Clear();
            HttpContext.Current.Response.ContentType = "application/pdf";
            HttpContext.Current.Response.AddHeader("Content-Disposition", string.Format("attachment;filename=\"{0}\"", "report.pdf"));
            byte[] pdfRaw = GetPdfRaw();
            HttpContext.Current.Response.OutputStream.Write(pdfRaw, 0, pdfRaw.Length);
        }

        public byte[] GetPdfRaw()
        {

            var pdf = new PdfRaw(MapPath(@"~\Resources\ReportProcessor.xslt"), MapPath(@"~\Resources\Toco.jpg"), @"%systemroot%\fonts\Tahoma.TTF");
            

            XmlDocument doc = new XmlDocument();
            //Загружаем данные их xml файла
            doc.Load(MapPath(@"~\Resources\UserProfile.xml"));
            return pdf.GetPdfRaw(doc);

            //XslCompiledTransform xslTransform = new XslCompiledTransform();
            ////Загружаем схему XSLT преобразований
            //xslTransform.Load(MapPath(@"~\Resources\ReportProcessor.xslt"));
            //XsltArgumentList list = new XsltArgumentList();
            ////Записываем в параметры схемы путь до картинки
            //list.AddParam("picturePath", string.Empty, MapPath(@"~\Resources\Toco.jpg"));
            ////Создаем поток в памяти, куда будет писаться наша xml схема для pdf документа
            //using (MemoryStream stream = new MemoryStream())
            //{
            //    //Создаем xml схему нашего pdf документа
            //    xslTransform.Transform(doc, list, stream);

            //    //Основной Font для pdf документа
            //    BaseFont baseFont = BaseFont.CreateFont(Environment.ExpandEnvironmentVariables(@"%systemroot%\fonts\Tahoma.TTF"),
            //                        "CP1251", BaseFont.EMBEDDED);
    
            //    //Создаем PDF документ
            //    Document document = new Document();
            //    using (MemoryStream pdfStream = new MemoryStream())
            //    {
            //        PdfWriter.GetInstance(document, pdfStream);
            //        XmlDocument d = new XmlDocument();
            //        string str = Encoding.UTF8.GetString(stream.ToArray()).Substring(1);
            //        d.LoadXml(str);
            //        //Определяем преобразователь из xml в pdf
            //        ITextHandler h = new ITextHandler(document) {DefaultFont = baseFont};
            //        h.Parse(d);
            //        //Возвращаем полученный pdf файл в формате byte[]
            //        return pdfStream.ToArray();
            //    }
            //}

            return null;
        }
    }
}

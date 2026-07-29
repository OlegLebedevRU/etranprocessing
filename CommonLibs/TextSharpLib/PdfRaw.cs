using iTextSharp.text;
using iTextSharp.text.html.simpleparser;
using iTextSharp.text.pdf;
using iTextSharp.text.xml;
using PdfCommon.Interfaces;
using System;
using System.IO;
using System.Text;
using System.Xml;
using System.Xml.Xsl;

namespace TextSharpLib
{
    public class PdfRaw: IPdfRaw
    {
        readonly XslCompiledTransform _xslTransform = new XslCompiledTransform();
        readonly XsltArgumentList _xsltArgumentList = new XsltArgumentList();
        private readonly BaseFont _baseFont;
        private iTextSharp.text.Font font;


        public byte[]  CreatePdf(string html)
        {
            MemoryStream msOutput = new MemoryStream();
            TextReader reader = new StringReader(html);

            // step 1: creation of a document-object
            //Document document = new Document(PageSize.A4, 30, 30, 30, 30);
            Document document = new Document(PageSize.A4);

            // step 2:
            // we create a writer that listens to the document
            // and directs a XML-stream to a file
            PdfWriter writer = PdfWriter.GetInstance(document, msOutput);

            // step 3: we create a worker parse the document
            HTMLWorker worker = new HTMLWorker(document);

            // step 4: we open document and start the worker on the document
            document.Open();
            worker.StartDocument();

            // step 5: parse the html into the document
            worker.Parse(reader);

            // step 6: close the document and the worker
            worker.EndDocument();
            worker.Close();
            document.Close();

            return msOutput.ToArray();
        }

        public PdfRaw(string reportProcessorXslt, string logo, string baseFontStringName)
        {
            //Загружаем схему XSLT преобразований
            _xslTransform.Load(reportProcessorXslt);
            //xslTransform.Load(MapPath(@"~\Resources\ReportProcessor.xslt"));
            //Записываем в параметры схемы путь до картинки
            //list.AddParam("picturePath", string.Empty, MapPath(@"~\Resources\Toco.jpg"));
            if (!string.IsNullOrEmpty(logo))
                _xsltArgumentList.AddParam("picturePath", string.Empty, logo);

            //Основной Font для pdf документа
            //baseFont = BaseFont.CreateFont(Environment.ExpandEnvironmentVariables(@"%systemroot%\fonts\Tahoma.TTF"),
            //                    "CP1251", BaseFont.EMBEDDED);
            //_baseFont = BaseFont.CreateFont(Environment.ExpandEnvironmentVariables(baseFontStringName),
            //                    "CP1251", BaseFont.EMBEDDED);
            _baseFont = BaseFont.CreateFont(baseFontStringName, BaseFont.IDENTITY_H, BaseFont.NOT_EMBEDDED);
            //font = new iTextSharp.text.Font(_baseFont, iTextSharp.text.Font.DEFAULTSIZE, iTextSharp.text.Font.NORMAL);



        }

        public byte[] GetPdfRaw(XmlDocument data)
        {
            var doc = new XmlDocument();
            //Загружаем данные их xml файла
            //doc.Load(MapPath(@"~\Resources\UserProfile.xml"));
            doc.LoadXml(data.OuterXml);

            //Создаем поток в памяти, куда будет писаться наша xml схема для pdf документа
            using (var stream = new MemoryStream())
            {
                //Создаем xml схему нашего pdf документа
                _xslTransform.Transform(doc, _xsltArgumentList, stream);
                //69х157
                //Создаем PDF документ
                //var m = 2.8355387523629489603024574669187;
                //float margin = (float) (1*m);
                //float x = (float)(69 * m);
                //float y = (float) (157*m);

                //var document = new Document(new Rectangle(x, y), margin, margin, margin, margin);
                var document = new Document();
                using (var pdfStream = new MemoryStream())
                {
                    PdfWriter.GetInstance(document, pdfStream);
                    var d = new XmlDocument();
                    string str = Encoding.UTF8.GetString(stream.ToArray()).Substring(1);
                    d.LoadXml(str);
                    //Определяем преобразователь из xml в pdf
                    var h = new ITextHandler(document) { DefaultFont = _baseFont };
                    h.Parse(d);
                    //Возвращаем полученный pdf файл в формате byte[]
                    return pdfStream.ToArray();
                }
            }
        }
    }
}

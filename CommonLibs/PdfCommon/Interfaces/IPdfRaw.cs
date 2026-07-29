using System.IO;
using System.Xml;

namespace PdfCommon.Interfaces
{
    public interface IPdfRaw
    {
        byte[] CreatePdf(string html);
        byte[] GetPdfRaw(XmlDocument data);

    }
}

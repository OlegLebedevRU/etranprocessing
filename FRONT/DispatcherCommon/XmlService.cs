using System.IO;
using System.Text;
using System.Xml;
using System.Xml.Serialization;

namespace DispatcherCommon
{
    public class XmlService
    {

        public static T DeSerialize<T>(string xml)
        {
            var serializer = new XmlSerializer(typeof(T));
            var memoryStream = new MemoryStream(xml.StringToByteArray());
            return (T)serializer.Deserialize(memoryStream);
        }


        public static string Serialize(object file)
        {
            string resultString;

            var ns = new XmlSerializerNamespaces();
            ns.Add("", "");

            var xmlWriterSettings = new XmlWriterSettings
            {
                Indent = true,
                OmitXmlDeclaration = false,
                Encoding = Encoding.GetEncoding(1251)
            };

            using (var memoryStream = new MemoryStream())
            using (var xmlWriter = XmlWriter.Create(memoryStream, xmlWriterSettings))
            {
                var x = new XmlSerializer(file.GetType());
                x.Serialize(xmlWriter, file, ns);
                memoryStream.Position = 0; // rewind the stream before reading back.
                using (var sr = new StreamReader(memoryStream, Encoding.GetEncoding(1251)))
                {
                    resultString = sr.ReadToEnd();
                } // note memory stream disposed by StreamReaders Dispose()
            }
            return resultString;
        }
    }
}
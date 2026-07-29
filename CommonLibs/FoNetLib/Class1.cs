using System.Drawing;
using Fonet;
using Fonet.Render.Pdf;
using iTextSharp.text.pdf;
using PdfCommon.Interfaces;
using System;
using System.Drawing.Imaging;
using System.IO;
using System.Xml;

namespace FoNetLib
{
    public class FonetPdf : IPdfRaw
    {
        public static byte[] CreateBarcodeImage2(string barcode)
        {
            BarcodeEAN barCodeEan = new BarcodeEAN();

            barCodeEan.CodeType = iTextSharp.text.pdf.Barcode.EAN13;
            //barCodeEan.Code = barcode;
            barCodeEan.Code = "1234567891234";
            barCodeEan.BarHeight = 36;

            var image = barCodeEan.CreateDrawingImage(System.Drawing.Color.Black, System.Drawing.Color.White);
            MemoryStream ms = new MemoryStream();
            image.Save(ms, ImageFormat.Gif);
            image.Save("image1.gif");
            return ms.GetBuffer();

        }

        public static byte[] CreateBarcodeImage(string barcode)
        {
            var b = new BarcodeLib.Barcode();
            var image = b.Encode(BarcodeLib.TYPE.Interleaved2of5, barcode.Trim(), Color.Black, Color.White, 95, 36);
            var ms = new MemoryStream();
            image.Save(ms, ImageFormat.Gif);
            return ms.GetBuffer();
        }

        

        private static byte[] ImageHandler(string input)
        {
            long result;
            if (string.IsNullOrEmpty(input))// || input.Length != 13 || !long.TryParse(input, out result))
            {
                return null; //fo performs normal processing
            }

            return CreateBarcodeImage(input);
        }
        public  byte[] MakePdf(XmlDocument xslFoDocument)
        {
            PdfRendererOptions options = new PdfRendererOptions();
            options.FontType = FontType.Subset;
            options.Kerning = false;

            FonetDriver driver = FonetDriver.Make();
            driver.CloseOnExit = true;

            driver.Options = options;
            driver.ImageHandler = new FonetDriver.FonetImageHandler(ImageHandler);//
            byte[] pdfBytes = new byte[0];

            using (MemoryStream ms = new MemoryStream())
            {
                driver.Render(xslFoDocument, ms);
                pdfBytes = ms.ToArray();
            }

            return pdfBytes;
        }
        public byte[] CreatePdf(string html)
        {
            throw new NotImplementedException();
        }

        public byte[] GetPdfRaw(XmlDocument data)
        {
            throw new NotImplementedException();
        }
    }
}

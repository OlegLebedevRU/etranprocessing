using System;
using System.Linq;
using System.Text;

namespace DispatcherCommon
{
    public static class ByteExtension
    {

        /// <summary>
        /// UTs the f8 byte array to string.
        /// </summary>
        /// <param name="characters">The characters.</param>
        /// <returns></returns>
        public static String Utf8ByteArrayToString(this Byte[] characters)
        {
            var encoding = new UTF8Encoding();
            return encoding.GetString(characters, 0, characters.Length);
        }

        /// <summary>
        /// Strings to UT f8 byte array.
        /// </summary>
        /// <param name="byteString">The p XML string.</param>
        /// <returns></returns>
        public static Byte[] StringToUtf8ByteArray(this String byteString)
        {
            var encoding = new UTF8Encoding();
            var byteArray = encoding.GetBytes(byteString);
            return byteArray;
        }

        public static Byte[] StringToByteArray(this String byteString)
        {
            var encoding = Encoding.Default;
            var byteArray = encoding.GetBytes(byteString);
            return byteArray;
        }

        public static string ByteArrayToHexString(this byte[] ba)
        {
            return ba == null ? String.Empty : ba.Aggregate(string.Empty, (c, n) => c + n.ToString("X2"));
        }

        public static Byte[] HexStringToByteArray(this String str)
        {
            if ((str.Length % 2) != 0)
                return new byte[0];

            byte[] b = new byte[str.Length / 2];
            int j = 0;
            for (int i = 0; i < str.Length; i += 2)
            {
                string sub = str.Substring(i, 2);
                b[j++] = byte.Parse(sub, System.Globalization.NumberStyles.HexNumber);
            }
            return b;

        }

    }
}
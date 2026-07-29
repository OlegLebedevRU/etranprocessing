namespace EtranLib
{

namespace Utility
    {
        using System;
        using System.Text;


        /// <summary>
        /// Summary description for HexEncoding.
        /// </summary>
        public class HexEncoding
        {
            public HexEncoding()
            {
                //
                // TODO: Add constructor logic here
                //
            }
            public static int GetByteCount(string hexString)
            {
                int numHexChars = 0;
                char c;
                // remove all none A-F, 0-9, characters
                for (int i = 0; i < hexString.Length; i++)
                {
                    c = hexString[i];
                    if (IsHexDigit(c))
                        numHexChars++;
                }
                // if odd number of characters, discard last character
                if (numHexChars % 2 != 0)
                {
                    numHexChars--;
                }
                return numHexChars / 2; // 2 characters per byte
            }
            /// <summary>
            /// Creates a byte array from the hexadecimal string. Each two characters are combined
            /// to create one byte. First two hexadecimal characters become first byte in returned array.
            /// Non-hexadecimal characters are ignored. 
            /// </summary>
            /// <param name="hexString">string to convert to byte array</param>
            /// <param name="discarded">number of characters in string ignored</param>
            /// <returns>byte array, in the same left-to-right order as the hexString</returns>
            public static byte[] GetBytes(string hexString, out int discarded)
            {
                discarded = 0;
                string newString = "";
                char c;
                // remove all none A-F, 0-9, characters
                for (int i = 0; i < hexString.Length; i++)
                {
                    c = hexString[i];
                    if (IsHexDigit(c))
                        newString += c;
                    else
                        discarded++;
                }
                // if odd number of characters, discard last character
                if (newString.Length % 2 != 0)
                {
                    discarded++;
                    newString = newString.Substring(0, newString.Length - 1);
                }

                int byteLength = newString.Length / 2;
                byte[] bytes = new byte[byteLength];
                string hex;
                int j = 0;
                for (int i = 0; i < bytes.Length; i++)
                {
                    hex = new String(new Char[] { newString[j], newString[j + 1] });
                    bytes[i] = HexToByte(hex);
                    j = j + 2;
                }
                return bytes;
            }
            public static string ToString(byte[] bytes)
            {
                string hexString = "";
                for (int i = 0; i < bytes.Length; i++)
                {
                    hexString += bytes[i].ToString("X2");
                }
                return hexString;
            }
            /// <summary>
            /// Determines if given string is in proper hexadecimal string format
            /// </summary>
            /// <param name="hexString"></param>
            /// <returns></returns>
            public static bool InHexFormat(string hexString)
            {
                bool hexFormat = true;

                foreach (char digit in hexString)
                {
                    if (!IsHexDigit(digit))
                    {
                        hexFormat = false;
                        break;
                    }
                }
                return hexFormat;
            }

            /// <summary>
            /// Returns true is c is a hexadecimal digit (A-F, a-f, 0-9)
            /// </summary>
            /// <param name="c">Character to test</param>
            /// <returns>true if hex digit, false if not</returns>
            public static bool IsHexDigit(Char c)
            {
                int numChar;
                int numA = Convert.ToInt32('A');
                int num1 = Convert.ToInt32('0');
                c = Char.ToUpper(c);
                numChar = Convert.ToInt32(c);
                if (numChar >= numA && numChar < (numA + 6))
                    return true;
                if (numChar >= num1 && numChar < (num1 + 10))
                    return true;
                return false;
            }
            /// <summary>
            /// Converts 1 or 2 character string into equivalant byte value
            /// </summary>
            /// <param name="hex">1 or 2 character string</param>
            /// <returns>byte</returns>
            private static byte HexToByte(string hex)
            {
                if (hex.Length > 2 || hex.Length <= 0)
                    throw new ArgumentException("hex must be 1 or 2 characters in length");
                byte newByte = byte.Parse(hex, System.Globalization.NumberStyles.HexNumber);
                return newByte;
            }


        }
    }

namespace Xml
{
    using System.Xml;
    using System.Collections;
    using System.Collections.Specialized;

    class XmlClass
    {
        static public XmlElement CreateElement(XmlDocument doc, string el_name, NameValueCollection attr)
        {
            return CreateElement(doc, el_name, attr, null);
        }

        static public XmlElement CreateElement(XmlDocument doc, string el_name, NameValueCollection attr, string InnerText)
        {
            XmlElement field = doc.CreateElement(el_name);//, "http://payment.beepayxp.jetinfosoft.ru");
            if (InnerText != null)
                field.InnerText = InnerText;
            if (attr != null)
                for (int i = 0; i < attr.Count; i++)
                {
                    XmlAttribute xml_attr = doc.CreateAttribute(attr.Keys[i]);
                    xml_attr.Value = attr[i];
                    field.Attributes.Append(xml_attr);
                }
            return field;
        }

        static public void InsertInnerText(XmlDocument xmldoc, XmlNode xmlnode, NameValueCollection InnerText)
        {
            IEnumerator EmpEnumerator = InnerText.GetEnumerator();
            EmpEnumerator.Reset();
            while (EmpEnumerator.MoveNext())
            {
                string name = (string)EmpEnumerator.Current;
                string val = InnerText[name];
                XmlElement el = CreateElement(xmldoc, name, null, val);
                xmlnode.AppendChild(el);
            }
        }
    }
}
    namespace Math
    {
        using System;
        public class TrendLine
        {


            static public double[] CalcNewYAxis(int n, double[] y, double[] x)
            {
                double m_y_sum = 0;
                double m_x_sum = 0;
                double m_x_sq_sum = 0;
                double m_x_sum_sq = 0;
                double m_xy_sum = 0;
                double m_slope = 0;
                double m_intercept = 0;

                for (int i = 0; i < n; i++)
                {
                    m_xy_sum += (y[i] * x[i]);
                    m_y_sum += y[i];
                    m_x_sum += x[i];
                    m_x_sum_sq += Math.Pow(x[i], 2);
                }

                m_x_sq_sum = Math.Pow(m_x_sum, 2);
                m_slope = (n * m_xy_sum - m_x_sum * m_y_sum) / (n * m_x_sum_sq - m_x_sq_sum);
                m_intercept = (m_y_sum - m_slope * m_x_sum) / n;

                double[] y_new = new double[n];
                for (int i = 0; i < n; i++)
                    y_new[i] = m_slope * x[i] + m_intercept;

                return y_new;
            }
        }
    }

    namespace Data
    {
        using System;
        using System.Xml;
        using System.Data.SqlClient;
        using System.Data;
        using System.Collections;
        using System.Collections.Specialized;
        using System.IO;

        public class Utilities
        {
            /// <summary>
            /// method to read a text file into a DataSet
            /// </summary>
            /// <param name="file">file to read from</param>
            /// <param name="tableName">name of the DataTable we want to add</param>
            /// <param name="delimeter">delimiter to split on</param>
            /// <returns>a populated DataSet</returns>
            public DataSet BuildDataSet(string file, string tableName, string delimeter)
            {
                //create our DataSet
                DataSet domains = new DataSet();
                //add our table
                domains.Tables.Add(tableName);
                try
                {
                    //first make sure the file exists
                    if (File.Exists(file))
                    {
                        //create a StreamReader and open our text file
                        StreamReader reader = new StreamReader(file);
                        //read the first line in and split it into columns
                        string[] columns = reader.ReadLine().Split(delimeter.ToCharArray());
                        //now add our columns (we will check to make sure the column doesnt exist before adding it)
                        foreach (string col in columns)
                        {
                            //variable to determine if a column has been added
                            bool added = false;
                            string next = "";
                            //our counter
                            int i = 0;
                            while (!(added))
                            {
                                string columnName = col;
                                //now check to see if the column already exists in our DataTable
                                if (!(domains.Tables[tableName].Columns.Contains(columnName)))
                                {
                                    //since its not in our DataSet we will add it
                                    domains.Tables[tableName].Columns.Add(columnName, typeof(string));
                                    added = true;
                                }
                                else
                                {
                                    //we didnt add the column so increment out counter
                                    i++;
                                }
                            }
                        }
                        //now we need to read the rest of the text file
                        string data = reader.ReadToEnd();
                        //now we will split the file on the carriage return/line feed
                        //and toss it into a string array
                        string[] rows = data.Split("\r".ToCharArray());
                        //now we will add the rows to our DataTable
                        foreach (string r in rows)
                        {
                            string[] items = r.Split(delimeter.ToCharArray());
                            //split the row at the delimiter
                            domains.Tables[tableName].Rows.Add(items);
                        }
                    }
                    else
                    {
                        throw new FileNotFoundException("The file " + file + " could not be found");
                    }

                }
                catch (FileNotFoundException ex)
                {
                    //_message = ex.Message;
                    return null;
                }
                catch (Exception ex)
                {
                    //_message = ex.Message;
                    return null;
                }

                //now return the DataSet
                return domains;
            }


            ////Sample usage

            ////for a Windows application
            //DataSet data = BuildDataSet("C:\MyFile.txt","MyTable",",");

            ////For an ASP.Net application
            //DataSet data = BuildDataSet(Server.MapPath("MyFile.txt"),"MyTable",",");

        }

        public class DBManager
        {
            public enum DataReadType { DataSet, ExecuteNonQuery, Hashtable, ArrayList, ExecuteScalar, ExecuteXmlReader, NameValueCollection };

            SqlConnection connection = null;
            readonly string constr = string.Empty;
            SqlTransaction tran = null;
            SqlCommand command;

            public DBManager(string _constr)
            {
                constr = _constr;
                connection = new SqlConnection(constr);
                connection.Open();
                command = new SqlCommand(constr, connection);
            }

            public void Close()
            {
                connection.Close();
            }

            public void TranOpen()
            {
                tran = connection.BeginTransaction();
                command.Transaction = tran;
            }
            public void TranRollback()
            {
                tran.Rollback();
                command.Transaction = null;
            }

            public void TranCommit()
            {
                tran.Commit();
                command.Transaction = null;
            }


            public object Execute(string sp_name, CommandType ct, DataReadType rt, NameValueCollection namedValues, params object[] objValues)
            {

                command.CommandText = sp_name;
                command.CommandType = ct;

                if (ct == CommandType.StoredProcedure && (namedValues != null || objValues!=null))
                {
                    SqlCommandBuilder.DeriveParameters(command);
                    int index = 0;
                    foreach (SqlParameter parameter in command.Parameters)
                    {
                        if (parameter.Direction == ParameterDirection.Input || parameter.Direction == ParameterDirection.InputOutput)
                        {
                            if (namedValues != null)
                            {
                                parameter.Value = namedValues[parameter.ParameterName.Replace("@", "")];
                            }
                            else
                            {
                                parameter.Value = objValues[index];
                            }
                            index++;
                        }
                    }
                }
                switch (rt)
                {

                    case DataReadType.DataSet:
                        {
                            SqlDataAdapter custDA = new SqlDataAdapter();
                            custDA.SelectCommand = command;
                            DataSet ds = new DataSet();
                            custDA.Fill(ds);
                            custDA.Dispose();
                            return ds;
                        }

                    case DataReadType.ExecuteNonQuery:
                        {
                            return command.ExecuteNonQuery();
                        }
                    case DataReadType.NameValueCollection:
                        {
                            NameValueCollection output = new NameValueCollection();
                            SqlDataReader reader = command.ExecuteReader();
                            if (reader.Read())
                                for (int i = 0; i < reader.FieldCount; i++)
                                    output.Add( reader.GetName(i) , reader[i].ToString());
                            reader.Close();
                            return output;
                        }

                    case DataReadType.Hashtable:
                        {
                            Hashtable output = new Hashtable();
                            SqlDataReader reader = command.ExecuteReader();
                            if(reader.Read())
                                for (int i = 0; i < reader.FieldCount; i++)
                                    output.Add(reader.GetName(i), reader[i]);
                            reader.Close();
                            return output;
                        }
                    case DataReadType.ArrayList:
                        {
                            ArrayList output = new ArrayList();
                            SqlDataReader reader = command.ExecuteReader();
                            while (reader.Read())
                                    output.Add(reader[0]);
                            reader.Close();
                            return output;
                        }
                    case DataReadType.ExecuteScalar:
                        {
                            return command.ExecuteScalar();
                        }
                    case DataReadType.ExecuteXmlReader:
                        {
                            XmlReader xmlr = command.ExecuteXmlReader();
                            xmlr.Read();
                            string xml_str = string.Empty;
                            while (xmlr.ReadState != System.Xml.ReadState.EndOfFile)
                            {
                                xml_str += xmlr.ReadOuterXml();
                            }
                            xmlr.Close();
                            return xml_str;
                        }
                }
                return null;
            }
        }
    }

    namespace Files
    {
        using System;
        using System.IO;

        public class FileDB
        {
            private string file_name = string.Empty;

            public FileDB(string _file_name)
            {
                file_name = _file_name;
                if (!File.Exists(file_name))
                {
                    File.Create(file_name);
                }

            }

            public void AddRec(string rec)
            {
                lock (this)
                {
                    using (StreamWriter write = new StreamWriter(file_name, true))
                    {
                        write.WriteLine(rec);
                    }
                }
            }

            public bool IsExist(string rec)
            {
                string allRead = string.Empty;
                lock (this)
                {
                    using (StreamReader read = new StreamReader(file_name))
                    {
                        allRead = read.ReadToEnd();
                    }
                }
                if (allRead.IndexOf(rec) > -1)
                    return true;
                return false;
            }


        }
    }
    
    namespace RandomAndPassword
    {
        using System;
        using System.Security.Cryptography;
        using System.Text;

        /// <summary>
        /// This class can generate random passwords, which do not include ambiguous 
        /// characters, such as I, l, and 1. The generated password will be made of
        /// 7-bit ASCII symbols. Every four characters will include one lower case
        /// character, one upper case character, one number, and one special symbol
        /// (such as '%') in a random order. The password will always start with an
        /// alpha-numeric character; it will not start with a special symbol (we do
        /// this because some back-end systems do not like certain special
        /// characters in the first position).
        /// </summary>
        public class RandomPassword
        {
            // Define default min and max password lengths.
            private static int DEFAULT_MIN_PASSWORD_LENGTH = 8;
            private static int DEFAULT_MAX_PASSWORD_LENGTH = 10;

            // Define supported password characters divided into groups.
            // You can add (or remove) characters to (from) these groups.
            //private static string PASSWORD_CHARS_LCASE  = "abcdefgijkmnopqrstwxyz";
            //private static string PASSWORD_CHARS_LCASE = "abcdef";
            //private static string PASSWORD_CHARS_UCASE  = "ABCDEFGHJKLMNPQRSTWXYZ";
            private static string PASSWORD_CHARS_NUMERIC = "123456789";
            //private static string PASSWORD_CHARS_SPECIAL= "*$-+?_&=!%{}/";

            /// <summary>
            /// Generates a random password.
            /// </summary>
            /// <returns>
            /// Randomly generated password.
            /// </returns>
            /// <remarks>
            /// The length of the generated password will be determined at
            /// random. It will be no shorter than the minimum default and
            /// no longer than maximum default.
            /// </remarks>
            public static string Generate()
            {
                return Generate(DEFAULT_MIN_PASSWORD_LENGTH,
                    DEFAULT_MAX_PASSWORD_LENGTH);
            }

            /// <summary>
            /// Generates a random password of the exact length.
            /// </summary>
            /// <param name="length">
            /// Exact password length.
            /// </param>
            /// <returns>
            /// Randomly generated password.
            /// </returns>
            public static string Generate(int length)
            {
                return Generate(length, length);
            }

            /// <summary>
            /// Generates a random password.
            /// </summary>
            /// <param name="minLength">
            /// Minimum password length.
            /// </param>
            /// <param name="maxLength">
            /// Maximum password length.
            /// </param>
            /// <returns>
            /// Randomly generated password.
            /// </returns>
            /// <remarks>
            /// The length of the generated password will be determined at
            /// random and it will fall with the range determined by the
            /// function parameters.
            /// </remarks>
            public static string Generate(int minLength,
                int maxLength)
            {
                // Make sure that input parameters are valid.
                if (minLength <= 0 || maxLength <= 0 || minLength > maxLength)
                    return null;

                // Create a local array containing supported password characters
                // grouped by types. You can remove character groups from this
                // array, but doing so will weaken the password strength.
                char[][] charGroups = new char[][] 
		{
			//PASSWORD_CHARS_LCASE.ToCharArray(),
			//PASSWORD_CHARS_UCASE.ToCharArray(),
			PASSWORD_CHARS_NUMERIC.ToCharArray(),
			//PASSWORD_CHARS_SPECIAL.ToCharArray()
		};

                // Use this array to track the number of unused characters in each
                // character group.
                int[] charsLeftInGroup = new int[charGroups.Length];

                // Initially, all characters in each group are not used.
                for (int i = 0; i < charsLeftInGroup.Length; i++)
                    charsLeftInGroup[i] = charGroups[i].Length;

                // Use this array to track (iterate through) unused character groups.
                int[] leftGroupsOrder = new int[charGroups.Length];

                // Initially, all character groups are not used.
                for (int i = 0; i < leftGroupsOrder.Length; i++)
                    leftGroupsOrder[i] = i;

                // Because we cannot use the default randomizer, which is based on the
                // current time (it will produce the same "random" number within a
                // second), we will use a random number generator to seed the
                // randomizer.

                // Use a 4-byte array to fill it with random bytes and convert it then
                // to an integer value.
                byte[] randomBytes = new byte[4];

                // Generate 4 random bytes.
                RNGCryptoServiceProvider rng = new RNGCryptoServiceProvider();
                rng.GetBytes(randomBytes);

                // Convert 4 bytes into a 32-bit integer value.
                int seed = (randomBytes[0] & 0x7f) << 24 |
                    randomBytes[1] << 16 |
                    randomBytes[2] << 8 |
                    randomBytes[3];

                // Now, this is real randomization.
                Random random = new Random(seed);

                // This array will hold password characters.
                char[] password = null;

                // Allocate appropriate memory for the password.
                if (minLength < maxLength)
                    password = new char[random.Next(minLength, maxLength + 1)];
                else
                    password = new char[minLength];

                // Index of the next character to be added to password.
                int nextCharIdx;

                // Index of the next character group to be processed.
                int nextGroupIdx;

                // Index which will be used to track not processed character groups.
                int nextLeftGroupsOrderIdx;

                // Index of the last non-processed character in a group.
                int lastCharIdx;

                // Index of the last non-processed group.
                int lastLeftGroupsOrderIdx = leftGroupsOrder.Length - 1;

                // Generate password characters one at a time.
                for (int i = 0; i < password.Length; i++)
                {
                    // If only one character group remained unprocessed, process it;
                    // otherwise, pick a random character group from the unprocessed
                    // group list. To allow a special character to appear in the
                    // first position, increment the second parameter of the Next
                    // function call by one, i.e. lastLeftGroupsOrderIdx + 1.
                    if (lastLeftGroupsOrderIdx == 0)
                        nextLeftGroupsOrderIdx = 0;
                    else
                        nextLeftGroupsOrderIdx = random.Next(0,
                            lastLeftGroupsOrderIdx);

                    // Get the actual index of the character group, from which we will
                    // pick the next character.
                    nextGroupIdx = leftGroupsOrder[nextLeftGroupsOrderIdx];

                    // Get the index of the last unprocessed characters in this group.
                    lastCharIdx = charsLeftInGroup[nextGroupIdx] - 1;

                    // If only one unprocessed character is left, pick it; otherwise,
                    // get a random character from the unused character list.
                    if (lastCharIdx == 0)
                        nextCharIdx = 0;
                    else
                        nextCharIdx = random.Next(0, lastCharIdx + 1);

                    // Add this character to the password.
                    password[i] = charGroups[nextGroupIdx][nextCharIdx];

                    // If we processed the last character in this group, start over.
                    if (lastCharIdx == 0)
                        charsLeftInGroup[nextGroupIdx] =
                            charGroups[nextGroupIdx].Length;
                    // There are more unprocessed characters left.
                    else
                    {
                        // Swap processed character with the last unprocessed character
                        // so that we don't pick it until we process all characters in
                        // this group.
                        if (lastCharIdx != nextCharIdx)
                        {
                            char temp = charGroups[nextGroupIdx][lastCharIdx];
                            charGroups[nextGroupIdx][lastCharIdx] =
                                charGroups[nextGroupIdx][nextCharIdx];
                            charGroups[nextGroupIdx][nextCharIdx] = temp;
                        }
                        // Decrement the number of unprocessed characters in
                        // this group.
                        charsLeftInGroup[nextGroupIdx]--;
                    }

                    // If we processed the last group, start all over.
                    if (lastLeftGroupsOrderIdx == 0)
                        lastLeftGroupsOrderIdx = leftGroupsOrder.Length - 1;
                    // There are more unprocessed groups left.
                    else
                    {
                        // Swap processed group with the last unprocessed group
                        // so that we don't pick it until we process all groups.
                        if (lastLeftGroupsOrderIdx != nextLeftGroupsOrderIdx)
                        {
                            int temp = leftGroupsOrder[lastLeftGroupsOrderIdx];
                            leftGroupsOrder[lastLeftGroupsOrderIdx] =
                                leftGroupsOrder[nextLeftGroupsOrderIdx];
                            leftGroupsOrder[nextLeftGroupsOrderIdx] = temp;
                        }
                        // Decrement the number of unprocessed groups.
                        lastLeftGroupsOrderIdx--;
                    }
                }

                // Convert password characters into a string and return the result.
                return new string(password);
            }

            static public byte[] HexStringToByteArray(string str, int size_default)
            {

                if (str == null && size_default > 0)
                    str = RandomPassword.Generate(size_default * 2, size_default * 2);
                else
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

        /// <summary>
        /// Illustrates the use of the RandomPassword class.
        /// </summary>
        /// 

        public class RandomPasswordTest
        {
            /// <summary>
            /// The main entry point for the application.
            /// </summary>
            //[STAThread]
            //static void Main(string[] args)
            //{
            // Print 100 randomly generated passwords (8-to-10 char long).
            /*
            for (int i=0; i<10; i++)
            {
                string f= RandomPassword.Generate(32, 32);
                Console.WriteLine(f+ " LEN: "+f.Length/2);
            }
            */
            /*
            string f= RandomPassword.Generate(32, 32);
            Console.WriteLine(f+ " LEN: "+f.Length/2);

            byte	[]d = Encoding.Default.GetBytes(f);
            Console.WriteLine(ClientInterface.AToString(ref d));
            */
            /*
            string str = "ACC9596327B85E205BF181951AF36B53";
            byte	[]b =  new byte[str.Length];
            int j=0;
            for (int i = 0; i < str.Length; i+=2)
            {
                // den Hex-Wert aus dem langen String holen..
                string sub = str.Substring(i,2);
                // Parse-Methode von byte aufrufen
                // Parameter1: Der Hex-String
                // Parameter2: Eine Angabe das es sich dabei um Hex handelt..
                b[j++] = byte.Parse(sub, System.Globalization.NumberStyles.HexNumber);
                //Console.WriteLine(sub+ " -> " + b.ToString());
            }
            */
            //byte	[]b =  RandomPassword.HexStringToByteArray("1234567891",10);
            //Console.WriteLine(ClientInterface.AToString(ref b));



            //}
        }


        public class PasswordGenerator
        {
            public PasswordGenerator()
            {
                this.Minimum = DefaultMinimum;
                this.Maximum = DefaultMaximum;
                this.ConsecutiveCharacters = false;
                this.RepeatCharacters = true;
                this.ExcludeSymbols = false;
                this.Exclusions = null;

                rng = new RNGCryptoServiceProvider();
            }

            protected int GetCryptographicRandomNumber(int lBound, int uBound)
            {
                // Assumes lBound >= 0 && lBound < uBound
                // returns an int >= lBound and < uBound
                uint urndnum;
                byte[] rndnum = new Byte[4];
                if (lBound == uBound - 1)
                {
                    // test for degenerate case where only lBound can be returned   
                    return lBound;
                }

                uint xcludeRndBase = (uint.MaxValue - (uint.MaxValue % (uint)(uBound - lBound)));

                do
                {
                    rng.GetBytes(rndnum);
                    urndnum = System.BitConverter.ToUInt32(rndnum, 0);
                } while (urndnum >= xcludeRndBase);

                return (int)(urndnum % (uBound - lBound)) + lBound;
            }

            protected char GetRandomCharacter()
            {
                int upperBound = pwdCharArray.GetUpperBound(0);

                if (true == this.ExcludeSymbols)
                {
                    upperBound = PasswordGenerator.UBoundDigit;
                }

                int randomCharPosition = GetCryptographicRandomNumber(pwdCharArray.GetLowerBound(0), upperBound);

                char randomChar = pwdCharArray[randomCharPosition];

                return randomChar;
            }

            public string Generate()
            {
                // Pick random length between minimum and maximum   
                int pwdLength = GetCryptographicRandomNumber(this.Minimum, this.Maximum);

                StringBuilder pwdBuffer = new StringBuilder();
                pwdBuffer.Capacity = this.Maximum;

                // Generate random characters
                char lastCharacter, nextCharacter;

                // Initial dummy character flag
                lastCharacter = nextCharacter = '\n';

                for (int i = 0; i < pwdLength; i++)
                {
                    nextCharacter = GetRandomCharacter();

                    if (false == this.ConsecutiveCharacters)
                    {
                        while (lastCharacter == nextCharacter)
                        {
                            nextCharacter = GetRandomCharacter();
                        }
                    }

                    if (false == this.RepeatCharacters)
                    {
                        string temp = pwdBuffer.ToString();
                        int duplicateIndex = temp.IndexOf(nextCharacter);
                        while (-1 != duplicateIndex)
                        {
                            nextCharacter = GetRandomCharacter();
                            duplicateIndex = temp.IndexOf(nextCharacter);
                        }
                    }

                    if ((null != this.Exclusions))
                    {
                        while (-1 != this.Exclusions.IndexOf(nextCharacter))
                        {
                            nextCharacter = GetRandomCharacter();
                        }
                    }

                    pwdBuffer.Append(nextCharacter);
                    lastCharacter = nextCharacter;
                }

                if (null != pwdBuffer)
                {
                    return pwdBuffer.ToString();
                }
                else
                {
                    return String.Empty;
                }
            }

            public string Exclusions
            {
                get { return this.exclusionSet; }
                set { this.exclusionSet = value; }
            }

            public int Minimum
            {
                get { return this.minSize; }
                set
                {
                    this.minSize = value;
                    if (PasswordGenerator.DefaultMinimum > this.minSize)
                    {
                        this.minSize = PasswordGenerator.DefaultMinimum;
                    }
                }
            }

            public int Maximum
            {
                get { return this.maxSize; }
                set
                {
                    this.maxSize = value;
                    if (this.minSize >= this.maxSize)
                    {
                        this.maxSize = PasswordGenerator.DefaultMaximum;
                    }
                }
            }

            public bool ExcludeSymbols
            {
                get { return this.hasSymbols; }
                set { this.hasSymbols = value; }
            }

            public bool RepeatCharacters
            {
                get { return this.hasRepeating; }
                set { this.hasRepeating = value; }
            }

            public bool ConsecutiveCharacters
            {
                get { return this.hasConsecutive; }
                set { this.hasConsecutive = value; }
            }

            private const int DefaultMinimum = 6;
            private const int DefaultMaximum = 10;
            private const int UBoundDigit = 61;

            private RNGCryptoServiceProvider rng;
            private int minSize;
            private int maxSize;
            private bool hasRepeating;
            private bool hasConsecutive;
            private bool hasSymbols;
            private string exclusionSet;
            private char[] pwdCharArray = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`~!@#$%^&*()-_=+[]{}\\|;:'\",<.>/?".ToCharArray();
        }
    }
    
    namespace ScheduleTimer
    {
        using System.Timers;
        using System;

        public class ScheduleTimer
        {
            public enum DateTimeType { Daily };
            public delegate void OnEventOccur();

            private DateTimeType type;
            private DateTime eventoccur;
            private int rate;
            Timer aTimer;
            static public bool rrr = true;
            private OnEventOccur funOnEventOccur;

            //ReestrUpload =
            //    new ScheduleTimer(GateMegafonSZ.bl.ReestrUpload, new DateTime(DateTime.Now.Year, DateTime.Now.Month,
            //    DateTime.Now.Day, EtranConfigurationManager.HourUpload, 0, 0),
            //    ScheduleTimer.DateTimeType.Daily, EtranConfigurationManager.RateTimer);

            private void OnTimedEvent(object source, ElapsedEventArgs e)
            {
                //GlobalObjectsManager.Logger.Info("next time: " + eventoccur.ToString());
                if (DateTime.Now >= eventoccur)
                {
                    //GlobalObjectsManager.Logger.Info("it's time: " + eventoccur.ToString());
                    eventoccur += new TimeSpan(1, 0, 0, 0, 0);
                    (new System.Threading.Thread(new System.Threading.ThreadStart(funOnEventOccur))).Start();
                }
                //   else
                //       Debug.WriteLine("NOT RUN");
            }


            public void ResetTimer()
            {
                if (eventoccur != null)
                    eventoccur -= new TimeSpan(1, 0, 0, 0, 0);
            }

            public ScheduleTimer(OnEventOccur fun, DateTime _eventoccur, DateTimeType _type, int _rate)
            {
                funOnEventOccur = fun;
                eventoccur = _eventoccur;
                type = _type;
                rate = _rate;
                aTimer = new Timer(rate);
                aTimer.Elapsed += new ElapsedEventHandler(OnTimedEvent);
                aTimer.AutoReset = true;
                aTimer.Enabled = true;
            }

        }
    }

    namespace Collection
    {
        using System.Collections.Specialized;
        using System;
        public class Collection
        {
            static public NameValueCollection GetNameValueCollection(string data, string delimiter, string split)
            {
                string[] m = data.Split(delimiter.ToCharArray(0, delimiter.Length));
                NameValueCollection result = new NameValueCollection();
                foreach (string s in m)
                {
                    string[] m1 = s.Split(split.ToCharArray(0, split.Length));
                    if (m1.Length != 2) continue;
                    result.Add(m1[0].Trim(), m1[1]);
                }
                return result;
            }
            static public NameValueCollection GetNameValueCollection(string Params)
            {
                NameValueCollection paramsTable = new NameValueCollection();
                string[] paramEntries = Params.Split(';');
                if (paramEntries.Length > 1 || paramEntries[0] != string.Empty)
                {
                    foreach (string param in paramEntries)
                    {
                        int indx;
                        string key = string.Empty;
                        string val = string.Empty;

                        try
                        {
                            indx = param.IndexOf(' ');
                            if (indx < 0)
                                indx = param.IndexOf('=');

                            key = param.Substring(0, indx);
                            val = param.Substring(indx + 1, param.Length - indx - 1);
                        }
                        catch (Exception ex)
                        {
                            int h = 0;
                        }
                        paramsTable.Add(key, val);
                    }
                }
                return paramsTable;
            }

        }
    }

    namespace Math
    {
        using System;
        public class MathEtran
        {
            public static double Round(double value, int digits)
            {
                double scale = Math.Pow(10.0, digits);
                double round = Math.Floor(Math.Abs(value) * scale + 0.5);
                return (Math.Sign(value) * round / scale);
            }
        }
    }

    namespace Net
    {
        using System.Net;
        using System.Net.Sockets;
        using System.IO;
        using System.Text;
        using System.Security.Cryptography.X509Certificates;

        public class Net
        {
            //  <system.net>
            //    <settings>
            //      <servicePointManager expect100Continue="false" />
            //    </settings>
            //  </system.net>
            static public void Init()
            {
                ServicePointManager.ServerCertificateValidationCallback += new System.Net.Security.RemoteCertificateValidationCallback(CustomValidation);
                ServicePointManager.SecurityProtocol = System.Net.SecurityProtocolType.Ssl3;
            }

            private static bool CustomValidation(object sender, X509Certificate cert, X509Chain chain, System.Net.Security.SslPolicyErrors error) { return true; }
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

            static public string XmlPost(string url, int timeout, string _data)
            {
                HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
                wrq.Timeout = timeout;
                wrq.Method = "POST";
                wrq.ContentType = "text/xml; charset=windows-1251";
                byte[] data = Encoding.Default.GetBytes(_data);
                wrq.ContentLength = data.Length;
                Stream newStream = wrq.GetRequestStream();
                newStream.Write(data, 0, data.Length);
                newStream.Close();

                HttpWebResponse hwr = wrq.GetResponse() as HttpWebResponse;
                Stream strm = hwr.GetResponseStream();
                StreamReader reader = new StreamReader(strm, Encoding.GetEncoding(1251));
                return reader.ReadToEnd();
            }

            static public string DownloadData(string url)
            {
                WebClient myWebClient = new WebClient();
                return Encoding.GetEncoding(1251).GetString(myWebClient.DownloadData(url));
            }

            static public string FileUpload(string url, string file_name)
            {
                WebClient myWebClient = new WebClient();
                //myWebClient.Credentials = new NetworkCredential(EtranConfigurationManager.Login,EtranConfigurationManager.Password);
                //myWebClient.Headers.Add("P_DATE", P_DATE);
                return Encoding.GetEncoding(1251).GetString(myWebClient.UploadFile(url, "POST", file_name));
            }

            static public byte[] TcpSend(string hostname, int port, byte[] data, int timeout)
            {
                TcpClient client = new TcpClient(hostname, port);
                client.ReceiveTimeout = timeout;
                NetworkStream stream = client.GetStream();
                stream.Write(data, 0, data.Length);

                string ret = string.Empty;
                int i = 0;
                int k = 100;
                while (!stream.DataAvailable && (i++) < timeout / k)
                    System.Threading.Thread.Sleep(k);
                //while(stream.DataAvailable)
                //{
                int len = client.Available;
                //GlobalObjectsManager.Logger.Info("Receive Len: " + len);
                //GlobalObjectsManager.Logger.Info("len: " + len);
                byte[] packet = new byte[len];
                int bytes = stream.Read(packet, 0, len);
                //   ret += Encoding.Default.GetString(packet);
                System.Threading.Thread.Sleep(100);
                //}
                client.Close();
                //return ret;
                return packet;
            }

        }
    }

    namespace Crypto
    {
        using System;
        using System.Text;
        using System.Security.Cryptography;
        using System.Security.Cryptography.X509Certificates;
        using System.Collections;
        using System.Collections.Specialized;

        public class Crypto
        {

            public static void CleanStoreByOU(int ou_term_number, string Rek)
            {
                X509Store store = null;
                try
                {
                    store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                    store.Open(OpenFlags.MaxAllowed);
                    X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                    X509Certificate2Collection found = collection.Find(X509FindType.FindBySubjectName, Rek, false);
                    foreach (X509Certificate2 cert in found)
                    {
                        NameValueCollection nvc_orgs = Collection.Collection.GetNameValueCollection(cert.Subject, ",", "=");
                        string tm = nvc_orgs["OU"];
                        int i_tm = 0;
                        try
                        {
                            i_tm = int.Parse(tm);
                        }
                        catch
                        {
                        }
                        if (ou_term_number == i_tm)
                        {
                            store.Remove(cert);
                        }


                    }

                }
                finally
                {
                    if(store != null)
                        store.Close();
                }
            }

            static public void AddCet(X509Certificate2 cert)
            {
                X509Store store = null;
                try
                {
                    store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                    store.Open(OpenFlags.MaxAllowed);
                    store.Add(cert);
                }
                finally
                {
                    store.Close();
                }
            }


            static public ArrayList CertListSubject(string Rek)
            {
                X509Store store = null;
                try
                {
                    store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                    store.Open(OpenFlags.ReadOnly);
                    X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                    X509Certificate2Collection found = collection.Find(X509FindType.FindBySubjectName, Rek, true);
                    ArrayList list = new ArrayList();
                    for (int i = 0; i < found.Count; i++)
                    {
                        list.Add(found[i].Subject);
                    }
                    return list;
                }
                catch //(Exception ex)
                {
                    //GlobalObjectsManager.Logger.Error("При доступе к пользовательскому сертификату возникло исключение", ex);
                }
                finally
                {
                    store.Close();
                }
                return null;
            }

            static public X509Certificate2Collection GetCertColl(string Rek)
            {
                X509Store store = null;
                try
                {
                    store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                    store.Open(OpenFlags.ReadOnly);
                    X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                    return collection.Find(X509FindType.FindBySubjectName, Rek, true);
                }
                catch //(Exception ex)
                {
                    //GlobalObjectsManager.Logger.Error("При доступе к пользовательскому сертификату возникло исключение", ex);
                }
                finally
                {
                    store.Close();
                }
                return null;
            }

            static public X509Certificate2 GetCertByString(string Rek)
            {
                X509Certificate2 cert = null;
                X509Store store = null;
                try
                {
                    store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                    store.Open(OpenFlags.ReadOnly);
                    X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                    X509Certificate2Collection found = collection.Find(X509FindType.FindBySubjectName, Rek, true);
                    if (found.Count == 1)
                        cert = found[0];
                    else
                        if (found.Count > 1)
                            throw new Exception("найдено больше одного сертификата удовлетворяющему условию: " + Rek);
                        else
                            throw new Exception("не найдено сертификата удовлетворяющему условию: " + Rek);


                }
                catch //(Exception ex)
                {
                    //GlobalObjectsManager.Logger.Error("При доступе к пользовательскому сертификату возникло исключение", ex);
                }
                finally
                {
                    store.Close();
                }
                return cert;
            }



            static public bool VerifyHash(string msg, string signature, string certrek)
            {
                try
                {
                    X509Certificate2 cert = GetCertByString(certrek);
                    return VerifyHash(msg, signature, ref cert);
                }
                catch (Exception e)
                {
                    string f = e.Message;
                }
                return false;
            }


            static public string HashAndSign(string msg, string certrek, int indx)
            {
                try
                {
                    X509Certificate2 cert = GetCertColl(certrek)[indx];
                    return HashAndSign(msg, ref cert);
                }
                catch
                {
                }
                return null;
            }

            static public string HashAndSign(string msg, string certrek)
            {
                try
                {
                    X509Certificate2 cert = GetCertByString(certrek);
                    return HashAndSign(msg, ref cert);
                }
                catch
                {
                }
                return null;
            }

            static public bool VerifyHash(string msg, string signature, ref X509Certificate2 cert)
            {
                RSACryptoServiceProvider rsa_public = cert.PublicKey.Key as RSACryptoServiceProvider;
                return rsa_public.VerifyHash((new SHA1Managed()).ComputeHash(Encoding.Default.GetBytes(msg)), CryptoConfig.MapNameToOID("SHA1"), Convert.FromBase64String(signature));
            }

            static public string HashAndSign(string msg, ref X509Certificate2 cert)
            {
                RSACryptoServiceProvider rsa_private = cert.PrivateKey as RSACryptoServiceProvider;
                HashAlgorithm SHA1 = HashAlgorithm.Create("SHA1");
                return Convert.ToBase64String(rsa_private.SignHash(SHA1.ComputeHash(Encoding.Default.GetBytes(msg)), CryptoConfig.MapNameToOID("SHA1")));
            }

            public static string MD5Base64(string data)
            {
                return Convert.ToBase64String((new MD5CryptoServiceProvider()).ComputeHash(Encoding.GetEncoding(1251).GetBytes(data)));
            }

            public static string MD5HashHex(string data)
            {
                byte[] result = (new MD5CryptoServiceProvider()).ComputeHash(Encoding.GetEncoding(1251).GetBytes(data));
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < result.Length; i++)
                {
                    sb.Append(result[i].ToString("X2"));
                }
                return sb.ToString();
            }
        }
    }

    namespace СуммаПрописью
    {
        using System;
        using System.Text;


        // Классы для преобразования чисел и денежных сумм в пропись.
        // Источник - http://morpher.ru/SummaPropisyu.aspx
        //
        // Данная реализация примечательна тем, что в ней почти нет 
        // операций сложения строк, всё делается через StringBuilder.
        // Работает в 1,5 раза быстрее своих аналогов на C#.
        //
        // Аналоги:
        // http://www.gotdotnet.ru/Downloads/Examples/772.aspx (C#, Уфимцев)
        // http://rsdn.ru/article/files/dotnet/RusNumber.xml (C#)
        // http://sources.ru/builder/faq/031.html (C++)
        // http://delphiworld.narod.ru/base/sum_written_out9.html (Pascal)
        //
        // Метод Пропись перегружен для сумм типа decimal и double.
        // Все неотрицательные суммы типа decimal с не более чем двумя цифрами
        // после запятой представимы в виде прописи.
        // Для сумм типа double максимальное представимое число - Число.MaxDouble.
        //

        /// <summary>
        /// Класс для записи денежных сумм прописью: "тысяча рублей 00 копеек".
        /// </summary>
        /// <example>
        /// Сумма.Пропись (100, Валюта.Рубли); // "сто рублей 00 копеек"
        /// Валюта.Рубли.Пропись (123.45); // "сто двадцать три рубля 45 копеек"
        /// </example>
        public static class Сумма
        {
            /// <summary>
            /// Записывает пропись суммы в заданной валюте в <paramref name="result"/> строчными буквами.
            /// </summary>
            public static StringBuilder Пропись(decimal сумма, Валюта валюта, StringBuilder result)
            {
                decimal целая = Math.Floor(сумма);
                uint дробная = (uint)((сумма - целая) * 100);

                Число.Пропись(целая, валюта.ОсновнаяЕдиница, result);
                return ДобавитьКопейки(дробная, валюта, result);
            }

            /// <summary>
            /// Записывает пропись суммы в заданной валюте в <paramref name="result"/> строчными буквами.
            /// </summary>
            public static StringBuilder Пропись(double сумма, Валюта валюта, StringBuilder result)
            {
                double целая = Math.Floor(сумма);
                uint дробная = (uint)((сумма - целая) * 100);

                Число.Пропись(целая, валюта.ОсновнаяЕдиница, result);
                return ДобавитьКопейки(дробная, валюта, result);
            }

            private static StringBuilder ДобавитьКопейки(uint дробная, Валюта валюта, StringBuilder result)
            {
                result.Append(' ');

                // Почему-то эта строчка выполняется быстрее, чем следующая за ней закомментированная.
                result.Append(дробная.ToString("00"));
                //result.AppendFormat ("{0:00}", дробная);

                result.Append(' ');
                result.Append(Число.Согласовать(валюта.ДробнаяЕдиница, дробная));

                return result;
            }

            /// <summary>
            /// Проверяет, подходит ли число для передачи методу 
            /// <see cref="Сумма.Пропись (decimal, Валюта)"/>.
            /// </summary>
            /// <remarks>
            /// Сумма должна быть неотрицательной и должна содержать 
            /// не более двух цифр после запятой.
            /// </remarks>
            /// <returns>
            /// Описание нарушенного ограничения или null.
            /// </returns>
            public static string ПроверитьСумму(decimal сумма)
            {
                if (сумма < 0) return "Сумма должна быть неотрицательной.";

                decimal целая = Math.Floor(сумма);
                decimal дробная = (сумма - целая) * 100;

                if (Math.Floor(дробная) != дробная)
                {
                    return "Сумма должна содержать не более двух цифр после запятой.";
                }

                return null;
            }

            /// <summary>
            /// Возвращает пропись заданной суммы строчными буквами.
            /// </summary>
            public static string Пропись(decimal n, Валюта валюта)
            {
                return Число.ApplyCaps(Пропись(n, валюта, new StringBuilder()), Заглавные.Нет);
            }

            /// <summary>
            /// Возвращает пропись заданной суммы.
            /// </summary>
            public static string Пропись(decimal n, Валюта валюта, Заглавные заглавные)
            {
                return Число.ApplyCaps(Пропись(n, валюта, new StringBuilder()), заглавные);
            }

            /// <summary>
            /// Возвращает пропись заданной суммы строчными буквами.
            /// </summary>
            public static string Пропись(double n, Валюта валюта)
            {
                return Число.ApplyCaps(Пропись(n, валюта, new StringBuilder()), Заглавные.Нет);
            }

            /// <summary>
            /// Возвращает пропись заданной суммы.
            /// </summary>
            public static string Пропись(double n, Валюта валюта, Заглавные заглавные)
            {
                return Число.ApplyCaps(Пропись(n, валюта, new StringBuilder()), заглавные);
            }
        }

        /// <summary>
        /// Класс для преобразования чисел в пропись на русском языке.
        /// </summary>
        /// <example>
        /// Число.Пропись (1, РодЧисло.Мужской); // "один"
        /// Число.Пропись (2, РодЧисло.Женский); // "две"
        /// Число.Пропись (21, РодЧисло.Средний); // "двадцать одно"
        /// </example>
        /// <example>
        /// Число.Пропись (5, new ЕдиницаИзмерения (
        ///  РодЧисло.Мужской, "метр", "метра", "метров"), sb); // "пять метров"
        /// </example>
        public static class Число
        {
            /// <summary>
            /// Получить пропись числа с согласованной единицей измерения.
            /// </summary>
            /// <param name="число"> Число должно быть целым, неотрицательным. </param>
            /// <param name="еи"></param>
            /// <param name="result"> Сюда записывается результат. </param>
            /// <returns> <paramref name="result"/> </returns>
            /// <exception cref="ArgumentException">
            /// Если число меньше нуля или не целое. 
            /// </exception>
            public static StringBuilder Пропись(decimal число, IЕдиницаИзмерения еи, StringBuilder result)
            {
                string error = ПроверитьЧисло(число);
                if (error != null) throw new ArgumentException(error, "число");

                // Целочисленные версии работают в разы быстрее, чем decimal.
                if (число <= uint.MaxValue)
                {
                    Пропись((uint)число, еи, result);
                }
                else if (число <= ulong.MaxValue)
                {
                    Пропись((ulong)число, еи, result);
                }
                else
                {
                    MyStringBuilder mySb = new MyStringBuilder(result);

                    decimal div1000 = Math.Floor(число / 1000);
                    ПрописьСтаршихКлассов(div1000, 0, mySb);
                    ПрописьКласса((uint)(число - div1000 * 1000), еи, mySb);
                }

                return result;
            }

            /// <summary>
            /// Получить пропись числа с согласованной единицей измерения.
            /// </summary>
            /// <param name="число"> 
            /// Число должно быть целым, неотрицательным, не большим <see cref="MaxDouble"/>. 
            /// </param>
            /// <param name="еи"></param>
            /// <param name="result"> Сюда записывается результат. </param>
            /// <exception cref="ArgumentException">
            /// Если число меньше нуля, не целое или больше <see cref="MaxDouble"/>. 
            /// </exception>
            /// <returns> <paramref name="result"/> </returns>
            /// <remarks>
            /// float по умолчанию преобразуется к double, поэтому нет перегрузки для float.
            /// В результате ошибок округления возможно расхождение цифр прописи и
            /// строки, выдаваемой double.ToString ("R"), начиная с 17 значащей цифры.
            /// </remarks>
            public static StringBuilder Пропись(double число, IЕдиницаИзмерения еи, StringBuilder result)
            {
                string error = ПроверитьЧисло(число);
                if (error != null) throw new ArgumentException(error, "число");

                if (число <= uint.MaxValue)
                {
                    Пропись((uint)число, еи, result);
                }
                else if (число <= ulong.MaxValue)
                {
                    // Пропись с ulong выполняется в среднем в 2 раза быстрее.
                    Пропись((ulong)число, еи, result);
                }
                else
                {
                    MyStringBuilder mySb = new MyStringBuilder(result);

                    double div1000 = Math.Floor(число / 1000);
                    ПрописьСтаршихКлассов(div1000, 0, mySb);
                    ПрописьКласса((uint)(число - div1000 * 1000), еи, mySb);
                }

                return result;
            }

            /// <summary>
            /// Получить пропись числа с согласованной единицей измерения.
            /// </summary>
            /// <returns> <paramref name="result"/> </returns>
            public static StringBuilder Пропись(ulong число, IЕдиницаИзмерения еи, StringBuilder result)
            {
                if (число <= uint.MaxValue)
                {
                    Пропись((uint)число, еи, result);
                }
                else
                {
                    MyStringBuilder mySb = new MyStringBuilder(result);

                    ulong div1000 = число / 1000;
                    ПрописьСтаршихКлассов(div1000, 0, mySb);
                    ПрописьКласса((uint)(число - div1000 * 1000), еи, mySb);
                }

                return result;
            }

            /// <summary>
            /// Получить пропись числа с согласованной единицей измерения.
            /// </summary>
            /// <returns> <paramref name="result"/> </returns>
            public static StringBuilder Пропись(uint число, IЕдиницаИзмерения еи, StringBuilder result)
            {
                MyStringBuilder mySb = new MyStringBuilder(result);

                if (число == 0)
                {
                    mySb.Append("ноль");
                    mySb.Append(еи.РодМнож);
                }
                else
                {
                    uint div1000 = число / 1000;
                    ПрописьСтаршихКлассов(div1000, 0, mySb);
                    ПрописьКласса(число - div1000 * 1000, еи, mySb);
                }

                return result;
            }

            /// <summary>
            /// Записывает в <paramref name="sb"/> пропись числа, начиная с самого 
            /// старшего класса до класса с номером <paramref name="номерКласса"/>.
            /// </summary>
            /// <param name="sb"></param>
            /// <param name="число"></param>
            /// <param name="номерКласса">0 = класс тысяч, 1 = миллионов и т.д.</param>
            /// <remarks>
            /// В методе применена рекурсия, чтобы обеспечить запись в StringBuilder 
            /// в нужном порядке - от старших классов к младшим.
            /// </remarks>
            static void ПрописьСтаршихКлассов(decimal число, int номерКласса, MyStringBuilder sb)
            {
                if (число == 0) return; // конец рекурсии

                // Записать в StringBuilder пропись старших классов.
                decimal div1000 = Math.Floor(число / 1000);
                ПрописьСтаршихКлассов(div1000, номерКласса + 1, sb);

                uint числоДо999 = (uint)(число - div1000 * 1000);
                if (числоДо999 == 0) return;

                ПрописьКласса(числоДо999, Классы[номерКласса], sb);
            }

            static void ПрописьСтаршихКлассов(double число, int номерКласса, MyStringBuilder sb)
            {
                if (число == 0) return; // конец рекурсии

                // Записать в StringBuilder пропись старших классов.
                double div1000 = Math.Floor(число / 1000);
                ПрописьСтаршихКлассов(div1000, номерКласса + 1, sb);

                uint числоДо999 = (uint)(число - div1000 * 1000);
                if (числоДо999 == 0) return;

                ПрописьКласса(числоДо999, Классы[номерКласса], sb);
            }

            static void ПрописьСтаршихКлассов(ulong число, int номерКласса, MyStringBuilder sb)
            {
                if (число == 0) return; // конец рекурсии

                // Записать в StringBuilder пропись старших классов.
                ulong div1000 = число / 1000;
                ПрописьСтаршихКлассов(div1000, номерКласса + 1, sb);

                uint числоДо999 = (uint)(число - div1000 * 1000);
                if (числоДо999 == 0) return;

                ПрописьКласса(числоДо999, Классы[номерКласса], sb);
            }

            static void ПрописьСтаршихКлассов(uint число, int номерКласса, MyStringBuilder sb)
            {
                if (число == 0) return; // конец рекурсии

                // Записать в StringBuilder пропись старших классов.
                uint div1000 = число / 1000;
                ПрописьСтаршихКлассов(div1000, номерКласса + 1, sb);

                uint числоДо999 = число - div1000 * 1000;
                if (числоДо999 == 0) return;

                ПрописьКласса(числоДо999, Классы[номерКласса], sb);
            }

            #region ПрописьКласса

            /// <summary>
            /// Формирует запись класса с названием, например,
            /// "125 тысяч", "15 рублей".
            /// Для 0 записывает только единицу измерения в род.мн.
            /// </summary>
            private static void ПрописьКласса(uint числоДо999, IЕдиницаИзмерения класс, MyStringBuilder sb)
            {
                uint числоЕдиниц = числоДо999 % 10;
                uint числоДесятков = (числоДо999 / 10) % 10;
                uint числоСотен = (числоДо999 / 100) % 10;

                sb.Append(Сотни[числоСотен]);

                if ((числоДо999 % 100) != 0)
                {
                    Десятки[числоДесятков].Пропись(sb, числоЕдиниц, класс.РодЧисло);
                }

                // Добавить название класса в нужной форме.
                sb.Append(Согласовать(класс, числоДо999));
            }

            #endregion

            #region ПроверитьЧисло

            /// <summary>
            /// Проверяет, подходит ли число для передачи методу 
            /// <see cref="Пропись(decimal,IЕдиницаИзмерения,StringBuilder)"/>.
            /// </summary>
            /// <returns>
            /// Описание нарушенного ограничения или null.
            /// </returns>
            public static string ПроверитьЧисло(decimal число)
            {
                if (число < 0)
                    return "Число должно быть больше или равно нулю.";

                if (число != decimal.Floor(число))
                    return "Число не должно содержать дробной части.";

                return null;
            }

            /// <summary>
            /// Проверяет, подходит ли число для передачи методу 
            /// <see cref="Пропись(double,IЕдиницаИзмерения,StringBuilder)"/>.
            /// </summary>
            /// <returns>
            /// Описание нарушенного ограничения или null.
            /// </returns>
            public static string ПроверитьЧисло(double число)
            {
                if (число < 0)
                    return "Число должно быть больше или равно нулю.";

                if (число != Math.Floor(число))
                    return "Число не должно содержать дробной части.";

                if (число > MaxDouble)
                {
                    return "Число должно быть не больше " + MaxDouble + ".";
                }

                return null;
            }

            #endregion

            #region Согласовать

            /// <summary>
            /// Согласовать название единицы измерения с числом.
            /// Например, согласование единицы (рубль, рубля, рублей) 
            /// с числом 23 даёт "рубля", а с числом 25 - "рублей".
            /// </summary>
            public static string Согласовать(IЕдиницаИзмерения единицаИзмерения, uint число)
            {
                uint числоЕдиниц = число % 10;
                uint числоДесятков = (число / 10) % 10;

                if (числоДесятков == 1) return единицаИзмерения.РодМнож;
                switch (числоЕдиниц)
                {
                    case 1: return единицаИзмерения.ИменЕдин;
                    case 2:
                    case 3:
                    case 4: return единицаИзмерения.РодЕдин;
                    default: return единицаИзмерения.РодМнож;
                }
            }

            #endregion

            #region Единицы

            static string ПрописьЦифры(uint цифра, РодЧисло род)
            {
                return Цифры[цифра].Пропись(род);
            }

            abstract class Цифра
            {
                public abstract string Пропись(РодЧисло род);
            }

            class ЦифраИзменяющаясяПоРодам : Цифра, IИзменяетсяПоРодам
            {
                public ЦифраИзменяющаясяПоРодам(
                    string мужской,
                    string женский,
                    string средний,
                    string множественное)
                {
                    this.мужской = мужской;
                    this.женский = женский;
                    this.средний = средний;
                    this.множественное = множественное;
                }

                public ЦифраИзменяющаясяПоРодам(
                    string единственное,
                    string множественное)

                    : this(единственное, единственное, единственное, множественное)
                {
                }

                private readonly string мужской;
                private readonly string женский;
                private readonly string средний;
                private readonly string множественное;

                #region IИзменяетсяПоРодам Members

                public string Мужской { get { return this.мужской; } }
                public string Женский { get { return this.женский; } }
                public string Средний { get { return this.средний; } }
                public string Множественное { get { return this.множественное; } }

                #endregion

                public override string Пропись(РодЧисло род)
                {
                    return род.ПолучитьФорму(this);
                }
            }

            class ЦифраНеизменяющаясяПоРодам : Цифра
            {
                public ЦифраНеизменяющаясяПоРодам(string пропись)
                {
                    this.пропись = пропись;
                }

                private readonly string пропись;

                public override string Пропись(РодЧисло род)
                {
                    return this.пропись;
                }
            }

            private static readonly Цифра[] Цифры = new Цифра[]
        {
            null,
            new ЦифраИзменяющаясяПоРодам ("один", "одна", "одно", "одни"),
            new ЦифраИзменяющаясяПоРодам ("два", "две", "два", "двое"),
            new ЦифраИзменяющаясяПоРодам ("три", "трое"),
            new ЦифраИзменяющаясяПоРодам ("четыре", "четверо"),
            new ЦифраНеизменяющаясяПоРодам ("пять"),
            new ЦифраНеизменяющаясяПоРодам ("шесть"),
            new ЦифраНеизменяющаясяПоРодам ("семь"),
            new ЦифраНеизменяющаясяПоРодам ("восемь"),
            new ЦифраНеизменяющаясяПоРодам ("девять"),
        };

            #endregion
            #region Десятки

            static readonly Десяток[] Десятки = new Десяток[]
        {
            new ПервыйДесяток (),
            new ВторойДесяток (),
            new ОбычныйДесяток ("двадцать"),
            new ОбычныйДесяток ("тридцать"),
            new ОбычныйДесяток ("сорок"),
            new ОбычныйДесяток ("пятьдесят"),
            new ОбычныйДесяток ("шестьдесят"),
            new ОбычныйДесяток ("семьдесят"),
            new ОбычныйДесяток ("восемьдесят"),
            new ОбычныйДесяток ("девяносто")
        };

            abstract class Десяток
            {
                public abstract void Пропись(MyStringBuilder sb, uint числоЕдиниц, РодЧисло род);
            }

            class ПервыйДесяток : Десяток
            {
                public override void Пропись(MyStringBuilder sb, uint числоЕдиниц, РодЧисло род)
                {
                    sb.Append(ПрописьЦифры(числоЕдиниц, род));
                }
            }

            class ВторойДесяток : Десяток
            {
                static readonly string[] ПрописьНаДцать = new string[]
            {
                "десять",
                "одиннадцать",
                "двенадцать",
                "тринадцать",
                "четырнадцать",
                "пятнадцать",
                "шестнадцать",
                "семнадцать",
                "восемнадцать",
                "девятнадцать"
            };

                public override void Пропись(MyStringBuilder sb, uint числоЕдиниц, РодЧисло род)
                {
                    sb.Append(ПрописьНаДцать[числоЕдиниц]);
                }
            }

            class ОбычныйДесяток : Десяток
            {
                public ОбычныйДесяток(string названиеДесятка)
                {
                    this.названиеДесятка = названиеДесятка;
                }

                private readonly string названиеДесятка;

                public override void Пропись(MyStringBuilder sb, uint числоЕдиниц, РодЧисло род)
                {
                    sb.Append(this.названиеДесятка);

                    if (числоЕдиниц == 0)
                    {
                        // После "двадцать", "тридцать" и т.д. не пишут "ноль" (единиц)
                    }
                    else
                    {
                        sb.Append(ПрописьЦифры(числоЕдиниц, род));
                    }
                }
            }

            #endregion
            #region Сотни

            static readonly string[] Сотни = new string[]
        {
            null,
            "сто",
            "двести",
            "триста",
            "четыреста",
            "пятьсот",
            "шестьсот",
            "семьсот",
            "восемьсот",
            "девятьсот"
        };

            #endregion
            #region Классы

            #region КлассТысяч

            class КлассТысяч : IЕдиницаИзмерения
            {
                public string ИменЕдин { get { return "тысяча"; } }
                public string РодЕдин { get { return "тысячи"; } }
                public string РодМнож { get { return "тысяч"; } }
                public РодЧисло РодЧисло { get { return РодЧисло.Женский; } }
            }

            #endregion
            #region Класс

            class Класс : IЕдиницаИзмерения
            {
                readonly string начальнаяФорма;

                public Класс(string начальнаяФорма)
                {
                    this.начальнаяФорма = начальнаяФорма;
                }

                public string ИменЕдин { get { return this.начальнаяФорма; } }
                public string РодЕдин { get { return this.начальнаяФорма + "а"; } }
                public string РодМнож { get { return this.начальнаяФорма + "ов"; } }
                public РодЧисло РодЧисло { get { return РодЧисло.Мужской; } }
            }

            #endregion

            /// <summary>
            /// Класс - группа из 3 цифр.  Есть классы единиц, тысяч, миллионов и т.д.
            /// </summary>
            static readonly IЕдиницаИзмерения[] Классы = new IЕдиницаИзмерения[]
        {
            new КлассТысяч (),
            new Класс ("миллион"),
            new Класс ("миллиард"),
            new Класс ("триллион"),
            new Класс ("квадриллион"),
            new Класс ("квинтиллион"),
            new Класс ("секстиллион"),
            new Класс ("септиллион"),
            new Класс ("октиллион"),
 
            // Это количество классов покрывает весь диапазон типа decimal.
        };

            #endregion

            #region MaxDouble

            /// <summary>
            /// Максимальное число типа double, представимое в виде прописи.
            /// </summary>
            /// <remarks>
            /// Рассчитывается исходя из количества определённых классов.
            /// Если добавить ещё классы, оно будет автоматически увеличено.
            /// </remarks>
            public static double MaxDouble
            {
                get
                {
                    if (maxDouble == 0)
                    {
                        maxDouble = CalcMaxDouble();
                    }

                    return maxDouble;
                }
            }

            private static double maxDouble = 0;

            static double CalcMaxDouble()
            {
                double max = Math.Pow(1000, Классы.Length + 1);

                double d = 1;

                while (max - d == max)
                {
                    d *= 2;
                }

                return max - d;
            }

            #endregion

            #region Вспомогательные классы

            #region Форма

            #endregion
            #region MyStringBuilder

            /// <summary>
            /// Вспомогательный класс, аналогичный <see cref="StringBuilder"/>.
            /// Между вызовами <see cref="MyStringBuilder.Append"/> вставляет пробелы.
            /// </summary>
            class MyStringBuilder
            {
                public MyStringBuilder(StringBuilder sb)
                {
                    this.sb = sb;
                }

                readonly StringBuilder sb;
                bool insertSpace = false;

                /// <summary>
                /// Добавляет слово <paramref name="s"/>,
                /// вставляя перед ним пробел, если нужно.
                /// </summary>
                public void Append(string s)
                {
                    if (string.IsNullOrEmpty(s)) return;

                    if (this.insertSpace)
                    {
                        this.sb.Append(' ');
                    }
                    else
                    {
                        this.insertSpace = true;
                    }

                    this.sb.Append(s);
                }

                public override string ToString()
                {
                    return sb.ToString();
                }
            }

            #endregion

            #endregion

            #region Перегрузки метода Пропись, возвращающие string

            /// <summary>
            /// Возвращает пропись числа строчными буквами.
            /// </summary>
            public static string Пропись(decimal число, IЕдиницаИзмерения еи)
            {
                return Пропись(число, еи, Заглавные.Нет);
            }

            /// <summary>
            /// Возвращает пропись числа.
            /// </summary>
            public static string Пропись(decimal число, IЕдиницаИзмерения еи, Заглавные заглавные)
            {
                return ApplyCaps(Пропись(число, еи, new StringBuilder()), заглавные);
            }

            /// <summary>
            /// Возвращает пропись числа строчными буквами.
            /// </summary>
            public static string Пропись(double число, IЕдиницаИзмерения еи)
            {
                return Пропись(число, еи, Заглавные.Нет);
            }

            /// <summary>
            /// Возвращает пропись числа.
            /// </summary>
            public static string Пропись(double число, IЕдиницаИзмерения еи, Заглавные заглавные)
            {
                return ApplyCaps(Пропись(число, еи, new StringBuilder()), заглавные);
            }

            /// <summary>
            /// Возвращает пропись числа строчными буквами.
            /// </summary>
            public static string Пропись(ulong число, IЕдиницаИзмерения еи)
            {
                return Пропись(число, еи, Заглавные.Нет);
            }

            /// <summary>
            /// Возвращает пропись числа.
            /// </summary>
            public static string Пропись(ulong число, IЕдиницаИзмерения еи, Заглавные заглавные)
            {
                return ApplyCaps(Пропись(число, еи, new StringBuilder()), заглавные);
            }

            /// <summary>
            /// Возвращает пропись числа строчными буквами.
            /// </summary>
            public static string Пропись(uint число, IЕдиницаИзмерения еи)
            {
                return Пропись(число, еи, Заглавные.Нет);
            }

            /// <summary>
            /// Возвращает пропись числа.
            /// </summary>
            public static string Пропись(uint число, IЕдиницаИзмерения еи, Заглавные заглавные)
            {
                return ApplyCaps(Пропись(число, еи, new StringBuilder()), заглавные);
            }

            internal static string ApplyCaps(StringBuilder sb, Заглавные заглавные)
            {
                заглавные.Применить(sb);
                return sb.ToString();
            }

            #endregion
        }

        /// <summary>
        /// Стратегия расстановки заглавных букв.
        /// </summary>
        public abstract class Заглавные
        {
            /// <summary>
            /// Применить стратегию к <paramref name="sb"/>.
            /// </summary>
            public abstract void Применить(StringBuilder sb);

            class _ВСЕ : Заглавные
            {
                public override void Применить(StringBuilder sb)
                {
                    for (int i = 0; i < sb.Length; ++i)
                    {
                        sb[i] = char.ToUpperInvariant(sb[i]);
                    }
                }
            }

            class _Нет : Заглавные
            {
                public override void Применить(StringBuilder sb)
                {
                }
            }

            class _Первая : Заглавные
            {
                public override void Применить(StringBuilder sb)
                {
                    sb[0] = char.ToUpperInvariant(sb[0]);
                }
            }

            public static readonly Заглавные ВСЕ = new _ВСЕ();
            public static readonly Заглавные Нет = new _Нет();
            public static readonly Заглавные Первая = new _Первая();
        }

        /// <summary>
        /// Описывает тип валюты как совокупность двух единиц измерения - основной и дробной.
        /// Содержит несколько предопределённых валют - рубли, доллары, евро.
        /// </summary>
        /// <remarks>
        /// Предполагается, что основная единица равна 100 дробным. 
        /// </remarks>
        public class Валюта
        {
            /// <summary> </summary>
            public Валюта(IЕдиницаИзмерения основная, IЕдиницаИзмерения дробная)
            {
                this.основная = основная;
                this.дробная = дробная;
            }

            readonly IЕдиницаИзмерения основная;
            readonly IЕдиницаИзмерения дробная;

            /// <summary>
            /// Основная единица измерения валюты - рубли, доллары, евро и т.д.
            /// </summary>
            public IЕдиницаИзмерения ОсновнаяЕдиница
            {
                get { return this.основная; }
            }

            /// <summary>
            /// Дробная единица измерения валюты - копейки, центы, евроценты и т.д.
            /// </summary>
            public IЕдиницаИзмерения ДробнаяЕдиница
            {
                get { return this.дробная; }
            }

            public static readonly Валюта Рубли = new Валюта(
                new ЕдиницаИзмерения(РодЧисло.Мужской, "рубль", "рубля", "рублей"),
                new ЕдиницаИзмерения(РодЧисло.Женский, "копейка", "копейки", "копеек"));

            public static readonly Валюта Доллары = new Валюта(
                new ЕдиницаИзмерения(РодЧисло.Мужской, "доллар США", "доллара США", "долларов США"),
                new ЕдиницаИзмерения(РодЧисло.Мужской, "цент", "цента", "центов"));

            public static readonly Валюта Евро = new Валюта(
                new ЕдиницаИзмерения(РодЧисло.Мужской, "евро", "евро", "евро"),
                new ЕдиницаИзмерения(РодЧисло.Мужской, "цент", "цента", "центов"));

            /// <summary>
            /// Возвращает пропись суммы строчными буквами.
            /// </summary>
            public string Пропись(decimal сумма)
            {
                return Сумма.Пропись(сумма, this);
            }

            /// <summary>
            /// Возвращает пропись суммы строчными буквами.
            /// </summary>
            public string Пропись(double сумма)
            {
                return Сумма.Пропись(сумма, this);
            }

            /// <summary>
            /// Возвращает пропись суммы.
            /// </summary>
            public string Пропись(decimal сумма, Заглавные заглавные)
            {
                return Сумма.Пропись(сумма, this, заглавные);
            }

            /// <summary>
            /// Возвращает пропись суммы.
            /// </summary>
            public string Пропись(double сумма, Заглавные заглавные)
            {
                return Сумма.Пропись(сумма, this, заглавные);
            }
        }

        /// <summary>
        /// Класс, хранящий падежные формы единицы измерения в явном виде.
        /// </summary>
        public class ЕдиницаИзмерения : IЕдиницаИзмерения
        {
            /// <summary> </summary>
            public ЕдиницаИзмерения(РодЧисло родЧисло, string именЕдин, string родЕдин, string родМнож)
            {
                this.родЧисло = родЧисло;
                this.именЕдин = именЕдин;
                this.родЕдин = родЕдин;
                this.родМнож = родМнож;
            }

            readonly РодЧисло родЧисло;
            readonly string именЕдин;
            readonly string родЕдин;
            readonly string родМнож;

            #region IЕдиницаИзмерения Members

            string IЕдиницаИзмерения.ИменЕдин
            {
                get { return this.именЕдин; }
            }

            string IЕдиницаИзмерения.РодЕдин
            {
                get { return this.родЕдин; }
            }

            string IЕдиницаИзмерения.РодМнож
            {
                get { return this.родМнож; }
            }

            РодЧисло IЕдиницаИзмерения.РодЧисло
            {
                get { return this.родЧисло; }
            }

            #endregion
        }

        #region РодЧисло

        /// <summary>
        /// Указывает род и число.
        /// Может передаваться в качестве параметра "единица измерения" метода 
        /// <see cref="Число.Пропись(decimal,IЕдиницаИзмерения,StringBuilder)"/>.
        /// Управляет родом и числом числительных один и два.
        /// </summary>
        /// <example>
        /// Число.Пропись (2, РодЧисло.Мужской); // "два"
        /// Число.Пропись (2, РодЧисло.Женский); // "две"
        /// Число.Пропись (21, РодЧисло.Средний); // "двадцать одно"
        /// </example>
        public abstract class РодЧисло : IЕдиницаИзмерения
        {
            internal abstract string ПолучитьФорму(IИзменяетсяПоРодам слово);

            #region Рода

            class _Мужской : РодЧисло
            {
                internal override string ПолучитьФорму(IИзменяетсяПоРодам слово)
                {
                    return слово.Мужской;
                }
            }

            class _Женский : РодЧисло
            {
                internal override string ПолучитьФорму(IИзменяетсяПоРодам слово)
                {
                    return слово.Женский;
                }
            }

            class _Средний : РодЧисло
            {
                internal override string ПолучитьФорму(IИзменяетсяПоРодам слово)
                {
                    return слово.Средний;
                }
            }

            class _Множественное : РодЧисло
            {
                internal override string ПолучитьФорму(IИзменяетсяПоРодам слово)
                {
                    return слово.Множественное;
                }
            }

            public static readonly РодЧисло Мужской = new _Мужской();
            public static readonly РодЧисло Женский = new _Женский();
            public static readonly РодЧисло Средний = new _Средний();
            public static readonly РодЧисло Множественное = new _Множественное();

            #endregion

            #region IЕдиницаИзмерения Members

            РодЧисло IЕдиницаИзмерения.РодЧисло
            {
                get { return this; }
            }

            string IЕдиницаИзмерения.ИменЕдин
            {
                get { return null; }
            }

            string IЕдиницаИзмерения.РодЕдин
            {
                get { return null; }
            }

            string IЕдиницаИзмерения.РодМнож
            {
                get { return null; }
            }

            #endregion
        }

        #region IИзменяетсяПоРодам

        internal interface IИзменяетсяПоРодам
        {
            string Мужской { get; }
            string Женский { get; }
            string Средний { get; }
            string Множественное { get; }
        }

        #endregion

        #endregion

        #region ЕдиницаИзмерения

        /// <summary>
        /// Представляет единицу измерения (например, метр, рубль)
        /// и содержит всю необходимую информацию для согласования
        /// этой единицы с числом, а именно - три падежно-числовых формы
        /// и грамматический род / число.
        /// </summary>
        public interface IЕдиницаИзмерения
        {
            /// <summary>
            /// Форма именительного падежа единственного числа.
            /// Согласуется с числительным "один":
            /// одна тысяча, один миллион, один рубль, одни сутки и т.д.
            /// </summary>
            string ИменЕдин { get; }

            /// <summary>
            /// Форма родительного падежа единственного числа.
            /// Согласуется с числительными "один, два, три, четыре":
            /// две тысячи, два миллиона, два рубля, двое суток и т.д.
            /// </summary>
            string РодЕдин { get; }

            /// <summary>
            /// Форма родительного падежа множественного числа.
            /// Согласуется с числительным "ноль, пять, шесть, семь" и др:
            /// пять тысяч, пять миллионов, пять рублей, пять суток и т.д.
            /// </summary>
            string РодМнож { get; }

            /// <summary>
            /// Род и число единицы измерения.
            /// </summary>
            РодЧисло РодЧисло { get; }
        }

        #endregion
    }

}
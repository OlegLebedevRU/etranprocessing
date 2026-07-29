//using EtranLib.Collection;
//using EtranLib.Crypto;
//using EtranLib.Net;
//using EtranLib.ScheduleTimer;

namespace EtranLib
{


    namespace Math
    {
        using System;
        class TrendLine
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


            public object Execute(string sp_name, CommandType ct, DataReadType rt, params object[] parameterValues)
            {

                command.CommandText = sp_name;
                command.CommandType = ct;

                SqlCommandBuilder.DeriveParameters(command);
                int index = 0;
                foreach (SqlParameter parameter in command.Parameters)
                {
                    if (parameter.Direction == ParameterDirection.Input || parameter.Direction == ParameterDirection.InputOutput)
                    {
                        parameter.Value = parameterValues[index];
                        index++;
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
                            return output;
                        }

                    case DataReadType.Hashtable:
                        {
                            Hashtable output = new Hashtable();
                            SqlDataReader reader = command.ExecuteReader();
                            if(reader.Read())
                                for (int i = 0; i < reader.FieldCount; i++)
                                    output.Add(i, reader[i]);
                            return output;
                        }
                    case DataReadType.ArrayList:
                        {
                            ArrayList output = new ArrayList();
                            SqlDataReader reader = command.ExecuteReader();
                            while (reader.Read())
                                    output.Add(reader[0]);
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
        class Collection
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
        class MathEtran
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
        using System.IO;
        using System.Text;
        using System.Security.Cryptography.X509Certificates;

        class Net
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

        class Crypto
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

        }
    }
}
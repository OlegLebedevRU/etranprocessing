namespace Helpers
{


    namespace Net
    {
        using System.Net;
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
            static public string WebRequest(string url, int timeout, string _data, X509Certificate2 cert, NetworkCredential cred)
            {
                HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
                if (cert != null)
                    wrq.ClientCertificates.Add(cert);

                wrq.Timeout = timeout;

                if (cred != null)
                    wrq.Credentials = cred;

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
                StreamReader reader = new StreamReader(strm);//, Encoding.UTF8.GetEncoding(1251));
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
        }
    }


    namespace Collection
    {
        using System.Collections.Specialized;
        using System;

        public class Collection
        {
            public static NameValueCollection GetNameValueCollection(string data, string delimiter, string split)
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

            public static NameValueCollection GetNameValueCollection(string Params)
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
}
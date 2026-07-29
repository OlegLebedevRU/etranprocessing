using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime.Remoting.Contexts;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web;

namespace ConsoleApplication2
{
    internal class Program
    {
        //R: {"status":"success","balance":90.00,"invoice_id":"2000334103046","payment_id":"502304449680110001"}
        private static void Main(string[] args)
        {
            Debug.WriteLine("START ....");
            //StartButtonClick();
            try
            {
                var t = new YmRequest();

                var r = t.ProcessPayment();

                Debug.WriteLine("R: " + r);

            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
            }

            //Debug.WriteLine("AAAAAAAAAAAAAAAAAAAAAAAAAAA");

            //btnRead_Click();

            //Thread.Sleep(7000);
            Debug.WriteLine("I'm DONE.");

            //Debug.WriteLine("Text:." + Text.Length);
            //Debug.WriteLine("Content:." + Content);

        }

        private static async void btnRead_Click()
        {
            Context currentContext1 = Thread.CurrentContext;
            int result = await DoSomeWorkAsync();
            Context currentContext2 = Thread.CurrentContext;
            bool areEqual = ReferenceEquals(currentContext1, currentContext2);
            Debug.WriteLine("areEqual: " + areEqual);
        }

        private static async Task<int> DoSomeWorkAsync()
        {
            await Task.Delay(100).ConfigureAwait(true);
            return 1;
        }
        private static string Text;
        private static string Content;
        private static async void StartButtonClick()
        {
            // Убираем возможность повторного нажатия на кнопку
            //StartButton.IsEnabled = false;

            // Вызываем новую задачу, на этом выполнение функции закончится
            // а остаток функции установится в продолжение
            var task = await new WebClient().DownloadStringTaskAsync("http://habrahabr.ru/");
            Content = "Загрузка страницы завершена, начинается обработка";

            // В продолжении можно также запускать асинхронные операции со своим продолжением
            var result =  Task<string>.Factory.StartNew(() =>
            {
                //Thread.Sleep(5000); // Имитация длительной обработки...

                int y = 10;
                while (y-- > 0)
                {
                    Debug.WriteLine("InProcess.... LENOFText " + (string.IsNullOrEmpty(Text) ? 0 : Text.Length));
                    Thread.Sleep(500);
                }
                return "Результат обработки";
            });

            Thread.Sleep(2000);
            Text = task;

            //Task<string> task = new WebClient().DownloadStringTaskAsync("http://microsoft.com/");
            //task.Wait(); // Здесь мы ждем завершения задачи, что блокирует поток
            // Продолжение второй асинхронной операции
            Content = result.Result;
        }
    }
    public static class UriExtensions
    {
        public static Uri Append(this Uri uri, params string[] paths)
        {
            return new Uri(paths.Aggregate(uri.AbsoluteUri, (current, path) => string.Format("{0}/{1}", current.TrimEnd('/'), path.TrimStart('/'))));
        }
    }
    public class YmRequest
    {
        private const string UriBase = "https://money.yandex.ru/";
        private string _clientID = "DA6F9E193D5B86FC556C02F9C30EE7ECB734F53A8C335F0695062611A6362D63";
        private string _instanceName = "Forpay";
        private string _redirectUri = "https://forpay.net/Dispatcher/";
        private string _clientSecret = "846CD0E5F2766012254D24C907EF5D7178879C8DF483002999A456392BCCCF0964B5E0638281209AC9CF7BED3FC988882496CEF32A33E78A7749528692EF4A70";

        private string code =
            "1F85F756619F56348CCC58F9F2DC6163D3C02A5480D382D48B8B163D86A46A5B198F126F5030F14C65690352B41FD132AFF7B1844B27D1F534A5F156830805B753895F3F8EC81CB0EB47B992F2F6773783ADC0D5C52077808F4550561368EC6F9B751DAA26657BD2E24AB0DE85257C08B2014B8FF0F032B501C986EDE24ED015";

        //R: {"access_token":"410013762881958.B303ECB6892442EFBA75DBE5A0CF3A3FFDD181D0DB0E5BEF240D68B183FCEAD5E7447CAFCCE53332D0A46DF66D5F6BEC444AFC4DE311BB31FF3165654D3BC1861AB6BE72EFA705AB5A87FBA805555C11DB8DF7DC41AC504C14934046563EA250806AC357E881EF5CA417F04DD5609191C6C6A6AF856AA46449994AB53A40369C"}
        //R: {"access_token":"410013762881958.B319766D3D1DD63B8F1055FF5E5F11646820D22E8741B49B7545D516AD8D8B24B0F81A220656E59CA1EBB744D7E9DF2B181C7CA871A029DAEF1A62AF97E2F9B559BBF3E615BB50368D6A2FC076C0A688D78B3B1E968DC29C201746FE0C327B1FAD1101153AD9F84B2A9D4BD9114DB02EB7BEA80DFE68D848B0662F1E185C72ED"}
        //R: {"access_token":"410013762881958.6FC4222E2767183A6FFE633C66D32861615C7118C70564F22595FEB7A705F9928D4E0F5CF986D7E5C5EC9E13CE8CC9FCC45D6588ADA8908F2191ED9BC7FC6173FA0FD1E5727ACCB2BAFA052FFAA73959A67AF42A6E1E7B5D27511A43D6C51086168A82D34A370A8D2F5FE192BBF2BD76762DE45C3EA2C096565F9222CBB3659D"}
        private static string _accessToken = "Bearer 410013762881958.6FC4222E2767183A6FFE633C66D32861615C7118C70564F22595FEB7A705F9928D4E0F5CF986D7E5C5EC9E13CE8CC9FCC45D6588ADA8908F2191ED9BC7FC6173FA0FD1E5727ACCB2BAFA052FFAA73959A67AF42A6E1E7B5D27511A43D6C51086168A82D34A370A8D2F5FE192BBF2BD76762DE45C3EA2C096565F9222CBB3659D";


        #region API
        public string AccountInfo()
        {
            addToken = true;
            var r = Post("/api/account-info", "");
            addToken = false;

            return r;
        }

        public string ProcessPayment()
        {
            addToken = true;
            var r = Post("/api/process-payment", "request_id=323039393830373139345f366336346135333565656638333830616339613933316666643633643234346162623161343236635f333436373930313636");
            addToken = false;
            return r;
        }
        public string RequestPayment()
        {
            addToken = true;
            var r = Post("/api/request-payment", "pattern_id=phone-topup&phone-number=79162376067&amount=10.00");
            addToken = false;
            return r;
        }

        public string TokenRevoke()
        {
            addToken = true;
            var r = Post("/api/revoke", "");
            addToken = false;
            return r;
        }

        #endregion

        public string GetToken()
        {
            var portString = "client_id=" + _clientID + "&grant_type=authorization_code";
            portString += "&code=" + code;
            portString += "&client_secret=" + _clientSecret;
            portString += "&instance_name=" + _instanceName;
            portString += "&redirect_uri=" + HttpUtility.UrlEncode(_redirectUri);
            var r = Post("/oauth/token", portString);
            return r;
        }

        public string Authorize()
        {
            var portString = "client_id=" + _clientID + "&response_type=code";
            portString += "&client_secret=" + _clientSecret;
            portString += "&instance_name=" + _instanceName;
            portString += "&redirect_uri=" + HttpUtility.UrlEncode(_redirectUri);
            //portString += "&scope=" + HttpUtility.UrlEncode("account-info operation-history request-payment process-payment");
            portString += "&scope=" + HttpUtility.UrlEncode("account-info payment-shop");
            var r = Post("/oauth/authorize", portString);
            return r;
        }

        private static bool addToken;
        static public string WebRequest(string url, int timeout, string _data)
        {
            HttpWebRequest wrq = HttpWebRequest.Create(url) as HttpWebRequest;
            wrq.UserAgent = "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.874.121 Safari/535.2";
            if (addToken)
                wrq.Headers.Add("Authorization", _accessToken);

            wrq.Timeout = timeout;
            wrq.AllowAutoRedirect = false;
            if (_data == null)
                wrq.Method = "GET";
            else
            {
                wrq.Method = "POST";
                wrq.ContentType = "application/x-www-form-urlencoded";
                byte[] data = Encoding.UTF8.GetBytes(_data);
                wrq.ContentLength = data.Length;
                Stream newStream = wrq.GetRequestStream();
                newStream.Write(data, 0, data.Length);
                newStream.Close();
            }
            HttpWebResponse hwr = wrq.GetResponse() as HttpWebResponse;

            // check the header for a Location value
            var location = hwr.Headers["Location"];
            if (location != null)
            {
                Debug.WriteLine("loction: " + location);
                //return WebRequest(location, 30000, null);
                //return location;
            }
            //else
            //{
            //    // anything non null means we got a redirect
            //    return string.Empty;
            //}
            Stream strm = hwr.GetResponseStream();
            StreamReader reader = new StreamReader(strm, System.Text.Encoding.UTF8);
            return reader.ReadToEnd();
        }


        private string Post(string relativePath, string paramsPost)
        {
            Debug.WriteLine("paramsPost: " + paramsPost);
            ServicePointManager.ServerCertificateValidationCallback = (a, b, c, d) => true;
            Uri uri = new Uri(UriBase);
            uri = uri.Append(relativePath);
            Debug.WriteLine(uri);
            return WebRequest(uri.ToString(), 30000, paramsPost);
            //using (var client = new WebClient())
            //{
            //    var dataToPost = Encoding.Default.GetBytes(paramsPost);
            //    var r = client.UploadData(uri, "POST", dataToPost);
            //    result = System.Text.Encoding.UTF8.GetString(r);
            //    // do something with the result
            //}
            //using (var client = new WebClient())
            //{
            //    client.Headers[HttpRequestHeader.ContentType] = "application/x-www-form-urlencoded";
            //    client.Encoding = Encoding.UTF8;
            //    Uri uri = new Uri(UriBase);
            //    uri = uri.Append(relativePath);
            //    Debug.WriteLine(uri);
            //    var result = client.UploadString(uri,"POST", paramsPost);
            //    Debug.WriteLine(result);
            //    return result;
            //}
        }

    }
}

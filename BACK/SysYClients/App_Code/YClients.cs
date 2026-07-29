using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using EtranLib.Data;
using System.Data;

/// <summary>
/// Summary description for YClients
/// </summary>
public class YClients
{
    readonly int Company_id;
    readonly string Bearer;
    readonly string User_Token;
    readonly string login;
    readonly string pwd;
    readonly string data;
    readonly DBManager db;
    readonly string DbConnectionString;
    readonly Uri baseAddress;
    readonly long Phone;

    public YClients(int terminal, long phone)
    {
        baseAddress = new Uri(EtranConfigurationManager.UrlPay);
        Bearer = EtranConfigurationManager.Token;
        DbConnectionString = GetDbConn("Organizations");
        db = new DBManager(DbConnectionString);
        var ds = (DataSet)db.Execute("YClients_Get", CommandType.StoredProcedure, DBManager.DataReadType.DataSet, null, terminal, phone);
        Company_id = (int)ds.Tables[0].Rows[0]["staff_id"];
        GlobalObjectsManager.Logger.Info("Company_id: " + Company_id);
        if (Company_id == 0)
            throw new Exception("Company_id == 0 check terminal for tb_YClientsTerminals");
        login = EtranConfigurationManager.Login;
        GlobalObjectsManager.Logger.Info("login: " + login);
        pwd = EtranConfigurationManager.Pwd;
        GlobalObjectsManager.Logger.Info("pwd: " + pwd);
        data = (string)ds.Tables[0].Rows[0]["data"];
        GlobalObjectsManager.Logger.Info("data: " + data);
        User_Token = GetUserTokenAsync(login, pwd).Result;
        GlobalObjectsManager.Logger.Info("User_Token: " + User_Token);
        Phone = phone;
    }
    static public string GetDbConn(string name)
    {
        string ret = string.Empty;
        try
        {
            ret = (new System.Net.WebClient()).DownloadString("http://bl.corepay.local/EtranConfig/" + "?function=dbconn&dbname=" + name);
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error(ex);
        }
        return ret;
    }
    public async Task<string> GetUserTokenAsync(string login, string pass)
    {
        using (var httpClient = new HttpClient { BaseAddress = baseAddress })
        {
            httpClient.DefaultRequestHeaders.TryAddWithoutValidation("authorization", "Bearer " + Bearer);
            using (var content = new StringContent("{  \"login\": \"" + login + "\",  \"password\": \"" + pass + "\"}", System.Text.Encoding.Default, "application/json"))
            {
                using (var response = await httpClient.PostAsync("auth", content))
                {
                    var responseData = await response.Content.ReadAsStringAsync();
                    dynamic stuff = JObject.Parse(responseData);
                    string v = stuff.ToString(Newtonsoft.Json.Formatting.Indented);
                    GlobalObjectsManager.Logger.Info(v);
                    return stuff.user_token;
                }
            }
        }
    }

    class Item
    {
        public string Name { get; set; }
        public int Cost { get; set; }
    }

    class ItemDb
    {
        public int id { get; set; }
        public int cost { get; set; }
        public int discount { get; set; }
        public int first_cost { get; set; }
        public int record_id { get; set; }

    }


    public async Task<string> DoCheckAsync()
    {
        string service_datetime = "";
        string name = "";
        string mail = "";
        int totalCost = 0;
        int count = 0;
        List<Item> items = new List<Item>();
        List<ItemDb> itemsDb = new List<ItemDb>();
        string result = "";
        string client_id = "";
        int master_id = 0;
        string master_name = "";
        using (var httpClient = new HttpClient { BaseAddress = baseAddress })
        {
            GlobalObjectsManager.Logger.Info("GetAsync(\"clients / " + Company_id + " ? phone = " + Phone+")\"");
            httpClient.DefaultRequestHeaders.TryAddWithoutValidation("authorization", "Bearer " + Bearer + ", User " + User_Token);
            using (var response = await httpClient.GetAsync("clients/" + Company_id + "?phone=" + Phone))
            {
                string responseData = await response.Content.ReadAsStringAsync();
                
                //var file = System.IO.Path.Combine(System.Web.HttpContext.Current.Server.MapPath("~"), "client.json");
                //string responseData = System.IO.File.ReadAllText(file, Encoding.UTF8);
                
                dynamic stuff = JObject.Parse(responseData);
                GlobalObjectsManager.Logger.Info(stuff.ToString(Newtonsoft.Json.Formatting.Indented));
                count = stuff.count;
                if (count > 0)
                {
                    client_id = stuff.data[0].id;
                    name = stuff.data[0].name;
                    mail = stuff.data[0].mail;
                }
            }
            if (!string.IsNullOrEmpty(client_id))
            {
                GlobalObjectsManager.Logger.Info("GetAsync(\"records/ " + Company_id + " ? client_id= " + client_id + ")\"");

                using (var response = await httpClient.GetAsync("records/" + Company_id + "?client_id=" + client_id))
                {
                    string responseData = await response.Content.ReadAsStringAsync();
                    
                    //var file = System.IO.Path.Combine(System.Web.HttpContext.Current.Server.MapPath("~"), "service.json");
                    //string responseData = System.IO.File.ReadAllText(file, Encoding.UTF8);

                    dynamic stuff = JObject.Parse(responseData);
                    GlobalObjectsManager.Logger.Info(stuff.ToString(Newtonsoft.Json.Formatting.Indented));
                    foreach (var data in stuff.data)
                    {
                        // *visit_attendance* - 2 - Пользователь подтвердил запись, 1 - Пользователь пришел, услуги оказаны, 0 - ожидание пользователя, -1 - пользователь не пришел на визит
                        // Нужно чтобы терем видел только записи со статусом пользователь пришел 
                        int visit_attendance = data.visit_attendance;
                        if (visit_attendance == 1)
                        {
                            service_datetime = data.datetime;
                            master_id = data.staff.id;
                            master_name = data.staff.name;
                            foreach (var s in data.services)
                            {
                                items.Add(new Item { Name = s.title, Cost = (int)s.cost });
                                totalCost += (int)s.cost;
                                itemsDb.Add(new ItemDb
                                {
                                    id = (int)s.id
                                    ,
                                    cost = (int)s.cost
                                    ,
                                    discount = (int)s.discount
                                    ,
                                    first_cost = (int)s.first_cost
                                    ,
                                    record_id = (int)data.id
                                });
                            }
                            break;
                        }
                    }
                }
            }
            var x = new
            {
                totalCost = totalCost,
                items = items.Select(item => new
                {
                    name = item.Name,
                    cost = item.Cost,
                })
            };
            var myJObject = JObject.FromObject(x);
            result = myJObject.ToString(Newtonsoft.Json.Formatting.None);
            if (itemsDb.Count>0)
            {
                var resDb = new
                {
                    staff_id = master_id,
                    name = master_name,
                    attendance = 0,
                    comment = "",
                    services = itemsDb.Select(s => new
                    {
                        id = (int)s.id
                                    ,
                        cost = (int)s.cost
                                    ,
                        discount = (int)s.discount
                                    ,
                        first_cost = (int)s.first_cost
                                    ,
                        record_id = (int)s.record_id
                    })
                    ,
                    client = new { phone = Phone, name = name, email = mail }
                    ,
                    datetime = service_datetime
                    ,
                    seance_length = 900
                    ,
                    save_if_busy = true
                    ,
                    send_sms = false
                    ,
                    fast_payment = 1
                };
                var resDbj = JObject.FromObject(resDb);
                string resultDb = resDbj.ToString(Newtonsoft.Json.Formatting.None);
                GlobalObjectsManager.Logger.Info("resultDb: " + resultDb);
                db.Execute("YClients_Reserv", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, null, Phone, resultDb);
            }
            return result;
        }
    }


    public async Task<string> DoPaymentAsync(int paymId, bool card)
    {
        GlobalObjectsManager.Logger.Info("DoPaymentAsync card: " + card);
        GlobalObjectsManager.Logger.Info("DoPaymentAsync data: " + data);
        dynamic stuff = JObject.Parse(data);
        stuff.attendance = 1;
        stuff.seance_length = 900;
        stuff.fast_payment = card ? 2 : 1;
        int record_id = stuff.services[0].record_id;
        int master_id = stuff.staff_id;
        string master_name = stuff.name;
        long phone = stuff.client.phone;
        GlobalObjectsManager.Logger.Info("master_id " + master_id + " master_name " + master_name);
        string contentStr = stuff.ToString(Newtonsoft.Json.Formatting.None);
        using (var httpClient = new HttpClient { BaseAddress = baseAddress })
        {
            GlobalObjectsManager.Logger.Info("PutAsync(\"record/ " + Company_id + "/" + record_id + ")\"");

            httpClient.DefaultRequestHeaders.TryAddWithoutValidation("authorization", "Bearer " + Bearer + ", User " + User_Token);
            using (var content = new StringContent(contentStr, System.Text.Encoding.UTF8, "application/json"))
            using (var response = await httpClient.PutAsync("record/" + Company_id + "/" + record_id, content))
            {
                string responseData = await response.Content.ReadAsStringAsync();
                dynamic response_stuff = JObject.Parse(responseData);
                GlobalObjectsManager.Logger.Info(response_stuff.ToString(Newtonsoft.Json.Formatting.Indented));
                if(response_stuff["errors"] != null)
                {
                    string ret = "";
                    ret += response_stuff.errors.code +":"+ response_stuff.errors.message;
                    return ret;
                }
            }
        }
        GlobalObjectsManager.Logger.Info("db payment insert paymId " + paymId);
        db.Execute("YClients_Pay", CommandType.StoredProcedure, DBManager.DataReadType.ExecuteNonQuery, null, paymId, phone, master_id, master_name);
        return "ok";
    }
}

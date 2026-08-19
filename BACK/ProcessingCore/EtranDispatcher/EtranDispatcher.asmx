<%@ WebService Language="C#" Class="EtranDispatcher.EtranDispatcher" %>

<script runat="server">
using System;
using System.Web.Services;
using System.ComponentModel;

namespace EtranDispatcher
{
    /// <summary>
    /// Summary description for Service1
    /// </summary>
    [WebService(Namespace = "http://tempuri.org/")]
    [WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
    [ToolboxItem(false)]
    public class EtranDispatcher : System.Web.Services.WebService
    {
        [WebMethod]
        public string HelloWorld()
        {
            return "Hello World";
        }
    }
}
</script>
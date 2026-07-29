			// 1-й пар-р тип запроса 1- разрешить или запретить работу
			// 2-й пар-р ID модуля
			// 3-й пар-р код ответа
			// 4-й пар-р подпись (MD5)

using System;
using System.Xml;
using System.IO;
using System.Text;
using System.Runtime.Remoting;

using System.Runtime.Remoting.Channels;
using System.Runtime.Remoting.Channels.Http;
//using System.Runtime.Remoting.Channels.Tcp;
using CyberInterface;
using System.Runtime.Serialization;
using System.Reflection;
using System.Threading;
using System.Diagnostics;




namespace RemotingInterfaceServer
{
	public class EntryPoint
	{
		public static void Main(string[] args)
		{
            clsLogger.Instance().SaveInfo("START...");
			string sLocation =Assembly.GetEntryAssembly().Location;
			clsLogger.Instance().SaveInfo("Location: "+sLocation);
			string sDirectoryName = Path.GetDirectoryName(sLocation);
			CyberplatClass.m_Exe_path = sDirectoryName+"\\";
			string sName = "CybServer";

			try
			{
                string xml_config = CyberplatClass.m_Exe_path + sName + ".xml";
                clsLogger.Instance().SaveInfo("CyberplatClass.m_Exe_path: " + CyberplatClass.m_Exe_path);
                clsLogger.Instance().SaveInfo("sName: " + sName);
                clsLogger.Instance().SaveInfo("xml_config: " + xml_config);

                XmlDocument doc = new XmlDocument();
                doc.Load(xml_config);

                XmlNode node = doc.SelectSingleNode("/configuration/service");
                int iPort = Convert.ToInt32(node.Attributes.GetNamedItem("port").Value);
                string objectUri = node.Attributes.GetNamedItem("objectUri").Value;


                HttpServerChannel channel = new HttpServerChannel(iPort);
                ChannelServices.RegisterChannel(channel, false);
                RemotingConfiguration.RegisterWellKnownServiceType(
                    typeof(CyberplatClass),
                    objectUri, WellKnownObjectMode.Singleton);

                clsLogger.Instance().SaveInfo(string.Format("START... port {0}, objectUri {1}", iPort, objectUri));
                Thread.Sleep(Timeout.Infinite);

                //CyberplatClass cl = new CyberplatClass();
                //string url;
                //string msg;
//                //cl.Encode(new string[] { "100", "54647343", "100", "14 9162376067" }, 1, out url, out msg);

//2012-01-18 16:33:16,143 - {R} ID: 1 web_ret 0000028601SM000000250000002500000125
//0J0005              00904291
//                    00000000
//BEGIN
//ERROR=0
//REST=758388.01

//END
//BEGIN SIGNATURE
//iQBRAwkBAA3MY08WvAwBAW9sAf4uk9GeV4nle2E0rCWWdnDq8BbbnIEHO4aeyp/d
//gknWm9eEdwogHJFDEEaBn5u7+mGzFXdapEXliAt8rCg23hjIsAHH
//=Wa/F
//END SIGNATURE


//                string msgIn = @"0000028501SM000000240000002400000125
//0J0005              00904291
//                    00000000
//BEGIN
//ERROR=0
//REST=11703.71
//
//END
//BEGIN SIGNATURE
//iQBRAwkBAA3MY08WvHIBAah8Af94qGxSTuRfBfoBpxH/6twD0+mgmv1xEOMh7Ron
//JORsBstZOKFJDZTa/fjcL74I7GaS+aWOcqV7xySTf91IyG/JsAHH
//=xcbo
//END SIGNATURE";

//                string MsgOut = string.Empty;
//                bool Ret = cl.Decode(msgIn, out MsgOut);


			}
			catch(Exception ex)
			{
                clsLogger.Instance().SaveError("EntryPoint.Main>>", ex);
			}
		}
	}
}


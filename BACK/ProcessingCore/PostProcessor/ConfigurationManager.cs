using System;
using System.Web;
using System.Configuration;

namespace PostProcessor
{
	/// <summary>
	/// Менеджер конфигурации диспетчера.
	/// </summary>
	public sealed class EtranConfigurationManager
	{

        private EtranConfigurationManager() { }


        static public string MessageProcessor
        {
            get
            {
                return ConfigurationManager.AppSettings["MessageProcessor"];
            }
        }


        static public int DBConnectionTimeOut
        {
            get
            {
                return int.Parse(ConfigurationManager.AppSettings["DBConnectionTimeOut"]);
            }
        }


        static public int TimeBuffer
        {
            get
            {
                return int.Parse(ConfigurationManager.AppSettings["TimeBuffer"]);
            }
        }

        static public int MPThreadLimit
        {
            get
            {
                return int.Parse(ConfigurationManager.AppSettings["MPThreadLimit"]);
            }
        }

        static public int StartStep
        {
            get
            {
                return 1000*int.Parse(ConfigurationManager.AppSettings["StartStep"]);
            }
        }

        static public int QueueSize
        {
            get
            {
                return int.Parse(ConfigurationManager.AppSettings["QueueSize"]);
            }
        }

        static public int StopTimeOut
        {
            get
            {
                return 1000*int.Parse(ConfigurationManager.AppSettings["StopTimeOut"]);
            }
        }

        static public string SP_NAME
        {
            get
            {
                return ConfigurationManager.AppSettings["SP_NAME"];
            }
        }

        
        static public string ThreadPath
        {
            get
            {
                return ConfigurationManager.AppSettings["ThreadPath"];
            }
        }

        static public string DbConnectionString
        {
            get
            {
                return GlobalObjectsManager.DbConnectionString;
            }
        }

        static public string EtranConfig
        {
            get
            {
                return ConfigurationManager.AppSettings["EtranConfig"];
            }
        }

        static public string DBConnName
        {
            get
            {
                return ConfigurationManager.AppSettings["DBConnName"];
            }
        }


	}
}

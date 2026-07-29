using System;
using System.Configuration;
using Estylesoft.DALC;
using Estylesoft.Etran;
using System.Collections.Specialized;
using System.Collections;

namespace MessageProcessor
{

    /// <summary>
    /// Менеджер конфигурации службы.
    /// </summary>
    public sealed class EtranConfigurationManager
    {
        private const int MIN_JOBS_QUEUE_CAPACITY = 1;

        private EtranConfigurationManager() { }


        static public string DoRequest(RequestMessage msg)
        {
            return (new ProxyInterfaceService(msg.Url)).DoRequest(msg.Function.ToString(), msg.PaymExtId, msg.PaymSubjTp.ToString(), (msg.AltAmount > 0) ? msg.AltAmount.ToString() : msg.Amount.ToString(), msg.Params, EtranConfigurationManager.PaySystemsTimeOut, msg.Rek, msg.TotalSum.ToString());
        }

        private static NameValueCollection nvc_limit = null;
        class PaymentLimitTspAfter
        {
            public int TspStart { get; set; }
            public long Amount { get; set; }
        }
        private static PaymentLimitTspAfter _paymentLimitTspAfter = null;
        static public long CheckLimit(int tsp_code)
        {
            long lm = 0;
            try
            {
                if (nvc_limit == null)
                    nvc_limit = EtranLib.Collection.Collection.GetNameValueCollection(ConfigurationManager.AppSettings["PaymentLimit"]);
                if (_paymentLimitTspAfter == null)
                {
                    var l = ConfigurationManager.AppSettings["PaymentLimitTspAfter"];
                    if (l != null && l.Length > 0)
                    {
                        var v = l.Split(' ');
                        _paymentLimitTspAfter = new PaymentLimitTspAfter { TspStart = int.Parse(v[0]), Amount = long.Parse(v[1]) };
                    }
                }

                if (tsp_code >= _paymentLimitTspAfter.TspStart)
                {
                    lm = _paymentLimitTspAfter.Amount;
                }
                else
                {
                    string amount = nvc_limit[tsp_code.ToString()];
                    if (amount == null)
                    {
                        amount = nvc_limit["default"];
                    }
                    lm = long.Parse(amount);
                }
            }
            catch { }
            return lm;
        }


        static public int TimeZoneMinuteRange
        {
            get
            {
                return int.Parse(ConfigurationManager.AppSettings["TimeZoneMinuteRange"]);
            }
        }


        static public string SERIAL_TEST
        {
            get
            {
                return ConfigurationManager.AppSettings["SERIAL_TEST"];
            }
        }

        static public string PaymentLimitErrorMessage
        {
            get
            {
                return ConfigurationManager.AppSettings["PaymentLimitErrorMessage"];
            }
        }

        static public short PaySystemsTimeOut
        {
            get
            {
                return short.Parse(ConfigurationManager.AppSettings["PaySystemsTimeOut"]);
            }
        }

        static public long SMS_cost
        {
            get
            {
                return long.Parse(ConfigurationManager.AppSettings["SMS_cost"]);
            }
        }

        static public string SMS_param_code
        {
            get
            {
                return ConfigurationManager.AppSettings["SMS_param_code"];
            }
        }

        static public string SMS_login
        {
            get
            {
                return ConfigurationManager.AppSettings["SMS_login"];
            }
        }

        static public string SMS_url
        {
            get
            {
                return ConfigurationManager.AppSettings["SMS_url"];
            }
        }

        static public string SMS_pwd
        {
            get
            {
                return ConfigurationManager.AppSettings["SMS_pwd"];
            }
        }


        /// <summary>
        /// Имя модуля сопряжения, асоциированного с данной службой.
        /// </summary>
        static public string ModuleName
        {
            get
            {
                return ConfigurationManager.AppSettings["ModuleName"];
            }
        }

        /// <summary>
        /// Вместительность очереди обработчиков платежных сообщений.
        /// </summary>
        static public int JobsQueueCapacity
        {
            get
            {
                int jobsQueueCapacity = Convert.ToInt32(ConfigurationManager.AppSettings["JobsQueueCapacity"]);

                if (jobsQueueCapacity < MIN_JOBS_QUEUE_CAPACITY)
                {
                    throw new ArgumentOutOfRangeException("JobsQueueCapacity", jobsQueueCapacity, "Вместительность очереди потоков должна быть больше либо равна " + MIN_JOBS_QUEUE_CAPACITY.ToString() + " .");
                }

                return jobsQueueCapacity;
            }
        }


        static public string EtranConfig
        {
            get
            {
                return ConfigurationManager.AppSettings["EtranConfig"];
            }
        }

        /// <summary>
        /// Строка соединения с БД, хранящей очередь платежных сообщений.
        /// </summary>
        static public string PaymentDbConnectionString
        {
            get
            {
                //return ConfigurationManager.ConnectionStrings["PaymentDbConnectionString"].ConnectionString;
                return GlobalObjectsManager.PaymentDbConnectionString;
            }
        }

        static public string ServiceDbConnectionString
        {
            get
            {
                //return ConfigurationManager.ConnectionStrings["ServiceDbConnectionString"].ConnectionString;
                return GlobalObjectsManager.ServiceDbConnectionString;
            }
        }

        static public string DBConnNamePayments
        {
            get
            {
                return ConfigurationManager.AppSettings["DBConnNamePayments"];
            }
        }

        static public string DBConnNameService
        {
            get
            {
                return ConfigurationManager.AppSettings["DBConnNameService"];
            }
        }

        static int[] _tspAutoAdd = null;
        static public bool IsTspAutoAdd(int tspnum)
        {
            try
            {
                if (_tspAutoAdd == null)
                {
                    var t = ConfigurationManager.AppSettings["TspAutoAdd"];
                    if (!string.IsNullOrEmpty(t))
                    {
                        var a = t.Split('-');
                        _tspAutoAdd = new int[] { int.Parse(a[0]), int.Parse(a[1]) };
                    }
                }

                if (_tspAutoAdd != null)
                {
                    return tspnum >= _tspAutoAdd[0] && tspnum <= _tspAutoAdd[1];
                }

            }
            catch { }
            return false;
        }

    }
}

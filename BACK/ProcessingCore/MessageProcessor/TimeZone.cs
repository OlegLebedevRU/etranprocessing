using System;
using System.Data;
using System.Configuration;
using System.Web;
using System.Web.Security;
using System.Web.UI;
using System.Web.UI.WebControls;
using System.Web.UI.WebControls.WebParts;
using System.Web.UI.HtmlControls;
using System.Collections;
using EtranLib.Data;
using System.Threading;


namespace MessageProcessor
{
    public class TimeZone
    {
        static private Hashtable m_htblTimeZone = new Hashtable();
        static private Hashtable m_htblBlocked = new Hashtable();
        static private bool m_bCheckTime = false;
        static private Thread th_reestr = null;
        static public bool IsRunApp = false;
        static public bool IsThreadExe = false;


        static public bool IsBlocked(int org_id)
        {
            if (!IsThreadExe)
                return true;

            if (m_bCheckTime)
            {
                object val = m_htblBlocked[org_id];
                if (val == null)
                    val = m_htblBlocked[0];
                if ((int)val == 1)
                    return true;
                else
                    return false;
            }
            return false;
        }

        static public void Init()
        {
            try
            {
                if (th_reestr == null)
                {
                    DBManager db = new DBManager(EtranConfigurationManager.ServiceDbConnectionString);
                    DataSet ds = (DataSet)db.Execute("select org_id, tz_utc  from service..TimeZones tz inner join service..TimeZonesOrgs tzo on(tz.tz_id = tzo.tz_id)", CommandType.Text, DBManager.DataReadType.DataSet, null);

                    m_htblTimeZone.Add(0, 0);
                    for (int i = 0; i < ds.Tables[0].Rows.Count; i++)
                        m_htblTimeZone.Add(ds.Tables[0].Rows[i]["org_id"], ds.Tables[0].Rows[i]["tz_utc"]);

                    th_reestr = new Thread(Controller);
                    th_reestr.Start();
                    IsRunApp = true;
                }
            }
            catch (Exception ex)
            {
        
                GlobalObjectsManager.Logger.Error("TimeZone.Init, ", ex);
            }
        }

        static public void UnInit()
        {
            IsRunApp = false;
            Thread.Sleep(1500);
            try
            {
                if (th_reestr != null && th_reestr.IsAlive)
                {
                    GlobalObjectsManager.Logger.Info("TimeZone.UnInit: try to abort");
                    th_reestr.Abort();
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("TimeZone.UnInit, ", ex);
            }
        }

        static public void Controller()
        {
            Thread.Sleep(100);
            GlobalObjectsManager.Logger.Info("TimeZone Controller START...");
            
            TimeSpan midnightA = new TimeSpan(0, (24*60) - EtranConfigurationManager.TimeZoneMinuteRange, 0);
            TimeSpan midnightB = new TimeSpan(0, EtranConfigurationManager.TimeZoneMinuteRange, 0);
            const int sleep = 1000;
            const int k = 60;
            int i = 0;
            while (IsRunApp && GlobalObjectsManager.IsRunApp)
            {
                bool bCheckTime = false;
                foreach (DictionaryEntry entry in m_htblTimeZone)
                {
                    try
                    {
                        int blocked = 0;
                        int org_id = (int)entry.Key;
                        int utc_add = (int)entry.Value;
                        TimeSpan dt_check = DateTime.Now.TimeOfDay;
                        if (utc_add != 0)
                            dt_check = DateTime.UtcNow.AddHours(utc_add).TimeOfDay;

                        //if (utc_add ==0)
                        //  dt_check = dt_check.Add(new TimeSpan(4, 20, 0));

                        GlobalObjectsManager.Logger.Info("TimeZone CHECK dt_check: "
                            + dt_check + " org_id: " + org_id + " midnightA : " + midnightA.ToString() + " midnightB: " + midnightB.ToString());

                        if (dt_check > midnightA || dt_check < midnightB)
                        {
                            bCheckTime = true;
                            blocked = 1;
                        }
                        else
                        {
                            blocked = 0;
                        }

                        if (blocked == 1)
                            GlobalObjectsManager.Logger.Info("TimeZone BLOCK org_id: " + org_id + " blocked: " + blocked);
                        else
                            GlobalObjectsManager.Logger.Info("TimeZone UNBLOCK org_id: " + org_id + " blocked: " + blocked);

                        if (m_htblBlocked[org_id] == null)
                        {
                            m_htblBlocked.Add(org_id, blocked);
                        }
                        else
                        {
                            m_htblBlocked[org_id] = blocked;
                        }

                    }
                    catch (Exception ex)
                    {
                        //Program.Logger.Error("SingleStep[" + ThreadID + "], " + StartLine, ex);
                        System.Diagnostics.Debug.WriteLine(ex.Message);
                        GlobalObjectsManager.Logger.Error("TimeZone Controller: " + ex.Message);
                    }
                }
                m_bCheckTime = bCheckTime;
                if (!IsThreadExe)
                    IsThreadExe = true;

                i = 0;
                while (i < k)
                {
                    if (IsRunApp && GlobalObjectsManager.IsRunApp)
                        Thread.Sleep(sleep);
                    else
                    {
                        GlobalObjectsManager.Logger.Info("TimeZone Controller break");
                        break;
                    }
                    i++;
                }

            }
            GlobalObjectsManager.Logger.Info("TimeZone Controller DONE.");
        }
    }
}

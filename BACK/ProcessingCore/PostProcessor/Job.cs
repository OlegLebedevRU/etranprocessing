using System;
using System.Data;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Diagnostics;
using System.Collections;

namespace PostProcessor
{

    public class EtranThread
    {
        public int mp_id;
        public bool enabled;
        public string descr;
        public int step; // millisec
        public int exec_step; // millisec
    }

    class Job
    {
        public Job(EtranThread Config)
        {
            m_config = Config;
        }

        
        private EtranThread m_config;

        public EtranThread Config
        {
            set
            {
                m_config = value;
                ((ManualResetEvent)update[0]).Set();
                GlobalObjectsManager.Logger.Info("thread updated.");
            }
            get
            {
                return m_config;
            }
        }

        WaitHandle[] update = new WaitHandle[]     {
        new ManualResetEvent(false)
        };


        public void ThreadPoolCallback(Object threadContext)
        {
            MyHandler done = ((MyHandler)threadContext);
            GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " [" + m_config.descr + "] - START...");
            while (m_config.enabled)
            {
                int totaltime = 0;
                try
                {
                    GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " - TRY...");
                    MessageProcessor mp = new MessageProcessor(EtranConfigurationManager.MessageProcessor);
                    
                    DataSet data = DbInterface.ExecuteSP(m_config.mp_id, EtranConfigurationManager.DBConnectionTimeOut);
                    int i = data.Tables[0].Rows.Count;
                    //int time_wait = m_config.step - EtranConfigurationManager.TimeBuffer;
                    //GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " - FOUND: " + i+" time_wait: "+time_wait);
                    GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " - FOUND: " + i);
                    //int count_start = Environment.TickCount;
                    while (i > 0)
                    {
                        i--;
                        try
                        {
                            int paym_id = (int)data.Tables[0].Rows[i]["paym_id"];
                            string PaymExtId = (string)data.Tables[0].Rows[i]["PaymExtId"];
                            int PaymSubjTp = (int)data.Tables[0].Rows[i]["PaymSubjTp"];
                            long paym_amount = (long)data.Tables[0].Rows[i]["paym_amount"];
                            int serial_number = (int)data.Tables[0].Rows[i]["serial_number"];
                            int totalsum = (int)data.Tables[0].Rows[i]["totalsum"];
                            string url = (string)data.Tables[0].Rows[i]["url"];
                            string rek = (string)data.Tables[0].Rows[i]["rek"];
                            int ps_id = (int)data.Tables[0].Rows[i]["ps_id"];
                            long AltAmount = (long)data.Tables[0].Rows[i]["AltAmount"];

                            //GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " - WAIT ASSIGN: " + paym_id);
                            //long avjobs = mp.GetAvailableJobs();
                            //while (avjobs < EtranConfigurationManager.MPThreadLimit)
                            //{
                            //    Thread.Sleep(m_config.exec_step);
                            //    int end_start = Environment.TickCount;
                            //    int timeleft = (end_start - count_start);
                            //    if (timeleft <= time_wait)
                            //    {
                            //        break;
                            //    }
                            //}
                            GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " - TRY ASSIGN: " + paym_id);

                            mp.AssignMessageParams(paym_id, PaymExtId, PaymSubjTp, paym_amount, serial_number, totalsum, url, rek, ps_id, AltAmount);
                            Thread.Sleep(m_config.exec_step);
                        }
                        catch (Exception e)
                        {
                            GlobalObjectsManager.Logger.Error(e);
                        }
                    }

                    //totaltime = (Environment.TickCount - count_start);
                }
                catch (Exception e)
                {
                    GlobalObjectsManager.Logger.Error(e);
                }
                finally
                {
                    
                    //if (data != null)
                      //  data.Close();
                }

                //GlobalObjectsManager.Logger.Info("totaltime: " + totaltime + " SLEEP FOR SEC" + m_config.step);
                GlobalObjectsManager.Logger.Info("SLEEP FOR SEC" + m_config.step);

                WaitHandle.WaitAny(update, m_config.step, false);
                ((ManualResetEvent)update[0]).Reset();

                GlobalObjectsManager.Logger.Info("after WaitOne");
                
            }

            GlobalObjectsManager.Logger.Info("MP_ID: " + m_config.mp_id + " DONE.");
            done(m_config);

        }
    }
}


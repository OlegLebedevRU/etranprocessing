using System;
using System.Collections.Generic;
using System.Collections;
using System.Text;
using System.Threading;
using System.Xml;
using System.Xml.Serialization;
using System.IO;
using System.Diagnostics;


namespace PostProcessor
{
    delegate void MyHandler(object o);

    class JobManager
    {
        private ArrayList m_al = new ArrayList();
        private bool Enable;

        public JobManager()
        {
            Enable = true;
        }


        public void StoppingAll()
        {
            GlobalObjectsManager.Logger.Info("StoppingAll is Start.");
            if (Enable)
            {
                lock (this)
                {
                    Enable = false;

                    GlobalObjectsManager.Logger.Info("StoppingAll m_al.Count: " + m_al.Count);
                    for (int i = 0; i < m_al.Count; i++)
                    {
                        EtranThread et = ((Job)m_al[i]).Config;
                        et.enabled = false;
                        ((Job)m_al[i]).Config = et;
                    }
                }
            }

            int CriticalTimeOut = EtranConfigurationManager.StopTimeOut;
            while(m_al.Count>0)
            {
                Thread.Sleep(100);
                CriticalTimeOut -= 100;
                if (CriticalTimeOut <= 0)
                {
                    GlobalObjectsManager.Logger.Info("CriticalTimeOut is out of time.");
                    break;
                }
             }

             if (m_al.Count == 0)
                 GlobalObjectsManager.Logger.Info("All Threads is removed OK.");
             else
                 GlobalObjectsManager.Logger.Info("Threads not removed.");

             GlobalObjectsManager.Logger.Info("StoppingAll is Done.");
            
        }

        private void Remove(object o)
        {
            lock (this)
            {
                EtranThread thread = (EtranThread)o;
                for (int i = 0; i < m_al.Count; i++)
                {
                    if (thread.mp_id == ((Job)m_al[i]).Config.mp_id)
                    {
                        m_al.RemoveAt(i);
                        GlobalObjectsManager.Logger.Info("Removed ID: " +thread.mp_id +" MESS: "+ thread.descr);
                        return;
                    }
                }
            }
        }

        private EtranThread UpdatedThread(EtranThread a)
        {
            for (int i = 0; i < m_al.Count; i++)
            {
                EtranThread et = ((Job)m_al[i]).Config;
                if (et.mp_id == a.mp_id)
                {
                    XmlSerializer s = new XmlSerializer(typeof(EtranThread));
                    StringBuilder sb1 = new StringBuilder();
                    StringBuilder sb2 = new StringBuilder();

                    XmlWriterSettings settings = new XmlWriterSettings();
                    XmlWriter writer = XmlWriter.Create(sb1, settings);
                    s.Serialize(writer, a);
                    writer.Flush();
                    writer = XmlWriter.Create(sb2, settings);
                    s.Serialize(writer, et);
                    writer.Flush();
                    if (string.Compare(sb1.ToString(), sb2.ToString(), true) != 0)
                    {
                        bool en = ((Job)m_al[i]).Config.enabled;
                        ((Job)m_al[i]).Config = a;
                        return (!en && a.enabled) ? null : a;
                    }
                    return a;
                }
            }
            return null;
        }


        public void Add(EtranThread a)
        {
            if (Enable)
            {
                lock (this)
                {
                    if (UpdatedThread(a) == null)
                        if (a.enabled == true)
                        {
                            Job Job = new Job(a);
                            m_al.Add(Job);
                            MyHandler gt = new MyHandler(Remove);
                            Debug.Assert(ThreadPool.QueueUserWorkItem(Job.ThreadPoolCallback, gt));
                        }
                }
            }
        }
    }
}

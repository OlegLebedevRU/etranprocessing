using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Xml;
using System.Diagnostics;
using System.Threading;
using System.Collections;


namespace PostProcessor
{
    class MessageProcessing
    {
        private Queue<FileSystemEventArgs> fifo;
        private JobManager JobManager;
        private bool Enable = false;
        private Thread Thread;
        private Thread ThreadStat;

        public MessageProcessing()
        {
            try
            {
                GlobalObjectsManager.Logger.Info("MessageProcessing() START");
                fifo = new Queue<FileSystemEventArgs>(EtranConfigurationManager.QueueSize);
                JobManager = new JobManager();
                FileSystemWatcher watch = new FileSystemWatcher(EtranConfigurationManager.ThreadPath, "*.xml");
                watch.EnableRaisingEvents = true;
                watch.Changed += new FileSystemEventHandler(OnChanged);
                watch.Created += new FileSystemEventHandler(OnChanged);
                //watch.Deleted += new FileSystemEventHandler(OnChanged);
                GlobalObjectsManager.Logger.Info("MessageProcessing() DONE");
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("MessageProcessing()", ex);
            }
        }


        private void Try(FileSystemEventArgs e)
        {
            Console.WriteLine("File: " + e.FullPath + " " + e.ChangeType);

            if (e.ChangeType == WatcherChangeTypes.Changed
                ||
                e.ChangeType == WatcherChangeTypes.Created
                )
            {
                try
                {
                    string data = e.FullPath;
                    using (StreamReader read = new StreamReader(e.FullPath, Encoding.GetEncoding(1251)))
                    {
                        data = read.ReadToEnd();
                    }
                    data = "<root>" + data + "</root>";

                    XmlDocument doc = new XmlDocument();
                    doc.LoadXml(data);
                    XmlNode node = doc.FirstChild;
                    int i = node.ChildNodes.Count;
                    while (i > 0)
                    {
                        i--;
                        XmlNode qw = node.ChildNodes[i];
                        Console.WriteLine(qw.OuterXml);
                        EtranThread th = new EtranThread();
                        th.mp_id = int.Parse(qw.Attributes["mp_id"].InnerText);
                        th.enabled = (int.Parse(qw.Attributes["enabled"].InnerText)) == 1 ? true : false;
                        th.descr = qw.Attributes["descr"].InnerText;
                        th.step = int.Parse(qw.Attributes["step"].InnerText) * 60000;
                        th.exec_step = int.Parse(qw.Attributes["exec_step"].InnerText);
                        
                        JobManager.Add(th);
                        Wait(EtranConfigurationManager.StartStep);
                    }


                }
                catch (Exception ex)
                {
                    GlobalObjectsManager.Logger.Error("Try",ex);
                }

            }

            //if (e.ChangeType == WatcherChangeTypes.Deleted)
            //{
            //   // th = new EtranThread();
            //    //FileInfo fileinfo = new FileInfo(e.FullPath);
            //    //th.name = fileinfo.Name;
            //    //th.SearchTemplate.m_value = fileinfo.Name;
            //}
            //else
            //{
            //    try
            //    {

            //      //  th = EtranThread.Get(e.FullPath);
            //        //FileInfo fileinfo = new FileInfo(e.FullPath);
            //        //th.name = fileinfo.Name;
            //        //th.SearchTemplate.m_value = fileinfo.Name;
            //    }
            //    catch (Exception err)
            //    {
            //        //Console.WriteLine(err);
            //        // если залочен ну хуй с ним
            //    }
            //}

            //if (th != null)
            //{
            //    JobManager.Add(th);
            //}
        }


        private void OnChanged(object source, FileSystemEventArgs e)
        {
            fifo.Enqueue(e);
        }

        private void Init(string thread_path)
        {
            string[] threads = Directory.GetFiles(thread_path);
            for (int i = 0; i < threads.Length; i++)
            {
                Thread.Sleep(EtranConfigurationManager.StartStep);
                FileInfo fileinfo = new FileInfo(threads[i]);
                FileSystemEventArgs args = new FileSystemEventArgs(WatcherChangeTypes.Created, fileinfo.DirectoryName, fileinfo.Name);
                Try(args);
            }
        }


        void Wait(int ms_wait)
        {
            int k = ms_wait;
            while ((k -= 10) > 0 && Enable)
                System.Threading.Thread.Sleep(10);
        }

        public void Stop()
        {
            Enable = false;
        }


        private void ThreadPoolState()
        {
            while (Enable)
            {
                int workerThreads, completionPortThreads;
                ThreadPool.GetAvailableThreads(out workerThreads, out completionPortThreads);
                GlobalObjectsManager.Logger.Info("workerThreads: " + workerThreads + "completionPortThreads: " + completionPortThreads);
                Wait(10000);
            }
        }

        private void MainStream(Object threadContext)
        {
            try
            {
                GlobalObjectsManager.Logger.Info("Init(EtranConfigurationManager.ThreadPath) START");
                Init(EtranConfigurationManager.ThreadPath);
                GlobalObjectsManager.Logger.Info("Init(EtranConfigurationManager.ThreadPath) DONE");

                while (Enable)
                {
                    Thread.Sleep(10);
                    if (fifo.Count > 0)
                    {
                        Wait(EtranConfigurationManager.StartStep);
                        Try(fifo.Dequeue());
                    }
                }
                JobManager.StoppingAll();
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("MainStream", ex);
            }
        }


        public void Start()
        {
            Enable = true;
            Thread = new Thread(MainStream);
            Thread.IsBackground = true;
            Thread.Start();
            ThreadStat = new Thread(ThreadPoolState);
            ThreadStat.IsBackground = true;
            ThreadStat.Start();
        }
    }
}

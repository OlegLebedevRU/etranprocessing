using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using log4net.Repository.Hierarchy;
using log4net.Appender;
using log4net.Layout;
using log4net.Core;
using log4net;

namespace CyberInterface
{
    internal class clsLogger
    {
        private string _LogFileName = "";

        protected clsLogger(string LogFileName)
        {
            try
            {
                _LogFileName = LogFileName;
                InitLogger();
            }
            catch { }
        }

        private static Hashtable _instance = new Hashtable();
        /// <summary> получение экземпляра объекта
        /// </summary>
        public static clsLogger Instance()
        {
            return Instance("DefaultLog");
        }

        /// <summary> получение экземпляра объекта
        /// </summary>
        public static clsLogger Instance(string LogFileName)
        {
            lock ("clsLogger")
            {
                if (!_instance.ContainsKey(LogFileName))
                {
                    _instance.Add(LogFileName, new clsLogger(LogFileName));
                }

                return (clsLogger)_instance[LogFileName];
            }
        }

        private void InitLogger()
        {
            try
            {
                Hierarchy hierarchy = (Hierarchy)LogManager.GetRepository();
                Logger Log = (Logger)hierarchy.GetLogger(_LogFileName);
                RollingFileAppender fileAppender = new RollingFileAppender();

                //if (_LogFileName == "DefaultLog")
                //    fileAppender.File = "Log\\" + Process.GetCurrentProcess().ProcessName + ".log";
                //else
                //    fileAppender.File = "Log\\" + _LogFileName + "." + Process.GetCurrentProcess().ProcessName + ".log";
                if (_LogFileName == "DefaultLog")
                    fileAppender.File = Process.GetCurrentProcess().ProcessName + ".log";
                else
                    fileAppender.File = _LogFileName + "." + Process.GetCurrentProcess().ProcessName + ".log";


                fileAppender.Layout = new PatternLayout("%d - %m%n");
                fileAppender.ImmediateFlush = true;
                fileAppender.RollingStyle = RollingFileAppender.RollingMode.Size;
                fileAppender.MaximumFileSize = "10MB";
                fileAppender.AppendToFile = true;
                fileAppender.MaxSizeRollBackups = 2;
                fileAppender.Threshold = Level.All;
                fileAppender.ActivateOptions();
                fileAppender.LockingModel = new FileAppender.MinimalLock();
                Log.Level = Level.All;
                Log.Additivity = true;
                Log.AddAppender(fileAppender);

                hierarchy.Configured = true;
            }
            catch { }
        }

        public void SaveError(object message, Exception exeption)
        {
            try
            {
                LogManager.GetLogger(_LogFileName).Error(message, exeption);
            }
            catch { }
        }

        public void SaveError(object message)
        {
            try
            {
                LogManager.GetLogger(_LogFileName).Error(message);
            }
            catch { }
        }

        public void SaveInfo(object message)
        {
            try
            {
                LogManager.GetLogger(_LogFileName).Info(message);
            }
            catch { }
        }
    }
}

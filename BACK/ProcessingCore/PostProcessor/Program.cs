using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Text;
using System.Xml.Serialization;
using System.Threading;
using System.Collections;
using System.ServiceProcess;

namespace PostProcessor
{

    class Program : ServiceBase
    {
        Thread _processingThread = null;
        MessageProcessing _mp = null;

        public void ThreadStartProc()
        {
            GlobalObjectsManager.Init();
            GlobalObjectsManager.Logger.Info("ThreadStartProc START...");
            _mp = new MessageProcessing();
            _mp.Start();
            GlobalObjectsManager.Logger.Info("ThreadStartProc DONE");

        }

        static void Main(string[] args)
        {
            Run(new Program());
        }

        public Program()
        {
            this.ServiceName = GlobalObjectsManager.ServiceName;
        }

        protected override void OnStart(string[] args)
        {
            try
            {
                base.OnStart(args);

                _processingThread = new Thread(new ThreadStart(ThreadStartProc));
                _processingThread.Start();
                //if (mp == null)
                //    mp = new MessageProcessing();
                //mp.Start();
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("OnStart", ex);
            }
        }

        protected override void OnStop()
        {
            try
            {
                base.OnStop();

                if (_processingThread != null)
                {
                    _processingThread.Abort();
                    _processingThread.Join();
                    _processingThread = null;
                }
                if (_mp != null)
                {
                    _mp.Stop();
                    Thread.Sleep(100);
                }
            }
            catch (Exception ex)
            {
                GlobalObjectsManager.Logger.Error("OnStop", ex);
            }
        }

        // <summary>

        /// Dispose of objects that need it here.

        /// </summary>

        /// <param name="disposing">Whether

        ///    or not disposing is going on.</param>

        protected override void Dispose(bool disposing)
        {
            base.Dispose(disposing);
        }

        /// <summary>

        /// OnPause: Put your pause code here

        /// - Pause working threads, etc.

        /// </summary>

        protected override void OnPause()
        {
            base.OnPause();
        }

        /// <summary>

        /// OnContinue(): Put your continue code here

        /// - Un-pause working threads, etc.

        /// </summary>

        protected override void OnContinue()
        {
            base.OnContinue();
        }

        /// <summary>

        /// OnShutdown(): Called when the System is shutting down

        /// - Put code here when you need special handling

        ///   of code that deals with a system shutdown, such

        ///   as saving special data before shutdown.

        /// </summary>

        protected override void OnShutdown()
        {
            base.OnShutdown();
        }

        /// <summary>

        /// OnCustomCommand(): If you need to send a command to your

        ///   service without the need for Remoting or Sockets, use

        ///   this method to do custom methods.

        /// </summary>

        /// <param name="command">Arbitrary Integer between 128 & 256</param>

        protected override void OnCustomCommand(int command)
        {
            //  A custom command can be sent to a service by using this method:

            //#  int command = 128; //Some Arbitrary number between 128 & 256

            //#  ServiceController sc = new ServiceController("NameOfService");

            //#  sc.ExecuteCommand(command);


            base.OnCustomCommand(command);
        }

        /// <summary>

        /// OnPowerEvent(): Useful for detecting power status changes,

        ///   such as going into Suspend mode or Low Battery for laptops.

        /// </summary>

        /// <param name="powerStatus">The Power Broadcast Status

        /// (BatteryLow, Suspend, etc.)</param>

        protected override bool OnPowerEvent(PowerBroadcastStatus powerStatus)
        {
            return base.OnPowerEvent(powerStatus);
        }

        /// <summary>

        /// OnSessionChange(): To handle a change event

        ///   from a Terminal Server session.

        ///   Useful if you need to determine

        ///   when a user logs in remotely or logs off,

        ///   or when someone logs into the console.

        /// </summary>

        /// <param name="changeDescription">The Session Change

        /// Event that occured.</param>

        protected override void OnSessionChange(
                  SessionChangeDescription changeDescription)
        {
            base.OnSessionChange(changeDescription);
        }
    }
}

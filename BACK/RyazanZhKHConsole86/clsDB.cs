using System;
using System.Collections.Generic;
using System.Web;
using System.IO;
using System.Xml;

/// <summary>
/// Сводное описание для clsDB
/// </summary>
public class clsDB
{
    static bool IsUpdate = false;

    public clsDB()
    {
    }

    static void Wait(int ms_wait)
    {
        int k = ms_wait;
        const int logEvent = 60000;

        while ((k -= 10) > 0 && GlobalObjectsManager.AppRun)
        {
            System.Threading.Thread.Sleep(10);
            if ((k % logEvent) == 0)
            {
                GlobalObjectsManager.Logger.Info("clsDB.Thread alive");
            }

        }
    }

    static public void Thread()
    {
        GlobalObjectsManager.Logger.Info("clsDB.Thread START");
        while (GlobalObjectsManager.AppRun)
        {
            UpdateDB();

            Wait(60000 * 10);
        }
        GlobalObjectsManager.Logger.Info("clsDB.Thread DONE");
    }

    static public void AppReset()
    {
        //GlobalObjectsManager.Logger.Info("AppReset START...");
        //XmlDocument xmldoc = new XmlDocument();
        //string baseDir = System.Web.HttpRuntime.AppDomainAppPath;
        //string configPath = baseDir + "web.config";
        //xmldoc.Load(configPath);
        //XmlNode node = xmldoc.LastChild["appSettings"];
        //XmlNode attr = null;
        //if (node != null)
        //{
        //    attr = node.SelectSingleNode("add[@key='DBsrc']");
        //    if (attr != null)
        //    {
        //        string value = attr.Attributes["value"].InnerText;
        //        attr.Attributes["value"].InnerText = value;
        //        xmldoc.Save(configPath);
        //    }
        //}
    }

    static public bool FileAvailablity(string name)
    {
        try
        {
            GlobalObjectsManager.Logger.Info("FileAvailablity try check file " + name);
            using (Stream stream = new FileStream(name, FileMode.Open))
            {
                System.Threading.Thread.Sleep(10);
            }

        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("FileAvailablity", ex);
            return false;
        }
        return true;
    }

    public static void CopyFile(string source, string dest)
    {
        using (FileStream sourceStream = new FileStream(source, FileMode.Open))
        {
            byte[] buffer = new byte[1024 * 1024]; // Change to suitable size after testing performance
            using (FileStream destStream = new FileStream(dest, FileMode.Create))
            {
                int i;
                while ((i = sourceStream.Read(buffer, 0, buffer.Length)) > 0)
                {
                    destStream.Write(buffer, 0, i);
                    //OnProgress(sourceStream.Position, sourceStream.Length);
                    System.Threading.Thread.Sleep(1);
                }
            }
        }
    }
    static public void UpdateDB()
    {
        System.Threading.Monitor.Enter(GlobalObjectsManager.Locker);
        IsUpdate = false;
        string updateMarkerFile = EtranConfigurationManager.DBdst + "update.txt";
        GlobalObjectsManager.Logger.Info("UpdateDB START");
        GlobalObjectsManager.Logger.Info("UpdateDB SRC: " + EtranConfigurationManager.DBsrc);
        GlobalObjectsManager.Logger.Info("UpdateDB DST: " + EtranConfigurationManager.DBdst);
        string sdt = DateTime.Now.ToString("yyyyMMddHHmmss");

        try
        {
            string[] fileArraySrc = Directory.GetFiles(EtranConfigurationManager.DBsrc, "*.*");
            GlobalObjectsManager.Logger.Info("UpdateDB found at " + EtranConfigurationManager.DBsrc + " files: " + fileArraySrc.Length);
            bool ready = false;

            for (int i = 0; i < fileArraySrc.Length; i++)
            {
                GlobalObjectsManager.Logger.Info("UpdateDB found file " + fileArraySrc[i]);
                ready = FileAvailablity(fileArraySrc[i]);
                if (!ready)
                {
                    GlobalObjectsManager.Logger.Info("UpdateDB file " + fileArraySrc[i] + " is currently in use");
                    break;
                }
                GlobalObjectsManager.Logger.Info("UpdateDB file " + fileArraySrc[i] + " ready for copy&remove");
            }

            if (ready && fileArraySrc.Length > 0)
            {
                GlobalObjectsManager.Logger.Info("UpdateDB new files to update OK");
                bool alreadyInProcess = File.Exists(updateMarkerFile);
                if (!alreadyInProcess)
                {
                    // try backup
                    if (Directory.Exists(EtranConfigurationManager.DBdst))
                    {
                        string BackupDirName = GlobalObjectsManager.curr_path + "BAZA-" +
                                               DateTime.Now.ToString("yyyyMMddHHmmssffff");
                        GlobalObjectsManager.Logger.Info("UpdateDB catalog BAZA exist, try remve it to " + BackupDirName);
                        Directory.Move(EtranConfigurationManager.DBdst, BackupDirName);
                    }
                    Directory.CreateDirectory(EtranConfigurationManager.DBdst);
                    File.WriteAllText(updateMarkerFile, "1");
                }
                for (int i = 0; i < fileArraySrc.Length; i++)
                {
                    string fileNameSrc = Path.GetFileName(fileArraySrc[i]);
                    GlobalObjectsManager.Logger.Info("UpdateDB try copy " + fileNameSrc + " to " + EtranConfigurationManager.DBdst + fileNameSrc);
                    //File.Copy(fileArraySrc[i], EtranConfigurationManager.DBdst + fileNameSrc, true);
                    CopyFile(fileArraySrc[i], EtranConfigurationManager.DBdst + fileNameSrc);
                    GlobalObjectsManager.Logger.Info("UpdateDB " + EtranConfigurationManager.DBdst + fileNameSrc + " !!! получено обновление.");
                    GlobalObjectsManager.Logger.Info("UpdateDB delete ftp " + fileArraySrc[i]);
                    File.Delete(fileArraySrc[i]);
                }
                File.Delete(updateMarkerFile);
                IsUpdate = true;
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("UpdateDB", ex);
        }
        finally
        {
            System.Threading.Monitor.Exit(GlobalObjectsManager.Locker);
        }

        try
        {
            if (IsUpdate)
            {
                GlobalObjectsManager.Logger.Info("UpdateDB AppReset...");
                AppReset();
            }
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("UpdateDB AppReset", ex);
        }
        GlobalObjectsManager.Logger.Info("UpdateDB DONE");
    }

    //static public void UpdateDB()
    //{
    //    System.Threading.Monitor.Enter(GlobalObjectsManager.Locker);
    //    IsUpdate = false;
    //    GlobalObjectsManager.Logger.Info("UpdateDB START");
    //    GlobalObjectsManager.Logger.Info("UpdateDB SRC: " + EtranConfigurationManager.DBsrc);
    //    GlobalObjectsManager.Logger.Info("UpdateDB DST: " + EtranConfigurationManager.DBdst);
    //    string sdt = DateTime.Now.ToString("yyyyMMddHHmmss");

    //    try
    //    {
    //        string[] fileArraySrc = Directory.GetFiles(EtranConfigurationManager.DBsrc, "*.pp");
    //        GlobalObjectsManager.Logger.Info("UpdateDB found at " + EtranConfigurationManager.DBsrc + " files: " + fileArraySrc.Length);
    //        string fileNameSrc = null;
    //        for (int i = 0; i < fileArraySrc.Length; i++)
    //        {
    //            fileNameSrc = Path.GetFileName(fileArraySrc[i]);
    //            GlobalObjectsManager.Logger.Info("TRY FIND TO BACKUP...");
    //            FileInfo fi = new FileInfo(EtranConfigurationManager.DBdst + fileNameSrc);
    //            if (fi.Exists)
    //            {
    //                GlobalObjectsManager.Logger.Info(EtranConfigurationManager.DBdst + fileNameSrc + " !!! EXIST, try backup IT.");
    //                File.Move(EtranConfigurationManager.DBdst + fileNameSrc, EtranConfigurationManager.DBdst + fileNameSrc + ".bak-" + sdt);
    //                GlobalObjectsManager.Logger.Info("BACKUP OK TO " + EtranConfigurationManager.DBdst + fileNameSrc + ".bak-" + sdt);
    //            }
    //            else
    //            {
    //                GlobalObjectsManager.Logger.Info(fileNameSrc + " NOT exist, try backup current DB file at " + EtranConfigurationManager.DBdst);
    //                string[] fileArrayDst = Directory.GetFiles(EtranConfigurationManager.DBdst, "RSN*.pp");
    //                GlobalObjectsManager.Logger.Info("UpdateDB found to backup CURRENT DB at " + EtranConfigurationManager.DBdst + " files: " + fileArrayDst.Length);

    //                for (int j = 0; j < fileArrayDst.Length; j++)
    //                {
    //                    string fileNameDst = Path.GetFileName(fileArrayDst[j]);
    //                    GlobalObjectsManager.Logger.Info("UpdateDB try backup file - " + fileNameDst);
    //                    File.Move(EtranConfigurationManager.DBdst + fileNameDst, EtranConfigurationManager.DBdst + fileNameDst + ".bak-" + DateTime.Now.ToString("yyyyMMddHHmmss"));
    //                }
    //            }
    //            GlobalObjectsManager.Logger.Info("UpdateDB try copy " + fileNameSrc + " to " + EtranConfigurationManager.DBdst + fileNameSrc);
    //            File.Copy(fileArraySrc[i], EtranConfigurationManager.DBdst + fileNameSrc, true);
    //            GlobalObjectsManager.Logger.Info("UpdateDB " + EtranConfigurationManager.DBdst + fileNameSrc + " !!! получено обновление.");
    //            GlobalObjectsManager.Logger.Info("UpdateDB delete ftp " + fileArraySrc[i]);
    //            File.Delete(fileArraySrc[i]);
    //            IsUpdate = true;
    //        }
    //    }
    //    catch (Exception ex)
    //    {
    //        GlobalObjectsManager.Logger.Error("UpdateDB", ex);
    //    }
    //    finally
    //    {
    //        System.Threading.Monitor.Exit(GlobalObjectsManager.Locker);
    //    }

    //    try
    //    {
    //        if (IsUpdate)
    //        {
    //            GlobalObjectsManager.Logger.Info("UpdateDB AppReset...");
    //            AppReset();
    //        }
    //    }
    //    catch (Exception ex)
    //    {
    //        GlobalObjectsManager.Logger.Error("UpdateDB AppReset", ex);
    //    }
    //    GlobalObjectsManager.Logger.Info("UpdateDB DONE");
    //}
}
using System;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;

/// <summary>
/// Сводное описание для clsReestr
/// </summary>
public class clsReestr
{


    public clsReestr()
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
                GlobalObjectsManager.Logger.Info("clsReestr.Thread alive");
            }
        }
    }


    static DateTime _lasttime = new DateTime();
    static public void Thread()
    {
        GlobalObjectsManager.Logger.Info("clsReestr.Thread START");
        _lasttime = DateTime.Now.AddDays(-2);
        while (GlobalObjectsManager.AppRun)
        {

            int lasttimeDay = int.Parse(_lasttime.ToString("yyyyMMdd"));
            int dateTimeNowDay = int.Parse(DateTime.Now.ToString("yyyyMMdd"));
            GlobalObjectsManager.Logger.Info("clsReestr.Thread check lasttime " + _lasttime.ToString(CultureInfo.InvariantCulture) + " lasttimeDay " + lasttimeDay + " dateTimeNowDay " + dateTimeNowDay);

            //if (lasttime < DateTime.Now.AddDays(-1) || (lasttime > DateTime.Now.AddDays(-1) && lasttime.Day < DateTime.Now.Day))
            if (lasttimeDay < dateTimeNowDay)
            {
                bool result = Upload();
                if (result)
                    _lasttime = DateTime.Now;
            }
            else
            {
                GlobalObjectsManager.Logger.Info("clsReestr.Thread check and wait else...");
            }
            Wait(60000 * 10);
        }
        GlobalObjectsManager.Logger.Info("clsReestr.Thread DONE");
    }

    static int IntParse(string value)
    {
        if (value == null)
            return 0;

        value = value.Replace(",", "");
        int iVal = 0;
        bool result = int.TryParse(value, out iVal);
        return iVal;
    }

    static public int GetDay(string str)
    {
        int Val = 0;

        Regex pattern =
            new Regex("-[0-9][0-9]-",
                RegexOptions.Compiled |
                RegexOptions.Singleline);

        Regex indxpat =
            new Regex("[0-9][0-9]",
                RegexOptions.Compiled |
                RegexOptions.Singleline);

        foreach (Match m in pattern.Matches(str))
            if (m.Success)
                foreach (Match f in indxpat.Matches(m.Value))
                    if (f.Success)
                        Val = IntParse(f.Value);

        return Val;
    }

    static public bool Upload()
    {
        GlobalObjectsManager.Logger.Info("clsReestr.Upload START");
        try
        {

            string[] fileArray = Directory.GetFiles(EtranConfigurationManager.ReestrSrc);
            GlobalObjectsManager.Logger.Info("clsReestr.Upload found at " + EtranConfigurationManager.ReestrSrc + " files: " + fileArray);
            string fileName = null;
            int day_now = DateTime.Now.Day;
            GlobalObjectsManager.Logger.Info("clsReestr.Upload day_now " + day_now);

            for (int i = 0; i < fileArray.Length; i++)
            {
                fileName = Path.GetFileName(fileArray[i]);
                int day_file = GetDay(fileName);
                if (day_file != day_now)
                {
                    GlobalObjectsManager.Logger.Info("clsReestr.Upload try copy " + fileName + " to " + EtranConfigurationManager.ReestrDst + fileName);
                    if (fileName.Substring(0, 3) == "13-")
                    {
                        GlobalObjectsManager.Logger.Info("clsReestr.Upload try copy " + fileName + " to " + EtranConfigurationManager.ReestrDst + fileName + " !!! TEST OPCODE 13 SKIP");
                        continue;
                    }


                    File.Copy(fileArray[i], EtranConfigurationManager.ReestrDst + fileName, true);

                    GlobalObjectsManager.Logger.Info("clsReestr.Upload " + fileName + " OK");
                    GlobalObjectsManager.Logger.Info("clsReestr.Upload try backup " + fileName);
                    if (!Directory.Exists(EtranConfigurationManager.SOGBackup))
                        Directory.CreateDirectory(EtranConfigurationManager.SOGBackup);


                    string backup_name = EtranConfigurationManager.SOGBackup + fileName + "-" + DateTime.Now.ToString("yyyyMMddHHmmssffff");
                    GlobalObjectsManager.Logger.Info("clsReestr.Upload try backup " + fileName + " to " + backup_name);
                    File.Move(fileArray[i], backup_name);
                    GlobalObjectsManager.Logger.Info("clsReestr.Upload OK backup" + fileName + " to " + backup_name);
                }
                else
                {
                    GlobalObjectsManager.Logger.Info("clsReestr.Upload !!! " + fileName + " еще рано.");
                }
            }
            return true;
        }
        catch (Exception ex)
        {
            GlobalObjectsManager.Logger.Error("clsReestr.Upload", ex);
        }
        GlobalObjectsManager.Logger.Info("clsReestr.Upload DONE");
        return false;
    }
}
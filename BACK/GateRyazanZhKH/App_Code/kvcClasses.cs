using System;
using System.Collections.Generic;
using System.Web;
using System.Collections.Specialized;
using System.Text.RegularExpressions;



public class clsKvc
{
    int PayLen = 0;
    int CntLen = 0;
    public readonly string PostAddress = string.Empty;
    public readonly string MonthPay;
    public readonly int Mode;
    public readonly int TotalPayment;
    public readonly List<int> list_Cnt;
    public readonly List<int> list_Pay;
    public List<clsPay> Pay = new List<clsPay>();
    public List<clsCnt> Cnt = new List<clsCnt>();
    private static NameValueCollection CntKoeffList = new NameValueCollection();



    static public List<int> GetIndx(string str, string spatt)
    {
        List<int> rez = new List<int>();

        Regex pattern =
            new Regex(spatt,
                RegexOptions.Compiled |
                RegexOptions.Singleline);

        Regex indxpat =
            new Regex("[0-9]",
                RegexOptions.Compiled |
                RegexOptions.Singleline);

        foreach (Match m in pattern.Matches(str))
            if (m.Success)
                foreach (Match f in indxpat.Matches(m.Value))
                    if (f.Success)
                        rez.Add(intParse(f.Value));
        rez.Sort();

        return rez;
    }

    public string UpdateUserInfo(clsKvc terminal)
    {

        string ChangeRet = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.SetMode, terminal.Mode);
        int value = int.MinValue;

        int service_type = int.MinValue;
        int service_vid = int.MinValue;
        int service_num = int.MinValue;

        //17 рабочий
        //пример 17 Водоотвед.              337,72        337,72         0,00
        for (int i = 0; i < terminal.Pay.Count; i++)
        {
            value = int.MinValue;

            string tn = terminal.Pay[i].GetName();
            foreach (clsPay pay in Pay)
                if (pay.GetName() == tn)
                {
                    service_vid = terminal.Pay[i].Vid;
                    GlobalObjectsManager.Logger.Info("UPDATE TRY PayVid: " + service_vid);

                    value = terminal.Pay[i].Sum;
                    GlobalObjectsManager.Logger.Info("UPDATE TRY Sum: " + value);

                    if (value != int.MinValue)
                    {
                        service_type = 1;
                        service_vid = terminal.Pay[i].Vid;
                        service_num = 0;
                        ChangeRet = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, value, service_type, service_vid, service_num);
                    }
                }
        }

        for (int i = 0; i < terminal.Cnt.Count; i++)
        {
            value = int.MinValue;
            string tn = terminal.Cnt[i].GetName();
            foreach (clsCnt cnt in Cnt)
                if (cnt.GetName() == tn)
                {

                    service_vid = terminal.Cnt[i].Vid;
                    GlobalObjectsManager.Logger.Info("UPDATE TRY CntVid: " + service_vid);

                    GlobalObjectsManager.Logger.Info("UPDATE TRY CntName: " + tn);

                    decimal Rash = terminal.Cnt[i].Rash;
                    GlobalObjectsManager.Logger.Info("UPDATE TRY Rash: " + Rash);

                    decimal ExpCons = terminal.Cnt[i].ExpCons;
                    GlobalObjectsManager.Logger.Info("UPDATE TRY ExpCons: " + ExpCons);

                    decimal FZS = terminal.Cnt[i].FZS;
                    GlobalObjectsManager.Logger.Info("UPDATE TRY FZS: " + FZS);

                    decimal Koeff = decimalParse(CntKoeffList[tn]);
                    GlobalObjectsManager.Logger.Info("UPDATE TRY Koeff: " + Koeff);

                    if (ExpCons > 0 || FZS > 0)
                    {
                        service_num = terminal.Cnt[i].Number;

                        if (ExpCons > 0)
                        {
                            decimal decValue = ExpCons / Koeff;
                            GlobalObjectsManager.Logger.Info("UPDATE TRY ExpCons dec_value: " + decValue);
                            value = (int) decValue;
                            GlobalObjectsManager.Logger.Info("UPDATE TRY ExpCons value: " + value);

                            ChangeRet = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.ExpCons, value,
                                service_vid, service_num);
                        }
                        if (FZS > 0)
                        {
                            decimal decValue = FZS / Koeff;
                            GlobalObjectsManager.Logger.Info("UPDATE TRY FZS dec_value: " + decValue);
                            value = (int)decValue;
                            GlobalObjectsManager.Logger.Info("UPDATE TRY FZS value: " + value);


                            ChangeRet = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.FZS, value,
                                service_vid, service_num);
                            
                        }

                    }
                    else
                    {

                        decimal decValue = Rash/Koeff;
                        GlobalObjectsManager.Logger.Info("UPDATE TRY Rash dec_value: " + decValue);
                        value = (int) decValue;
                        GlobalObjectsManager.Logger.Info("UPDATE TRY Rash value: " + value);

                        if (value != int.MinValue)
                        {
                            service_type = 2;
                            service_num = terminal.Cnt[i].Number;
                            ChangeRet = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Change, value,
                                service_type, service_vid, service_num);
                        }
                    }
                }
        }

        if (terminal.TotalPayment > 0)
            ChangeRet = GlobalObjectsManager.KVC_Exec(GlobalObjectsManager.enKVCMethod.Distribute, terminal.TotalPayment);

        return ChangeRet;
    }

    static int intParse(string value)
    {
        if (value == null)
            return 0;

        value = value.Replace(",", "");
        int iVal = 0;
        bool result = int.TryParse(value, out iVal);
        return iVal;
    }

    static decimal decimalParse(string value)
    {
        return decimal.Parse(value.Replace('.', ','));
    }

    public clsKvc(string init, bool ReadKoeff)
    {
        if (ReadKoeff)
            CntKoeffList.Clear();

        NameValueCollection m_Rek = Collection.GetNameValueCollection(init);
        PostAddress = m_Rek["PostAddress"] ?? "";
        MonthPay = m_Rek["MonthPay"];

        Mode = intParse(m_Rek["Mode"] ?? "1");

        TotalPayment = intParse(m_Rek["TotalPayment"] ?? "0");

        PayLen = intParse(m_Rek["PayLen"]);
        list_Pay = GetIndx(init, @";Pay[0-9]Vid=");
        PayLen = (list_Pay.Count > 0) ? list_Pay[list_Pay.Count - 1] + 1 : 0;


        CntLen = intParse(m_Rek["CntLen"]);
        list_Cnt = GetIndx(init, @";Cnt[0-9]Vid=");
        CntLen = (list_Cnt.Count > 0) ? list_Cnt[list_Cnt.Count - 1] + 1 : 0;



        string PrefixPay = "Pay";
        if (PayLen > 0)
        {
            for (int i = 0; i < PayLen; i++)
            {

                string Name = PrefixPay + i + "ServName";
                string Vid = PrefixPay + i + "Vid";
                string Sal = PrefixPay + i + "Sal";
                string Nac = PrefixPay + i + "Nac";
                string Sum = PrefixPay + i + "Sum";
                if (m_Rek[Vid] != null)
                {
                    clsPay pay = new clsPay(i, m_Rek[Name], intParse(m_Rek[Vid]), intParse(m_Rek[Sal]), intParse(m_Rek[Nac]), intParse(m_Rek[Sum]));
                    Pay.Add(pay);
                }
            }
        }

        string PrefixCnt = "Cnt";
        if (CntLen > 0)
        {
            for (int i = 0; i < CntLen; i++)
            {
                string Name = PrefixCnt + i + "ServName";
                string Vid = PrefixCnt + i + "Vid";
                string SubVid = PrefixCnt + i + "SubVid";
                string Prec = PrefixCnt + i + "Prec";
                string Rash = PrefixCnt + i + "Rash";
                string Sum = PrefixCnt + i + "Sum";
                string Number = PrefixCnt + i + "Number";
                string ExpCons = PrefixCnt + i + "ExpCons";
                string DisablePay = PrefixCnt + i + "DisablePay";
                string FZS = PrefixCnt + i + "FZS";

                string NormalTar = PrefixCnt + i + "Normal.Tar";
                string NormalRash = PrefixCnt + i + "Normal.Rash";
                string NormalSum = PrefixCnt + i + "Normal.Sum";
                string Value = PrefixCnt + i + "Value";
                string Koeff = PrefixCnt + i + "Koeff";
                if (ReadKoeff)
                {
                    GlobalObjectsManager.Logger.Info("ReadKoeff Tyr Cnt Name: " + Name);
                    if (m_Rek[Koeff] != null)
                    {
                        GlobalObjectsManager.Logger.Info("ReadKoeff " + ("clsCnt" + i) + " Add Koeff: " + m_Rek[Koeff]);
                        CntKoeffList.Add("clsCnt" + i, m_Rek[Koeff]);
                    }
                    else
                    {
                        GlobalObjectsManager.Logger.Info("ReadKoeff !!! ERROR Koeff for " + ("clsCnt" + i) + " not found");
                    }
                }

                //System.Diagnostics.Debug.WriteLine("Cnt Name:" + Name);
                if (m_Rek[Vid] != null)
                {
                    clsCnt cnt = new clsCnt(i,
                        m_Rek[Name], intParse(m_Rek[Vid]), intParse(m_Rek[SubVid]), intParse(m_Rek[Prec]), decimalParse(m_Rek[Rash]), intParse(m_Rek[Sum])
                        , intParse(m_Rek[Number])
                        , intParse(m_Rek[ExpCons])
                        , intParse(m_Rek[DisablePay])
                        , intParse(m_Rek[FZS])

                        , intParse(m_Rek[NormalTar]), intParse(m_Rek[NormalRash]), intParse(m_Rek[NormalSum])
                        , intParse(m_Rek[Value])
                        );
                    Cnt.Add(cnt);
                }
            }
        }
    }

}

public abstract class clsKvcFields
{
    abstract public string GetName();
}

/// <summary>
/// класс начислений
/// </summary>
public class clsPay : clsKvcFields
{
    public override string GetName()
    {
        return "clsPay" + i;
    }

    public readonly int i = 0;
    public readonly string Name = string.Empty;
    public readonly int Vid = 0;
    public readonly int Sal = 0;
    public readonly int Nac = 0;
    public readonly int Sum = 0;

    //;Pay0Vid=1;Pay0ServName=Сод.жилья;Pay0Sal=72138;Pay0Nac=72138;Pay0Sum=0;
    public clsPay(int _i, string name, int vid, int sal, int nac, int sum)
    {
        i = _i;
        Name = name; Vid = vid; Sal = sal; Nac = nac; Sum = sum;
    }
}

/// <summary>
/// класс счетчиков
/// </summary>
public class clsCnt : clsKvcFields
{
    //Cnt0Vid=3;Cnt0SubVid=3;Cnt0Number=0;Cnt0ServName=Эл.снаб. ;Cnt0Prec=5;Cnt0Rash=0;Cnt0Sum=0;
    //Cnt0Normal.Tar=273;Cnt0Normal.Rash=0;Cnt0Normal.Sum=0;Cnt0Value=00298;Cnt0PUoM=кВт-ч;Cnt0Koeff=1;

    public override string GetName()
    {
        return "clsCnt" + i;
    }

    public readonly int i = 0;
    public readonly string Name = string.Empty;
    public readonly int Vid = 0;
    public readonly int SubVid = 0;
    public readonly int Prec = 0;
    public readonly decimal Rash = 0;
    public readonly int Sum = 0;
    public readonly int Number = 0;
    public readonly int ExpCons = 0;
    public readonly int DisablePay = 0;
    public readonly int FZS = 0;
    
    public readonly int NormalTar = 0;
    public readonly int NormalRash = 0;
    public readonly int NormalSum = 0;
    public readonly int Value = 0;

    public clsCnt(int _i, string name, int vid, int subvid, int prec, decimal rash, int sum, int number, int expCons, int disablePay, int fzs, int normaltar, int normalrash, int normalsum, int value)
    {
        i = _i;
        Name = name; Vid = vid; SubVid = subvid; Prec = prec; Rash = rash; Sum = sum; Number = number;
        ExpCons = expCons;
        DisablePay = disablePay;
        FZS = fzs;
        NormalTar = normaltar; NormalRash = normalrash; NormalSum = normalsum; Value = value;
    }

}
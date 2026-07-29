using DispatcherCommon;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.ServiceModel;
using System.Text;

// NOTE: You can use the "Rename" command on the "Refactor" menu to change the class name "Monitoring" in code, svc and config file together.
public class Monitoring : IMonitoring
{
    public bool Authorize(string postboxNumber, string login, string password, out SharedUserInfo uInfo)
    {
        GlobalObjectsManager.Logger.Info("Authorize " + "postboxNumber:" + postboxNumber + " login " + login + " password " + password);

        uInfo = new SharedUserInfo { Name = "Иван", Patronymic = "Иванович", Role = 1, Surname= "Иванов", UserId = "1" };
        return true;
    }

    public void CheckOverdueParcels(string postboxNumber, out List<SharedMailingInfo> overdueParcels)
    {
        GlobalObjectsManager.Logger.Info("CheckOverdueParcels " + "postboxNumber:" + postboxNumber);
        overdueParcels = new List<SharedMailingInfo>()
        {
            new SharedMailingInfo { BoxNumber = "1", Number="1" }
        };
        throw new NotImplementedException();
    }

    public void Collection(string postboxNumber, Guid operationId, SharedCollectionInfo cInfo)
    {
        GlobalObjectsManager.Logger.Info("Collection " + "postboxNumber:" + postboxNumber + " operationId " + operationId + "cInfo " + XmlService.Serialize(cInfo));
    }

    public void ParcelCodeScanned(string postboxNumber, string parcelNumber, out string boxNumber, out string code, out decimal tariff, out string phoneNumber)
    {
        GlobalObjectsManager.Logger.Info("ParcelCodeScanned " + "postboxNumber:" + postboxNumber + " parcelNumber " + parcelNumber);
        boxNumber = "1";
        code = "1";
        tariff = 100;
        phoneNumber = "88008885566";
    }

    public void ParcelDelivered(string postboxNumber, Guid operationId, string boxNumber)
    {
        GlobalObjectsManager.Logger.Info("ParcelDelivered " + "postboxNumber:" + postboxNumber + " operationId " + operationId + " boxNumber " + boxNumber);
    }

    public void ParcelToBox(string postboxNumber, Guid operationId, string boxNumber, string parcelNumber)
    {
        GlobalObjectsManager.Logger.Info("ParcelToBox " + "postboxNumber:" + postboxNumber + " operationId " + operationId + " boxNumber " + boxNumber + " parcelNumber " + parcelNumber);
    }

    public void Payment(string postboxNumber, Guid operationId, string boxNumber, SharedPaymentInfo pInfo, out SharedRapidaResult res)
    {
        res = new SharedRapidaResult { BillRegId = "1", Description="", ErrCode="0", IsError=false, OperatorName = "payment", PaymDate= DateTime.Now.ToString(), PaymExtId = "0010_33455_6546464", PaymNumb= "1111", TermId="1" };
    }

    public void PostboxOnline(string postboxNumber, List<SharedPostboxProblemInfo> problems)
    {
        GlobalObjectsManager.Logger.Info("PostboxOnline " + "postboxNumber:" + postboxNumber + " problems.Count " + (problems==null? 0:problems.Count));
        if(problems != null && problems.Count>0)
            foreach(var p in problems)
                GlobalObjectsManager.Logger.Info("SharedPostboxProblemInfo: " + XmlService.Serialize(p));

    }

    public void ShiftClose(string postboxNumber, ulong shiftNum, string userId, DateTime dateClosed)
    {
        GlobalObjectsManager.Logger.Info("ShiftClose " + "postboxNumber:" + postboxNumber + " shiftNum " + shiftNum + " userId" + userId+ " dateClosed " + dateClosed);
    }

    public void ShiftOpen(string postboxNumber, ulong shiftNum, string frNumber, int frShiftNum, string userId, DateTime dateOpened)
    {
        GlobalObjectsManager.Logger.Info("ShiftOpen " + "postboxNumber:" + postboxNumber + " shiftNum " + shiftNum + " userId" + userId + " frNumber " + frNumber + " frShiftNum " + frShiftNum + " dateOpened " + dateOpened);
    }
}

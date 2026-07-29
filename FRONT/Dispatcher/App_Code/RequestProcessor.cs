using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Text;
using System.Xml;
using DispatcherCommon;
using Platerra.Terminal.Common.ESV.Classes.Exchange;

/// <summary>
/// Сводное описание для RequestProcessor
/// </summary>
public class RequestProcessor : IXmlRequestProcessor
{

    private static decimal GetKillBillSum(tb_Bills bill)
    {
        decimal killBillSum = 0;
        if (DateTime.Now < bill.KillDateTime)
        {
            var f = bill.tb_BuyoutRequest.FirstOrDefault(p => p.StatusId == 300);
            if (f != null) killBillSum = f.BuyoutSumm;
        }
        else
        {
            killBillSum = (bill.tb_BillProfits.Any(p => p.ProfitDateTime > DateTime.Now)
                ? bill.Nominal - (bill.Nominal / 100 * bill.tb_Emissions.AheadOfTimePercent)
                : bill.Nominal);
        }
        return killBillSum;
    }

    private static Bill GetBill(tb_Bills bill)
    {
        return new Bill
        {
            Id = bill.Id
            ,
            Number = bill.Number
            ,
            EmissionId = bill.EmissionId
            ,
            EmissionerInfo = bill.tb_Emissions.tb_Emissioners.tb_Clients.tb_Organizations.Name
            ,
            Nominal = bill.Nominal
            ,
            MinBillSum = bill.tb_Emissions.MinBillSum
            ,
            MaxBillSum = bill.tb_Emissions.MaxBillSum
            ,
            BillPeriodInMonths = bill.tb_Emissions.BillPeriodInMonths
            ,
            KillDateTime = bill.KillDateTime
            ,
            ActivateDateTime = bill.ActivateDateTime
            ,
            KillBillSum = GetKillBillSum(bill)
            ,
            StatusId = bill.StatusId
            ,
            DateTimeToPay = bill.tb_BillProfits.Any() ?
                bill.tb_BillProfits.OrderBy(x => Math.Abs((x.ProfitDateTime - DateTime.Now).Ticks))
                    .First()
                    .ProfitDateTime : DateTime.MinValue
            ,
            ProfitsSum = bill.tb_BillProfits.Where(p => p.ProfitDateTime < DateTime.Now).Sum(s => s.Sum)
            ,
            Payments =
                bill.tb_Payments.Select(p => new Payment { Sum = p.Sum, PaymentDateTime = p.DateTime }).ToArray()
            ,
            BillProfits =
                bill.tb_BillProfits.Select(p => new BillProfit { ProfitDateTime = p.ProfitDateTime, Sum = p.Sum }).ToArray()
        };
    }

    private static string RandomString(int size)
    {
        var random = new Random((int)DateTime.Now.Ticks);
        const string input = "0123456789";
        var builder = new StringBuilder();
        for (var i = 0; i < size; i++)
        {
            var ch = input[random.Next(0, input.Length)];
            builder.Append(ch);
        }
        return builder.ToString();
    }

    public string DoRequest(string service, string request)
    {
        bool needSave = false;
        service = service.ToLower();
        GlobalObjectsManager.Logger.Info("request:" + request);
        var doc = new XmlDocument();
        doc.LoadXml(request);
        var result = new object();
        using (var db = new EBSEntities())
        {
            if (service == typeof(EmissionInfo).Name.ToLower())
            {
                var t = XmlService.DeSerialize<EmissionInfo.EmissionInfoRequest>(request);
                var resp = new EmissionInfo.EmissionInfoResponse();
                var emission = db.tb_Emissions.FirstOrDefault(p => p.Id == t.EmissionId);
                if (emission != null)
                {
                    var org = emission.tb_Emissioners.tb_Clients.tb_Organizations;
                    var pc = db.ts_PropertyCategories.FirstOrDefault(p => p.Id == org.PropertyCategoryId);
                    //var name = string.Empty;
                    //if (pc != null)
                    //    name = pc.Name;
                    // два раза ООО ООО
                    //resp.EmissionerName = name + " " + org.Name;
                    resp.EmissionerName = org.Name;
                    resp.EmissionerINN = org.INN;
                    resp.EmissionerAddress = org.JurAddress;
                    resp.EmissionerDescription = org.Description;
                    resp.DirectorLastName = org.DirectorLastName;
                    resp.DirectorFirstName = org.DirectorFirstName;
                    resp.DirectorSurName = org.DirectorSurName;
                    resp.DigitalSign = emission.tb_Emissioners.DigitalSign;
                    using (var fs = new FileStoreEntities())
                    {
                        var f = fs.tblImages.FirstOrDefault(p => p.id == emission.Image);
                        if (f != null)
                        {
                            resp.EmissionImage = f.attachment;
                        }
                    }
                    resp.MaxBillSum = emission.MaxBillSum;
                    resp.MinBillSum = emission.MinBillSum;
                    resp.SaledBillsCount = 0;
                    resp.SaledBillsSum = 0;
                    resp.BillPeriodInMonths = emission.BillPeriodInMonths;
                    resp.EmissionYield = emission.tb_EmissionYield.Select(p => new EmissionYield { MonthNumber = p.MonthNumber, Percent = p.Percent }).ToArray();
                    resp.BillAllowances = emission.tb_BillAllowances.Select(p => new BillAllowances { MinBillSum = p.MinBillSum, Percent = p.Percent }).ToArray();
                    resp.EmissionDescription = emission.Comment;
                }
                else
                {
                    resp.ResultCode = 1;
                    resp.ResultDescription = "Не найден.";
                }
                result = resp;
            }
            if (service == typeof(ProfitBill).Name.ToLower())
            {
                var t = XmlService.DeSerialize<ProfitBill.ProfitBillRequest>(request);
                db.tb_Payments.Add(new tb_Payments() { BillId = t.BillId, Sum = t.Sum, PaymentTypeId = t.PaymentTypeId, DateTime = t.ProfitDateTime });
                var f = db.tb_Bills.FirstOrDefault(p => p.Id == t.BillId);
                if (f != null)
                {
                    GlobalObjectsManager.Logger.Info("ProfitBill OK t.BillId " + t.BillId);
                    switch (t.PaymentTypeId)
                    {
                        case 1:
                            f.Number += 1;
                            break;
                        case 2:
                            f.StatusId = 240;
                            f.StatusDateTime = DateTime.Now;
                            break;
                    }
                    GlobalObjectsManager.Logger.Info("ProfitBill OK f.Number " + f.Number);
                }
                else
                {
                    GlobalObjectsManager.Logger.Info("ProfitBill не найден t.BillId " + t.BillId);
                }
                needSave = true;
                result = new ProfitBill.ProfitBillResponse();
            }
            if (service == typeof(AuthBillInfo).Name.ToLower())
            {
                var t = XmlService.DeSerialize<AuthBillInfo.AuthBillInfoRequest>(request);
                var bill = db.tb_Bills.FirstOrDefault(p => p.Code == t.BillCode);
                if (bill == null)
                    result = new AuthBillInfo.AuthBillInfoResponse()
                    {
                        ResultCode = 1,
                        ResultDescription = "tb_Bills not found"
                    };
                else
                {
                    var i = db.tb_Investors.FirstOrDefault(p => p.Id == bill.tb_Investors.Id);
                    if (i == null)
                        result = new AuthBillInfo.AuthBillInfoResponse()
                        {
                            ResultCode = 1,
                            ResultDescription = "tb_Investors not found"
                        };
                    else
                    {
                        var user = i.tb_Clients.tb_Users.FirstOrDefault();
                        if (user == null)
                            result = new AuthBillInfo.AuthBillInfoResponse()
                            {
                                ResultCode = 1,
                                ResultDescription = "tb_Users not found"
                            };
                        else
                            result = new AuthBillInfo.AuthBillInfoResponse
                            {
                                Id = i.Id
                                ,
                                LastName = user.LastName
                                ,
                                FirstName = user.FirstName
                                ,
                                SurName = user.SurName
                                ,
                                Phone = user.Phone
                                ,
                                EMail = user.EMail
                                ,
                                Address = user.RegistrationAddress
                                ,
                                PassportPrefix = user.PassportPrefix
                                ,
                                PassportNumber = user.PassportNumber
                                ,
                                StatusId = user.StatusId
                                ,
                                Bill = GetBill(bill),
                            };
                    }
                }
            }
            if (service == typeof(CloseShift).Name.ToLower())
            {
                var t = XmlService.DeSerialize<CloseShift.CloseShiftRequest>(request);
                var shift = db.tb_Shifts.FirstOrDefault(p => p.Id == t.ShiftId);
                if (shift == null)
                {
                    result = new CloseShift.CloseShiftResponse() { ResultCode = 1, ResultDescription = "tb_Shifts not found" };
                }
                else
                {
                    shift.CloseDateTime = DateTime.Now;
                    shift.InkassExtId = t.InkassExtId;
                    needSave = true;
                    result = new CloseShift.CloseShiftResponse();
                }
            }
            if (service == typeof(OpenShift).Name.ToLower())
            {
                var t = XmlService.DeSerialize<OpenShift.OpenShiftRequest>(request);

                var user = db.tb_Users.FirstOrDefault(p => p.TokenKey == t.TokenKey);
                var openShift = new OpenShift.OpenShiftResponse
                {
                    IsUserExists = user != null,
                };

                if (user != null)
                {
                    openShift.UserId = user.Id;
                    openShift.UserStatusId = user.StatusId;
                    var kassir = user.tb_UsersRoles.Any(p => p.RoleId == 2);

                    //Активен
                    if (kassir && user.StatusId == 10)
                    {
                        var alreadyOpened = db.tb_Shifts.FirstOrDefault(p => p.KioskId == t.KioskId && p.CloseDateTime == null);
                        var shift = new tb_Shifts
                        {
                            OpenDateTime = DateTime.Now,
                            KioskId = t.KioskId,
                            UserId = user.Id,
                            InkassExtId = string.Empty
                        };

                        if (alreadyOpened == null)
                        {
                            db.tb_Shifts.Add(shift);
                            db.SaveChanges();
                        }
                        else
                        {
                            shift = alreadyOpened;
                        }
                        openShift.FirstName = user.FirstName;
                        openShift.LastName = user.LastName;
                        openShift.Password = user.Password;
                        openShift.SurName = user.SurName;
                        openShift.ShiftId = shift.Id;
                        openShift.OpenDateTime = shift.OpenDateTime;
                        openShift.InkassExtId = shift.InkassExtId;
                    }
                }
                result = openShift;
            }

            if (service == typeof(MainPageEmissions).Name.ToLower())
            {
                var t = XmlService.DeSerialize<MainPageEmissions.MainPageEmissionsRequest>(request);
                result = new MainPageEmissions.MainPageEmissionsResponse()
                {
                    EmissionIds = db.tb_EBS_KioskEmissions.Where(p => p.KioskId == t.KioskId).Select(p => p.EmissionId).ToArray()
                };
            }
            if (service == typeof(CheckSMSCode).Name.ToLower())
            {
                var t = XmlService.DeSerialize<CheckSMSCode.CheckSMSCodeRequest>(request);

                var f = db.tb_Users.FirstOrDefault(p => p.Phone == t.PhoneNumber);
                if (f != null || t.SendSMSIfUserNotExists)
                {
                    result = new CheckSMSCode.CheckSMSCodeResponse()
                    {
                        IsNewUser = f == null,
                        ClientTypeId = (f == null ? 0 : (f.tb_Clients != null ? f.tb_Clients.ClientTypeId : 0)),
                        UserStatusId = (f == null ? 0 : f.StatusId)
                    };

                    var smsReq = EtranConfigurationManager.EtranConfig + "?function=sms&number=" + t.PhoneNumber + "&msg=" + t.Code;
                    GlobalObjectsManager.Logger.Info("sms smsReq " + smsReq);

                    var client = new WebClient();
                    var smsResult = client.DownloadString(smsReq);
                    GlobalObjectsManager.Logger.Info("sms result " + smsResult);
                }
                else
                {
                    result = new CheckSMSCode.CheckSMSCodeResponse()
                    {
                        IsNewUser = true,
                        ResultCode = 0,
                        ResultDescription = "Не найден."
                    };
                }
            }
            if (service == typeof(RegistrateInvestor).Name.ToLower())
            {
                var investorRequest = XmlService.DeSerialize<RegistrateInvestor.RegistrateInvestorRequest>(request);
                if (db.tb_Users.Any(p => p.Phone == investorRequest.Phone))
                {
                    result = new RegistrateInvestor.RegistrateInvestorResponse()
                    {
                        ResultCode = 2
                        ,
                        ResultDescription = "Пользователь уже зарегистрирован."
                    };
                }
                else
                {
                    var client = new tb_Clients
                    {
                        ClientTypeId = 3
                        ,
                        IsJuridical = false
                        ,
                        StatusId = 60
                        ,
                        StatusComment = string.Empty
                        ,
                        Description = string.Empty
                        ,
                        StatusDateTime = DateTime.Now
                        ,
                        StatusUserId = 0
                    };

                    db.tb_Clients.Add(client);
                    db.SaveChanges();
                    db.tb_Investors.Add(new tb_Investors { Id = client.Id });

                    var user = new tb_Users()
                    {
                        Avatar = null
                        ,
                        ClientId = client.Id
                        ,
                        Description = string.Empty
                        ,
                        EMail = investorRequest.EMail
                        ,
                        FirstName = investorRequest.FirstName
                        ,
                        IsDirector = false
                        ,
                        LastName = investorRequest.LastName
                        ,
                        Login = string.Empty
                        ,
                        Password = string.Empty
                        ,
                        Phone = investorRequest.Phone
                        ,
                        NickName = string.Empty
                        ,
                        PassportIssueCode = string.Empty
                        ,
                        PassportIssueDate = null
                        ,
                        PassportIssueInfo = string.Empty
                        ,
                        PassportNumber = investorRequest.PassportNumber
                        ,
                        PassportPrefix = investorRequest.PassportPrefix
                        ,
                        RegistrationAddress = investorRequest.Address
                        ,
                        StatusComment = string.Empty
                        ,
                        StatusDateTime = DateTime.Now
                        ,
                        StatusId = 10
                        ,
                        StatusUserId = 0
                        ,
                        SurName = investorRequest.SurName
                        ,
                        WarrantyDate = null
                        ,
                        WarrantyNumber = string.Empty
                        ,
                        TokenKey = string.Empty
                    };

                    db.tb_Users.Add(user);
                    needSave = true;
                    result = new RegistrateInvestor.RegistrateInvestorResponse();
                }
            }

            if (service == typeof(RegistrateBill).Name.ToLower())
            {
                var registrateBillRequest = XmlService.DeSerialize<RegistrateBill.RegistrateBillRequest>(request);
                var user = db.tb_Users.FirstOrDefault(p => p.Phone == registrateBillRequest.Phone);
                if (user == null)
                    result = new RegistrateBill.RegistrateBillResponse()
                    {
                        ResultCode = 1,
                        ResultDescription = "Пользователь с phone " + registrateBillRequest.Phone + " не найден."
                    };
                else
                {
                    var client = user.tb_Clients;
                    var newBill = new tb_Bills
                    {
                        DocumentAttachment = null
                            ,
                        EmissionId = registrateBillRequest.EmissionId
                            ,
                        Nominal = registrateBillRequest.Nominal
                            ,
                        StatusId = registrateBillRequest.StatusId
                            ,
                        InvestorId = client.tb_Investors.Id
                            ,
                        LocationId = 1
                            ,
                        Number = 1
                            ,
                        ParticipationInAction = false
                            ,
                        StatusComment = string.Empty
                            ,
                        ReferrerId = null
                            ,
                        StatusDateTime = DateTime.Now
                            ,
                        StatusUserId = 0
                            ,
                        Code = string.Empty
                    };
                    db.tb_Bills.Add(newBill);
                    db.SaveChanges();
                    result = new RegistrateBill.RegistrateBillResponse() { BillId = newBill.Id };
                }
            }
            if (service == typeof(PrintBill).Name.ToLower())
            {
                var printBillRequest = XmlService.DeSerialize<PrintBill.PrintBillRequest>(request);
                var bill = db.tb_Bills.FirstOrDefault(p => p.Id == printBillRequest.BillId);
                if (bill == null)
                    result = new PrintBill.PrintBillResponse()
                    {
                        ResultCode = 1,
                        ResultDescription = "Не найден."
                    };
                else
                {
                    bill.StatusId = 220;
                    bill.StatusDateTime = printBillRequest.PrintDateTime;
                    bill.Number += 1;
                    //bill.Code = RandomString(10);
                    var genBarCode = bill.Id.ToString(CultureInfo.InvariantCulture);
                    while (genBarCode.Length < 6)
                        genBarCode = "0" + genBarCode;
                    bill.Code = genBarCode;

                    needSave = true;
                    result = new PrintBill.PrintBillResponse()
                    {
                        Number = bill.Number,
                        Code = bill.Code
                    };
                }
            }
            if (service == typeof(PayBill).Name.ToLower())
            {
                var payBillRequest = XmlService.DeSerialize<PayBill.PayBillRequest>(request);
                var bill = db.tb_Bills.FirstOrDefault(p => p.Id == payBillRequest.BillId);
                if (bill == null)
                    result = new PayBill.PayBillResponse()
                    {
                        ResultCode = 1,
                        ResultDescription = "Не найден."
                    };
                else
                {
                    bill.Nominal = payBillRequest.Nominal;
                    var listProfits = new List<tb_BillProfits>();
                    var billAllowances = bill.tb_Emissions.tb_BillAllowances.OrderBy(c => c.Percent).ToList();
                    var emissionYelds = bill.tb_Emissions.tb_EmissionYield;
                    byte billAllowancePercent = 0;

                    if (billAllowances.Count > 0)
                    {
                        for (int k = 0; k < billAllowances.Count; k++)
                        {
                            tb_BillAllowances ba = billAllowances[k];
                            if (bill.Nominal >= ba.MinBillSum)
                            {
                                billAllowancePercent = ba.Percent;
                                GlobalObjectsManager.Logger.Info("ba.MinSumm " + ba.MinBillSum);
                                break;
                            }
                        }
                    }

                    GlobalObjectsManager.Logger.Info("bill.Nominal " + bill.Nominal);
                    GlobalObjectsManager.Logger.Info("billAllowancePercent " + billAllowancePercent);

                    for (int i = 1; i <= bill.tb_Emissions.BillPeriodInMonths; i++)
                    {
                        var emYield = emissionYelds.FirstOrDefault(y => y.MonthNumber == i);
                        if (emYield == null)
                            continue;
                        var yieldPercent = emYield.Percent;

                        GlobalObjectsManager.Logger.Info("yieldPeriod " + i);
                        GlobalObjectsManager.Logger.Info("yieldPercent " + yieldPercent);

                        var sum = bill.Nominal / 100 * yieldPercent;
                        if (billAllowancePercent > 0)
                            sum *= 1 + (decimal)billAllowancePercent / 100;

                        GlobalObjectsManager.Logger.Info("sum " + sum);

                        listProfits.Add(new tb_BillProfits
                        {
                            BillId = bill.Id
                            ,
                            ProfitDateTime = payBillRequest.PayDateTime.AddMonths(bill.tb_Emissions.tb_EmissionYield.First(y => y.MonthNumber == i).MonthNumber)
                            ,
                            Sum = sum
                        });
                    }

                    db.tb_BillProfits.AddRange(listProfits);
                    bill.StatusId = 210;
                    bill.StatusDateTime = payBillRequest.PayDateTime;
                    bill.ActivateDateTime = payBillRequest.PayDateTime;
                    bill.KillDateTime = payBillRequest.PayDateTime.AddMonths(bill.tb_Emissions.BillPeriodInMonths);
                    needSave = true;
                    result = new PayBill.PayBillResponse();
                }
            }

            if (service == typeof(InvestorInfo).Name.ToLower())
            {
                var investorInfoRequest = XmlService.DeSerialize<InvestorInfo.InvestorInfoRequest>(request);
                var user = db.tb_Users.FirstOrDefault(p => p.Phone == investorInfoRequest.Phone);
                if (user == null)
                    result = new InvestorInfo.InvestorInfoResponse()
                    {
                        ResultCode = 1
                        ,
                        ResultDescription = "Пользователь с phone " + investorInfoRequest.Phone + " не найден."
                    };
                else
                {
                    var billList = user.tb_Clients.tb_Investors.tb_Bills.Select(GetBill).ToList();
                    result = new InvestorInfo.InvestorInfoResponse()
                    {
                        LastName = user.LastName
                        ,
                        FirstName = user.FirstName
                        ,
                        SurName = user.SurName
                        ,
                        EMail = user.EMail
                        ,
                        Address = user.RegistrationAddress
                        ,
                        PassportPrefix = user.PassportPrefix
                        ,
                        PassportNumber = user.PassportNumber
                        ,
                        NearestActionDate = billList.Count > 0 ? billList.OrderBy(x => Math.Abs((x.DateTimeToPay - DateTime.Now).Ticks)).First().DateTimeToPay : DateTime.MinValue
                        ,
                        Bills = billList.Count > 0 ? billList.ToArray() : new Bill[0],
                    };
                }
            }

            if (needSave)
                db.SaveChanges();
        }
        return XmlService.Serialize(result);
    }
}
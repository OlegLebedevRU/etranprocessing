library DLL;

{ Important note about DLL memory management: ShareMem must be the
  first unit in your library's USES clause AND your project's (select
  Project-View Source) USES clause if your DLL exports any procedures or
  functions that pass strings as parameters or function results. This
  applies to all strings passed to and from your DLL--even those that
  are nested in records and classes. ShareMem is the interface unit to
  the BORLNDMM.DLL shared memory manager, which must be deployed along
  with your DLL. To avoid using BORLNDMM.DLL, pass string information
  using PChar or ShortString parameters. }

uses
  SysUtils,
  Classes,
  Variants,
  Types_Kvc in 'Types_Kvc.pas',
  DllImport in 'Dllimport.pas';



{$R *.res}


var
Info: PInfo;
InfoExp: string;
  LicNum1  : Integer;
  LicNum2  : Integer;


function Free(): Integer; stdcall;
begin
    Result := FreeAccount(Info);
    Info := nil;
end;


function InitBase(BAZA: PChar; SOG: PChar):Integer; stdcall;
begin
//  Info := nil;
Result:= InitLib (BAZA, SOG);

//SetParameters (7, 211);
//SetParameters (BankCode, OperCode);
//SetRegion (Region);

//  e_street.Text := '219';
//  e_house.Text := '33';
//  e_corp.Text :='00';
//  e_place.Text :='38';
//  e_komnata.Text := '0';
//  e_contr.Text :='1';

end;

function InitRegion(Region: integer):Integer; stdcall;
begin
Result:= SetRegion (Region);
end;

function InitTerminal(BankCode: Integer; OperCode: Integer):Integer; stdcall;
begin
Result:= SetParameters (BankCode, OperCode);
end;


procedure InfoAdd(name: string; obj: Variant);
begin
InfoExp := InfoExp + ';' +name +'=' + VarToStr(obj)
//tm_check.Lines.Add(name +'=' + VarToStr(obj));
//tm_check.Lines.Add('fdsfsfsd');
end;

procedure PrintInfo(Info: PInfo);
var
  i        : integer;
  ServName : string;
  PUoM     : PChar;
  Koeff    : Double;
  Value    : PChar;
begin

InfoExp :='';


InfoAdd('PostAddress',string(Info.PostAddress));
//InfoAdd('Addr1',string(Info.Adr1));
//InfoAdd('Addr2',string(Info.Adr2));

InfoAdd('MonthPay',Info.MonthPay);
   InfoAdd('PayLen', Info.PayLen);

 for i:=0 to Info.PayLen-1 do
  begin
   GetParamCnt(Info.Pay[i].Vid, Koeff, PUoM);
   ServName := GetServiceName(Info, Info.Pay[i].Vid);

   InfoAdd('Pay'+IntToStr(i)+'Vid', Info.Pay[i].Vid);
   InfoAdd('Pay'+IntToStr(i)+'ServName', ServName);
   InfoAdd('Pay'+IntToStr(i)+'Sal', Info.Pay[i].Sal);
   InfoAdd('Pay'+IntToStr(i)+'Nac', Info.Pay[i].Nac);
   InfoAdd('Pay'+IntToStr(i)+'Sum', Info.Pay[i].Sum);

  end;

InfoAdd('CntLen', Info.CntLen);

 for i:=0 to Info.CntLen-1 do
  begin
   GetParamCnt(Info.Cnt[i].Vid, Koeff, PUoM);
   ServName := GetServiceName(Info, Info.Cnt[i].Vid);
    if Info.Cnt[i].Vid <> Info.Cnt[i].SubVid
     then ServName := Format('%s(%s)', [ServName, GetServiceName(Info, Info.Cnt[i].SubVid)]);
   ServName := ServName + ' ' + GetCounterName(Info, Info.Cnt[i].Vid, Info.Cnt[i].Number);

   GetValueCntNum(Info, Info.Cnt[i].Vid, Info.Cnt[i].Number, Value);

   InfoAdd('Cnt'+IntToStr(i)+'Vid', Info.Cnt[i].Vid);
   InfoAdd('Cnt'+IntToStr(i)+'SubVid', Info.Cnt[i].SubVid);
   InfoAdd('Cnt'+IntToStr(i)+'Number', Info.Cnt[i].Number);
   InfoAdd('Cnt'+IntToStr(i)+'ExpCons', Info.Cnt[i].ExpCons);
   InfoAdd('Cnt'+IntToStr(i)+'DisablePay', Info.Cnt[i].DisablePay);
   InfoAdd('Cnt'+IntToStr(i)+'FZS', Info.Cnt[i].FZS);
   InfoAdd('Cnt'+IntToStr(i)+'ServName', ServName);
   InfoAdd('Cnt'+IntToStr(i)+'Prec', Info.Cnt[i].Prec);
   InfoAdd('Cnt'+IntToStr(i)+'Rash', Info.Cnt[i].Rash*Koeff);
   InfoAdd('Cnt'+IntToStr(i)+'Sum', Info.Cnt[i].Sum);
InfoAdd('Cnt'+IntToStr(i)+'Normal.Tar', Info.Cnt[i].Normal.Tar);
InfoAdd('Cnt'+IntToStr(i)+'Normal.Rash', Info.Cnt[i].Normal.Rash);
InfoAdd('Cnt'+IntToStr(i)+'Normal.Sum', Info.Cnt[i].Normal.Sum);
InfoAdd('Cnt'+IntToStr(i)+'Value', string(Info.Cnt[i].Value));
InfoAdd('Cnt'+IntToStr(i)+'PUoM', string(PUoM));
InfoAdd('Cnt'+IntToStr(i)+'Koeff', Koeff);



  end;

 InfoAdd('TotalPayment', GetTotalPayment(Info));

end;


function SetMode(mode: Integer;out RetCode:integer): PChar;   stdcall;
begin
  RetCode:=0;
// mode  BY_DOLG = 1; BY_NACH = 2; BY_DOLG OR BY_NACH = 3

  RetCode:=FillCharges(Info, mode);

  PrintInfo(Info);

  Result := PChar(InfoExp);

end;


//¬ведена функци€ дл€ обработки указанного клиентом потреблени€ за текущий мес€ц (без оплаты). „ерез неЄ надо обрабатывать введенное клиентом значение потреблени€ Ц ExpCons (функци€ SetExpectedConsumptionNum, аргумент Rashod). ƒл€ ExpCons=<пусто> функцию вызывать не надо.
function ExpCons(value, service_vid, service_num: Integer;out RetCode: integer): PChar;   stdcall;
begin
  RetCode:=0;
  RetCode:=  SetExpectedConsumptionNum(Info, service_vid, service_num, value);
  PrintInfo(Info);
  Result := PChar(InfoExp);
end;

//¬ведена функци€ дл€ обработки указанного клиентом показани€ счетчика на текущий момент Ц FZS (без оплаты). ƒл€ FZS=<пусто> функци€ не вызываетс€. ¬ функции SetFactValueNum дл€ аргумента SetRashod надо использовать значение False:
function FZS(value, service_vid, service_num: Integer;out RetCode: integer): PChar;   stdcall;
begin
  RetCode:=0;
  RetCode:=  SetFactValueNum(Info, service_vid, service_num, value, false);
  PrintInfo(Info);
  Result := PChar(InfoExp);
end;


function Change(value, service_type, service_vid, service_num: Integer;out RetCode: integer): PChar;   stdcall;
begin
  RetCode:=0;
// value - дл€ начислений это сумма, дл€ счетчиков - значение
// service_type -  1 - начисление 2 - счетчики
// service_num - дл€ начислений не используетс€

  if service_type = 1  then
  RetCode:= SetCharge (Info, service_vid, value);
  if service_type = 2 then
  RetCode:=  SetConsumptionNum (Info, service_vid, service_num, value);

  PrintInfo(Info);
  Result := PChar(InfoExp);
end;


function Distribute(amount: Integer;out RetCode:Integer): PChar;   stdcall;
begin

  RetCode:=0;

  RetCode:=DistributeMoney(Info, amount);

  PrintInfo(Info);

  Result := PChar(InfoExp);

end;


function GetInfo(e_street, e_house, e_corp, e_place, e_komnata, e_contr: PChar;out RetCode:Integer): PChar;   stdcall;
var
 i : integer;
 ServName : string;
begin
  RetCode:=0;
//  Result := PChar('PChar(InfoExp)');

FreeAccount(Info);

 LicNum1 := StrToIntDef(e_street, 0) * 100000
          + StrToIntDef(e_house, 0) * 100
          + StrToIntDef(e_corp, 0);

 LicNum2 := StrToIntDef(e_place, 0) * 1000
          + StrToIntDef(e_komnata, 0) * 100
          + StrToIntDef(e_contr, 0);

RetCode:=GetAccount(LicNum1, LicNum2, Info);
 if RetCode = 0
  then
   begin

    PrintInfo(Info);

//    if Info.PayLen > 0
//     then
//      begin
//       for i:=0 to Info.PayLen-1 do
//        begin
//         ServName := GetServiceName(Info, Info.Pay[i].Vid);
//         cb_2.Items.Add(Format('%.3d  %-12s', [Info.Pay[i].Vid, ServName]));
//         //InfoAdd('TotalPayment', GetTotalPayment(Info));
//        end;
//      end;
//
//    if Info.CntLen > 0
//     then
//      begin
//       for i:=0 to Info.CntLen-1 do
//        begin
//         ServName := GetServiceName(Info, Info.Cnt[i].Vid);
//         if Info.Cnt[i].Vid <> Info.Cnt[i].SubVid
//          then ServName := Format('%s(%s)', [ServName, GetServiceName(Info, Info.Cnt[i].SubVid)]);
//         ServName := ServName + ' ' + GetCounterName(Info, Info.Cnt[i].Vid, Info.Cnt[i].Number);
//         cb_1.Items.Add(Format('%.3d%.3d  %-12s', [Info.Cnt[i].Vid, Info.Cnt[i].Number, ServName]));
//        end;
//      end;
 end;

  Result := PChar(InfoExp);
end;

function Payment(): Integer;   stdcall;
begin
  Result := WritePayment(Info);
end;

exports
InitBase
,InitRegion
,InitTerminal
,Free
,GetInfo
,SetMode
,ExpCons
,FZS
,Change
,Distribute
,Payment;

begin
end.

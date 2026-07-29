unit k_pay;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, StdCtrls, Dllimport, Types_kvc, ComCtrls, Buttons, Grids,
  ValEdit, StrUtils, RpDefine, RpRender, RpRenderCanvas, RpRenderPrinter,
  RpRenderPreview;

type
  Tf_LicNum = class(TForm)
    e_street: TEdit;
    e_house: TEdit;
    e_corp: TEdit;
    e_place: TEdit;
    e_komnata: TEdit;
    e_contr: TEdit;
    b_GetInfo: TButton;
    l_AddrByLicNum: TLabel;
    b_Pay: TButton;
    Label1: TLabel;
    Label2: TLabel;
    Label3: TLabel;
    Label4: TLabel;
    Label5: TLabel;
    Label6: TLabel;
    mm: TMemo;
    StatusBar1: TStatusBar;
    b_GetAddrInfo: TBitBtn;
    BitBtn1: TBitBtn;
    e_Amount: TEdit;
    Button1: TButton;
    b_SetCharge: TButton;
    Edit2: TEdit;
    Button4: TButton;
    mm2: TMemo;
    Label7: TLabel;
    b_SetConsumptionNum: TButton;
    b_SetChargeCntNum: TButton;
    b_GetChargeCntNum: TButton;
    cb_1: TComboBox;
    Button3: TButton;
    cb_2: TComboBox;
    Label8: TLabel;
    Label9: TLabel;
    Label10: TLabel;
    Label11: TLabel;
    Label12: TLabel;
    edt1: TEdit;
    procedure PrintInfo(Info: PInfo);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure b_PayClick(Sender: TObject);
    procedure BitBtn1Click(Sender: TObject);
    procedure b_GetInfoClick(Sender: TObject);
    procedure b_GetAddrInfoClick(Sender: TObject);
    procedure Button1Click(Sender: TObject);
    procedure b_SetChargeClick(Sender: TObject);
    procedure b_SearchClick(Sender: TObject);
    procedure Button4Click(Sender: TObject);
    procedure b_SetConsumptionNumClick(Sender: TObject);
    procedure b_SetConsumptionBySumNumClick(Sender: TObject);
    procedure b_SetChargeCntNumClick(Sender: TObject);
    procedure b_GetChargeCntNumClick(Sender: TObject);
    procedure Button3Click(Sender: TObject);
  private
    { Private declarations }
  public
    { Public declarations }
  end;

var
  f_LicNum: Tf_LicNum;
  LicNum1  : Integer;
  LicNum2  : Integer;
  Info     : PInfo;
InfoExp: string;

implementation

{$R *.dfm}

procedure Tf_LicNum.FormCreate(Sender: TObject);
begin
  Info := nil;
  InitLib ('BAZA', 'SOG');
  SetParameters (7, 211);
  SetRegion (1);
end;

procedure InfoAdd(name: string; obj: Variant);
begin
InfoExp := InfoExp + ';' +name +'=' + VarToStr(obj)
//tm_check.Lines.Add(name +'=' + VarToStr(obj));
//tm_check.Lines.Add('fdsfsfsd');
end;

procedure PrintInfo2(Info: PInfo);
var
  i        : integer;
  ServName : string;
  PUoM     : PChar;
  Koeff    : Double;
  Value    : PChar;
begin


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

procedure Tf_LicNum.PrintInfo(Info: PInfo);
var
  i        : integer;
  ServName : string;
  PUoM     : PChar;
  Koeff    : Double;
  Value    : PChar;
  Adr1:Integer;
    Adr2:Integer;
begin

 mm.Clear;
Adr1 := Info.Adr1;
Adr2 := Info.Adr2;
 mm.Lines.Add(IntToStr(Adr1));
  mm.Lines.Add(IntToStr(Adr2));

 l_AddrByLicNum.Caption := Info.PostAddress;
 mm.Lines.Add('');
 mm.Lines.Add('----- НАЧИСЛЕНИЯ -----');
 mm.Lines.Add('ЗА ' + IntToStr(Info.MonthPay));
 mm.Lines.Add('');
 mm.Lines.Add('   Вид   Услуга                 Долг       Начисления   Сумма оплаты');

 for i:=0 to Info.PayLen-1 do
  begin
   GetParamCnt(Info.Pay[i].Vid, Koeff, PUoM);
   ServName := GetServiceName(Info, Info.Pay[i].Vid);
  mm.Lines.Add(Format('  %3d  %-16s  %12.2f  %12.2f %12.2f',
               [Info.Pay[i].Vid,
                ServName,
                Info.Pay[i].Sal/100,
                Info.Pay[i].Nac/100,
                Info.Pay[i].Sum/100]));
  end;

 mm.Lines.Add('');
 mm.Lines.Add('----- СЧЕТЧИКИ -----');
 mm.Lines.Add('');
 mm.Lines.Add(' Вид Подвид Номер Наименование     Разр. Расход   Сумма     Тариф   Оплачено по');

 for i:=0 to Info.CntLen-1 do
  begin
   GetParamCnt(Info.Cnt[i].Vid, Koeff, PUoM);
   ServName := GetServiceName(Info, Info.Cnt[i].Vid);
    if Info.Cnt[i].Vid <> Info.Cnt[i].SubVid
     then ServName := Format('%s(%s)', [ServName, GetServiceName(Info, Info.Cnt[i].SubVid)]);
   ServName := ServName + ' ' + GetCounterName(Info, Info.Cnt[i].Vid, Info.Cnt[i].Number);

   GetValueCntNum(Info, Info.Cnt[i].Vid, Info.Cnt[i].Number, Value);

   mm.Lines.Add(Format('  %3d  %3d    %1d   %-16s  %2d    %3.1f  %8.2f  %8.2f  %8s  %5s  %5f',
                [Info.Cnt[i].Vid,
                 Info.Cnt[i].SubVid,
                 Info.Cnt[i].Number,
                 ServName,
                 Info.Cnt[i].Prec,
                 Info.Cnt[i].Rash*Koeff,
                 Info.Cnt[i].Sum/100,
                 Info.Cnt[i].Normal.Tar/100,
                 Info.Cnt[i].Value,
                 PUoM,
                 Koeff]));
  end;

 mm.Lines.Add('');
 mm.Lines.Add(Format('СУММА К ОПЛАТЕ - %12.2f',[GetTotalPayment(Info)/100]));
 mm.Lines.Add('');

 mm2.Clear;
 GetSharedData(Info);
 mm2.Lines.Add('ИНФОРМАЦИЯ:');
 for i := 0 to Info.SharedLen - 1 do
  mm2.Lines.Add(Format('  %3d - %12d', [Info.Shared[i].Code, Info.Shared[i].Value]));

end;

procedure Tf_LicNum.FormDestroy(Sender: TObject);
begin
 FreeAccount(Info);
end;

procedure Tf_LicNum.b_PayClick(Sender: TObject);
var
  R: Integer;
begin
  R := WritePayment(Info);
  case R of
    KVC_OK:
      ShowMessage('Запись выполнена');
    KVC_ZERO_PAYMENT:
      ShowMessage('Нельзя записать нулевой платеж');
  else
    ShowMessage('Ошибка при записи')
  end;
end;

procedure Tf_LicNum.BitBtn1Click(Sender: TObject);
begin
 FillCharges(Info, BY_NACH);
 PrintInfo(Info);
end;

procedure Tf_LicNum.b_GetInfoClick(Sender: TObject);
var
 i : integer;
 ServName : string;
begin
 StatusBar1.SimpleText := 'Идёт выборка данных...';
 FreeAccount(Info);
 Cb_1.Clear;
 Cb_2.Clear;
 
 LicNum1 := StrToIntDef(e_street.Text, 0) * 100000
          + StrToIntDef(e_house.Text, 0) * 100
          + StrToIntDef(e_corp.Text, 0);

 LicNum2 := StrToIntDef(e_place.Text, 0) * 1000
          + StrToIntDef(e_komnata.Text, 0) * 100
          + StrToIntDef(e_contr.Text, 0);

 if GetAccount(LicNum1, LicNum2, Info) = 0
  then
   begin
    l_AddrByLicNum.Caption := Info.PostAddress;
    PrintInfo(Info);

    if Info.PayLen > 0
     then
      begin
       for i:=0 to Info.PayLen-1 do
        begin
         ServName := GetServiceName(Info, Info.Pay[i].Vid);
         cb_2.Items.Add(Format('%.3d  %-12s', [Info.Pay[i].Vid, ServName]));
        end;
      end;

    if Info.CntLen > 0
     then
      begin
       for i:=0 to Info.CntLen-1 do
        begin
         ServName := GetServiceName(Info, Info.Cnt[i].Vid);
         if Info.Cnt[i].Vid <> Info.Cnt[i].SubVid
          then ServName := Format('%s(%s)', [ServName, GetServiceName(Info, Info.Cnt[i].SubVid)]);
         ServName := ServName + ' ' + GetCounterName(Info, Info.Cnt[i].Vid, Info.Cnt[i].Number);
         cb_1.Items.Add(Format('%.3d%.3d  %-12s', [Info.Cnt[i].Vid, Info.Cnt[i].Number, ServName]));
        end;
      end;
 end;
 StatusBar1.SimpleText := 'Выборка данных завершена';
end;

procedure Tf_LicNum.b_GetAddrInfoClick(Sender: TObject);
begin
  FillCharges(Info, BY_DOLG);
  PrintInfo(Info);
end;

procedure Tf_LicNum.Button1Click(Sender: TObject);
var
 Sum : integer;
 Values    : string;
 Vid_cb    : integer;
begin
 Values := cb_2.Text;
 Vid_cb := StrToInt(AnsiMidStr(Values, 1, 3));
 GetCharge (Info, Vid_cb, Sum);
 e_Amount.Text := IntToStr(Sum);
end;

procedure Tf_LicNum.b_SetChargeClick(Sender: TObject);
var
 Sum : integer;
 Values    : string;
 Vid_cb    : integer;
begin
 Values := cb_2.Text;
 Vid_cb := StrToInt(AnsiMidStr(Values, 1, 3));

 Sum := StrToInt(e_Amount.Text);
 SetCharge (Info, Vid_cb, Sum);
 PrintInfo(Info);
end;

procedure Tf_LicNum.b_SearchClick(Sender: TObject);
var
 i,j,k,m    : integer;
 counter    : integer;
 CounterLen : integer;
 TBegin, TEnd : TTime;
 TDelta     : TTime;
begin
 StatusBar1.SimpleText := 'Идёт выборка данных...';
 FreeAccount(Info);
 mm.Clear;
 i := 0;
 j := 0;
 k := 0;
 m := 0;
 counter := 0;
 CounterLen := 0;
 TBegin := Now;

 for j:=1 to 999 do
  begin
   for i:=1 to 99 do
    begin
     for k:=1 to 99 do
      begin
       LicNum1 := StrToIntDef(IntToStr(j), 0) * 100000 + StrToIntDef(IntToStr(i), 0) * 100 + StrToIntDef(e_corp.Text, 0);
       LicNum2 := StrToIntDef(IntToStr(i), 0) * 1000 + StrToIntDef(IntToStr(k), 0) * 100 + StrToIntDef(IntToStr(1), 0);
       if GetAccount(LicNum1, LicNum2, Info) = 0
        then
         begin
          f_LicNum.Update;
          counter := counter + 1;
          //CounterLen := Info.CntLen - 1;
          if ((k > 0) and (k < 10))
           then mm.Lines.Add('!!! Л/с - ' + IntToStr(LicNum1) + ' - ' + IntToStr(LicNum2) + ' ' + Info.PostAddress + ': ' + ' Номер счетчика - '); //+ IntToStr(Info.Cnt[i].Number));
          //for m :=0 to CounterLen do
          // begin
          //  if Info.Cnt[i].Number = 3
          //   then mm.Lines.Add('!!! Л/с - ' + IntToStr(LicNum1) + ' - ' + IntToStr(LicNum2) + ' ' + Info.PostAddress + ': ' + ' Номер счетчика - ' + IntToStr(Info.Cnt[i].Number));
          // end;
         end;
      end;
    end;
  end;

 TEnd := Now;
 TDelta := TEnd - TBegin;
 StatusBar1.SimpleText := 'Выборка данных завершена.';
 Label7.Caption := 'Обработано ' + IntToStr(counter) + ' адресов';
 Label8.Caption := 'TBegin ' + TimeToStr(TBegin);
 Label9.Caption := 'TEnd ' + TimeToStr(TEnd);
 Label10.Caption := 'TDelta ' + TimeToStr(TDelta); 
end;

procedure Tf_LicNum.Button4Click(Sender: TObject);
begin
  DistributeMoney(Info, StrToIntDef(Edit2.Text, 0));
  PrintInfo(Info);

//PrintInfo2(Info);
//edt1.Text :=  InfoExp;

end;

procedure Tf_LicNum.b_SetConsumptionNumClick(Sender: TObject);
var
 Sum : integer;
 Values    : string;
 Vid_cb    : integer;
 Number_cb : integer;
begin
 Values := cb_1.Text;
 Vid_cb := StrToInt(AnsiMidStr(Values, 1, 3));
 Number_cb := StrToInt(AnsiMidStr(Values, 4, 3));

 edt1.Text := IntToStr(Vid_cb) + ' - ' + IntToStr(Number_cb) + ' - ' + e_Amount.Text;

 Sum := StrToInt(e_Amount.Text);
 SetConsumptionNum (Info, Vid_cb, Number_cb, Sum);
 PrintInfo(Info);
end;

procedure Tf_LicNum.b_SetConsumptionBySumNumClick(Sender: TObject);
var
 Sum : integer;
 Vid : integer;
 Values    : string;
 Vid_cb    : integer;
 Number_cb : integer;
begin
 Values := cb_1.Text;
 Vid_cb := StrToInt(AnsiMidStr(Values, 1, 3));
 Number_cb := StrToInt(AnsiMidStr(Values, 4, 3));

 Sum := StrToInt(e_Amount.Text);
 SetConsumptionBySumNum (Info, Vid_cb, Number_cb, Sum);
 PrintInfo(Info);
end;

procedure Tf_LicNum.b_SetChargeCntNumClick(Sender: TObject);
var
 Sum : integer;
 Vid : integer;
 Values    : string;
 Vid_cb    : integer;
 Number_cb : integer;
begin
 Values := cb_1.Text;
 Vid_cb := StrToInt(AnsiMidStr(Values, 1, 3));
 Number_cb := StrToInt(AnsiMidStr(Values, 4, 3));

 Sum := StrToInt(e_Amount.Text);
 SetChargeCntNum (Info, Vid_cb, Vid_cb, Number_cb, Sum);
 PrintInfo(Info);
end;

procedure Tf_LicNum.b_GetChargeCntNumClick(Sender: TObject);
var
 Sum : integer;
 Vid : integer;
 Values    : string;
 Vid_cb    : integer;
 Number_cb : integer;
begin
 Values := cb_1.Text;
 Vid_cb := StrToInt(AnsiMidStr(Values, 1, 3));
 Number_cb := StrToInt(AnsiMidStr(Values, 4, 3));

 GetChargeCntNum (Info, Vid_cb, Number_cb, Sum);
 e_Amount.Text := IntToStr(Sum);
 PrintInfo(Info);
end;

procedure Tf_LicNum.Button3Click(Sender: TObject);
begin
 FillCharges(Info, BY_DOLG or BY_NACH);
 PrintInfo(Info);
end;




end.

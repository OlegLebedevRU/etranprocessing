object f_LicNum: Tf_LicNum
  Left = 226
  Top = 132
  Width = 921
  Height = 788
  Caption = 'f_LicNum'
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'MS Sans Serif'
  Font.Style = []
  OldCreateOrder = False
  OnCreate = FormCreate
  OnDestroy = FormDestroy
  PixelsPerInch = 96
  TextHeight = 13
  object l_AddrByLicNum: TLabel
    Left = 400
    Top = 28
    Width = 78
    Height = 13
    Caption = 'l_AddrByLicNum'
  end
  object Label1: TLabel
    Left = 64
    Top = 8
    Width = 32
    Height = 13
    Caption = #1059#1083#1080#1094#1072
  end
  object Label2: TLabel
    Left = 128
    Top = 8
    Width = 23
    Height = 13
    Caption = #1044#1086#1084
  end
  object Label3: TLabel
    Left = 176
    Top = 8
    Width = 36
    Height = 13
    Caption = #1050#1086#1088#1087#1091#1089
  end
  object Label4: TLabel
    Left = 224
    Top = 8
    Width = 48
    Height = 13
    Caption = #1050#1074#1072#1088#1090#1080#1088#1072
  end
  object Label5: TLabel
    Left = 282
    Top = 8
    Width = 44
    Height = 13
    Caption = #1050#1086#1084#1085#1072#1090#1072
  end
  object Label6: TLabel
    Left = 352
    Top = 8
    Width = 14
    Height = 13
    Caption = #1050#1056
  end
  object Label7: TLabel
    Left = 672
    Top = 184
    Width = 32
    Height = 13
    Caption = 'Label7'
  end
  object Label8: TLabel
    Left = 672
    Top = 200
    Width = 32
    Height = 13
    Caption = 'Label8'
  end
  object Label9: TLabel
    Left = 672
    Top = 216
    Width = 32
    Height = 13
    Caption = 'Label9'
  end
  object Label10: TLabel
    Left = 672
    Top = 232
    Width = 38
    Height = 13
    Caption = 'Label10'
  end
  object Label11: TLabel
    Left = 24
    Top = 96
    Width = 48
    Height = 13
    Caption = #1047#1085#1072#1095#1077#1085#1080#1077
  end
  object Label12: TLabel
    Left = 104
    Top = 136
    Width = 237
    Height = 13
    Caption = #1059#1089#1083#1091#1075#1072', '#1079#1072' '#1082#1086#1090#1086#1088#1091#1102' '#1073#1091#1076#1077#1090' '#1087#1088#1086#1080#1079#1074#1077#1076#1077#1085#1072' '#1086#1087#1083#1072#1090#1072
  end
  object e_street: TEdit
    Left = 56
    Top = 24
    Width = 49
    Height = 21
    TabOrder = 0
    Text = '219'
  end
  object e_house: TEdit
    Left = 112
    Top = 24
    Width = 49
    Height = 21
    TabOrder = 1
    Text = '33'
  end
  object e_corp: TEdit
    Left = 168
    Top = 24
    Width = 49
    Height = 21
    TabOrder = 2
    Text = '00'
  end
  object e_place: TEdit
    Left = 224
    Top = 24
    Width = 49
    Height = 21
    TabOrder = 3
    Text = '38'
  end
  object e_komnata: TEdit
    Left = 280
    Top = 24
    Width = 49
    Height = 21
    TabOrder = 4
    Text = '0'
  end
  object e_contr: TEdit
    Left = 336
    Top = 24
    Width = 49
    Height = 21
    TabOrder = 5
    Text = '1'
  end
  object b_GetInfo: TButton
    Left = 24
    Top = 56
    Width = 193
    Height = 25
    Caption = #1055#1086#1083#1091#1095#1080#1090#1100' '#1076#1072#1085#1085#1099#1077' '#1087#1086' '#1089#1095#1077#1090#1091
    TabOrder = 6
    OnClick = b_GetInfoClick
  end
  object b_Pay: TButton
    Left = 384
    Top = 304
    Width = 153
    Height = 25
    Caption = #1047#1040#1055#1048#1057#1040#1058#1068' '#1055#1051#1040#1058#1045#1046
    TabOrder = 7
    OnClick = b_PayClick
  end
  object mm: TMemo
    Left = 8
    Top = 336
    Width = 713
    Height = 393
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -11
    Font.Name = 'Courier New'
    Font.Style = []
    ParentFont = False
    TabOrder = 8
  end
  object StatusBar1: TStatusBar
    Left = 0
    Top = 740
    Width = 913
    Height = 19
    Panels = <>
  end
  object b_GetAddrInfo: TBitBtn
    Left = 224
    Top = 56
    Width = 137
    Height = 25
    Caption = #1056#1077#1078#1080#1084' "'#1044#1054#1051#1043'"'
    TabOrder = 10
    OnClick = b_GetAddrInfoClick
  end
  object BitBtn1: TBitBtn
    Left = 368
    Top = 56
    Width = 137
    Height = 25
    Caption = #1056#1077#1078#1080#1084' "'#1053#1040#1063#1048#1057#1051#1045#1053#1048#1071'"'
    TabOrder = 11
    OnClick = BitBtn1Click
  end
  object e_Amount: TEdit
    Left = 24
    Top = 112
    Width = 97
    Height = 21
    TabOrder = 12
    Text = '0'
  end
  object Button1: TButton
    Left = 272
    Top = 216
    Width = 305
    Height = 25
    Caption = #1055#1086#1083#1091#1095#1080#1090#1100' '#1090#1077#1082#1091#1097#1091#1102' '#1089#1091#1084#1084#1091' '#1086#1087#1083#1072#1090#1099' '#1079#1072' '#1091#1089#1083#1091#1075#1091' '#1087#1086' '#1085#1072#1095#1080#1089#1083#1077#1085#1080#1102
    TabOrder = 13
    OnClick = Button1Click
  end
  object b_SetCharge: TButton
    Left = 272
    Top = 184
    Width = 305
    Height = 25
    Caption = #1059#1089#1090#1072#1085#1086#1074#1080#1090#1100' '#1089#1091#1084#1084#1091' '#1079#1072' '#1091#1089#1083#1091#1075#1091' '#1087#1086' '#1085#1072#1095#1080#1089#1083#1077#1085#1080#1102
    TabOrder = 14
    OnClick = b_SetChargeClick
  end
  object Edit2: TEdit
    Left = 8
    Top = 304
    Width = 121
    Height = 21
    TabOrder = 15
    Text = '0'
  end
  object Button4: TButton
    Left = 136
    Top = 304
    Width = 241
    Height = 25
    Caption = #1056#1072#1089#1087#1088#1077#1076#1077#1083#1080#1090#1100' '#1074#1085#1077#1089#1105#1085#1085#1091#1102' '#1089#1091#1084#1084#1091' '#1087#1086' '#1091#1089#1083#1091#1075#1072#1084
    TabOrder = 16
    OnClick = Button4Click
  end
  object mm2: TMemo
    Left = 728
    Top = 136
    Width = 177
    Height = 593
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -11
    Font.Name = 'Courier New'
    Font.Style = []
    ParentFont = False
    TabOrder = 17
  end
  object b_SetConsumptionNum: TButton
    Left = 8
    Top = 184
    Width = 257
    Height = 25
    Caption = #1059#1089#1090#1072#1085#1086#1074#1080#1090#1100' '#1088#1072#1089#1093#1086#1076' '#1079#1072' '#1091#1089#1083#1091#1075#1091' '#1087#1086' '#1089#1095#1077#1090#1095#1080#1082#1091
    TabOrder = 18
    OnClick = b_SetConsumptionNumClick
  end
  object b_SetChargeCntNum: TButton
    Left = 8
    Top = 216
    Width = 257
    Height = 25
    Caption = #1059#1089#1090#1072#1085#1086#1074#1080#1090#1100' '#1089#1091#1084#1084#1091' '#1079#1072' '#1091#1089#1083#1091#1075#1091' '#1087#1086' '#1089#1095#1077#1090#1095#1080#1082#1091
    TabOrder = 19
    OnClick = b_SetChargeCntNumClick
  end
  object b_GetChargeCntNum: TButton
    Left = 8
    Top = 248
    Width = 257
    Height = 25
    Caption = #1055#1086#1083#1091#1095#1080#1090#1100' '#1090#1077#1082#1091#1097#1091#1102' '#1089#1091#1084#1084#1091' '#1079#1072' '#1091#1089#1083#1091#1075#1091' '#1087#1086' '#1089#1095#1077#1090#1095#1080#1082#1091
    TabOrder = 20
    OnClick = b_GetChargeCntNumClick
  end
  object cb_1: TComboBox
    Left = 24
    Top = 152
    Width = 153
    Height = 21
    ItemHeight = 13
    TabOrder = 21
    Text = #1055#1086' '#1089#1095#1077#1090#1095#1080#1082#1072#1084
  end
  object Button3: TButton
    Left = 512
    Top = 56
    Width = 201
    Height = 25
    Caption = #1056#1077#1078#1080#1084' "'#1044#1054#1051#1043'" + "'#1053#1040#1063#1048#1057#1051#1045#1053#1048#1071'"'
    TabOrder = 22
    OnClick = Button3Click
  end
  object cb_2: TComboBox
    Left = 272
    Top = 152
    Width = 153
    Height = 21
    ItemHeight = 13
    TabOrder = 23
    Text = #1055#1086' '#1085#1072#1095#1080#1089#1083#1077#1085#1080#1102
  end
  object edt1: TEdit
    Left = 176
    Top = 96
    Width = 713
    Height = 21
    TabOrder = 24
    Text = 'edt1'
  end
end
